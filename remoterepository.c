/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2007,2008,2009,2012 Bernhard R. Link
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02111-1301  USA
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>

#include "globals.h"
#include "error.h"
#include "ignore.h"
#include "filecntl.h"
#include "checksums.h"
#include "mprintf.h"
#include "dirs.h"
#include "chunks.h"
#include "names.h"
#include "aptmethod.h"
#include "signature.h"
#include "readtextfile.h"
#include "uncompression.h"
#include "diffindex.h"
#include "rredpatch.h"
#include "remoterepository.h"

/* This is code to handle lists from remote repositories.
   Those are stored in the lists/ (or --listdir) directory
   and needs some maintaince:

   - cleaning (unneeded) lists from that directory
   - deciding what to download from a remote repository
     (needs knowledge what is there and what is there)
   - in the future: implement ed to use remote .diffs
*/

struct remote_repository {
	struct remote_repository *next, *prev;

	/* repository is determined by pattern name currently.
	 * That might change if there is some safe way to combine
	 * some. (note that method options might make equally looking
	 * repositories different ones, so that is hard to decide).
	 *
	 * This is possible as pattern is not modifyable in options
	 * or method by the using distribution.
	 */
	const char *name;
	const char *method;
	const char *fallback;
	const struct strlist *config;

	struct aptmethod *download;

	struct remote_distribution *distributions;
};
static struct remote_repository *repositories = NULL;

struct remote_distribution {
	struct remote_distribution *next;

	/* repository and suite uniquely identify it,
	   as the only thing the distribution can change is the suite.
	   Currently most of the other fields would also fit in the
	   remote_repository structure, but I plan to add new patters
	   allowing this by distribution...
	*/
	struct remote_repository *repository;
	char *suite;

	/* flat repository */
	bool flat; bool flatnonflatwarned;
	char *suite_base_dir;

	/* if true, do not download or check Release file */
	bool ignorerelease;
	/* hashes to ignore */
	bool ignorehashes[cs_hashCOUNT];

	/* linked list of key descriptions to check against, each must match */
	struct signature_requirement *verify;

	/* local copy of InRelease, Release and Release.gpg file,
	 * only set if available */
	char *inreleasefile;
	char *releasefile;
	char *releasegpgfile;
	const char *usedreleasefile;

	/* filenames and checksums from the Release file */
	struct checksumsarray remotefiles;

	/* the index files we need */
	struct remote_index *indices;

	/* InRelease failed or requested not to be used */
	bool noinrelease;
};

struct remote_index {
	/* next index in remote distribution */
	struct remote_index *next;

	struct remote_distribution *from;

	/* what to download? .gz better than .bz2? and so on */
	struct encoding_preferences downloadas;

	/* remote filename as to be found in Release file*/
	char *filename_in_release;

	/* the name without suffix in the lists/ dir */
	char *cachefilename;
	/* the basename of the above */
	const char *cachebasename;

	/* index in checksums for the different types, -1 = not avail */
	int ofs[c_COUNT], diff_ofs;

	/* index in requested download methods so we can continue later */
	int lasttriedencoding;
	/* the compression to be tried currently */
	enum compression compression;

	/* the old uncompressed file, so that it is only deleted
	 * when needed, to avoid losing it for a patch run */
	/*@dependant@*/struct cachedlistfile *olduncompressed;
	struct checksums *oldchecksums;

	/* if using pdiffs, the content of the Packages.diff/Index: */
	struct diffindex *diffindex;
	/* the last patch queued to be applied */
	char *patchfilename;
	/*@dependant@*/const struct diffindex_patch *selectedpatch;
	bool deletecompressedpatch;

	bool queued;
	bool needed;
	bool got;
};

#define MAXPARTS 5
struct cachedlistfile {
	struct cachedlistfile *next;
	const char *basefilename;
	unsigned int partcount;
	const char *parts[MAXPARTS];
	/* might be used by some rule */
	bool needed, deleted;
	char fullfilename[];
};


static void remote_index_free(/*@only@*/struct remote_index *i) {
	if (i == NULL)
		return;
	free(i->cachefilename);
	free(i->patchfilename);
	free(i->filename_in_release);
	diffindex_free(i->diffindex);
	checksums_free(i->oldchecksums);
	free(i);
}

static void remote_distribution_free(/*@only@*/struct remote_distribution *d) {
	if (d == NULL)
		return;
	free(d->suite);
	signature_requirements_free(d->verify);
	free(d->inreleasefile);
	free(d->releasefile);
	free(d->releasegpgfile);
	free(d->suite_base_dir);
	checksumsarray_done(&d->remotefiles);
	while (d->indices != NULL) {
		struct remote_index *h = d->indices;
		d->indices = h->next;
		remote_index_free(h);
	}
	free(d);
}

void remote_repository_free(struct remote_repository *remote) {
	if (remote == NULL)
		return;
	while (remote->distributions != NULL) {
		struct remote_distribution *h = remote->distributions;
		remote->distributions = h->next;
		remote_distribution_free(h);
	}
	if (remote->next != NULL)
		remote->next->prev = remote->prev;
	if (remote->prev != NULL)
		remote->prev->next = remote->next;
	free(remote);
	return;
}

void cachedlistfile_freelist(struct cachedlistfile *c) {
	while (c != NULL) {
		struct cachedlistfile *n = c->next;
		free(c);
		c = n;
	}
}

void cachedlistfile_deleteunneeded(const struct cachedlistfile *c) {
	for (; c != NULL ; c = c->next) {
		if (c->needed)
			continue;
		if (verbose >= 0)
			printf("deleting %s\n", c->fullfilename);
		deletefile(c->fullfilename);
	}
}

static /*@null@*/ struct cachedlistfile *cachedlistfile_new(const char *basefilename, size_t len, size_t listdirlen) {
	struct cachedlistfile *c;
	size_t l;
	char *p;
	char ch;

	c = malloc(sizeof(struct cachedlistfile) + listdirlen + 2*len + 3);
	if (FAILEDTOALLOC(c))
		return NULL;
	c->next = NULL;
	c->needed = false;
	c->deleted = false;
	p = c->fullfilename;
	assert ((size_t)(p - (char*)c) <= sizeof(struct cachedlistfile));
	memcpy(p, global.listdir, listdirlen);
	p += listdirlen;
	*(p++) = '/';
	assert ((size_t)(p - c->fullfilename) == listdirlen + 1);
	c->basefilename = p;
	memcpy(p, basefilename, len); p += len;
	*(p++) = '\0';
	assert ((size_t)(p - c->fullfilename) == listdirlen + len + 2);

	c->parts[0] = p;
	c->partcount = 1;
	l = len;
	while (l-- > 0 && (ch = *(basefilename++)) != '\0') {
		if (ch == '_') {
			*(p++) = '\0';
			if (c->partcount < MAXPARTS)
				c->parts[c->partcount] = p;
			c->partcount++;
		} else if (ch == '%') {
			char first, second;

			if (len <= 1) {
				c->partcount = 0;
				return c;
			}
			first = *(basefilename++);
			second = *(basefilename++);
			if (first >= '0' && first <= '9')
				*p = (first - '0') << 4;
			else if (first >= 'a' && first <= 'f')
				*p = (first - 'a' + 10) << 4;
			else {
				c->partcount = 0;
				return c;
			}
			if (second >= '0' && second <= '9')
				*p |= (second - '0');
			else if (second >= 'a' && second <= 'f')
				*p |= (second - 'a' + 10);
			else {
				c->partcount = 0;
				return c;
			}
			p++;
		} else
			*(p++) = ch;
	}
	*(p++) = '\0';
	assert ((size_t)(p - c->fullfilename) <= listdirlen + 2*len + 3);
	return c;
}

retvalue cachedlists_scandir(/*@out@*/struct cachedlistfile **cachedfiles_p) {
	struct cachedlistfile *cachedfiles = NULL, **next_p;
	struct dirent *r;
	size_t listdirlen = strlen(global.listdir);
	DIR *dir;

	// TODO: check if it is always created before...
	dir = opendir(global.listdir);
	if (dir == NULL) {
		int e = errno;
		fprintf(stderr,
"Error %d opening directory '%s': %s!\n",
				e, global.listdir, strerror(e));
		return RET_ERRNO(e);
	}
	next_p = &cachedfiles;
	while (true) {
		size_t namelen;
		int e;

		errno = 0;
		r = readdir(dir);
		if (r == NULL) {
			e = errno;
			if (e == 0)
				break;
			/* this should not happen... */
			e = errno;
			fprintf(stderr, "Error %d reading dir '%s': %s!\n",
					e, global.listdir, strerror(e));
			(void)closedir(dir);
			cachedlistfile_freelist(cachedfiles);
			return RET_ERRNO(e);
		}
		namelen = _D_EXACT_NAMLEN(r);
		if (namelen == 1 && r->d_name[0] == '.')
			continue;
		if (namelen == 2 && r->d_name[0] == '.' && r->d_name[1] == '.')
			continue;
		*next_p = cachedlistfile_new(r->d_name, namelen, listdirlen);
		if (FAILEDTOALLOC(*next_p)) {
			(void)closedir(dir);
			cachedlistfile_freelist(cachedfiles);
			return RET_ERROR_OOM;
		}
		next_p = &(*next_p)->next;
	}
	if (closedir(dir) != 0) {
		int e = errno;
		fprintf(stderr, "Error %d closing directory '%s': %s!\n",
				e, global.listdir, strerror(e));
		cachedlistfile_freelist(cachedfiles);
		return RET_ERRNO(e);
	}
	*cachedfiles_p = cachedfiles;
	return RET_OK;
}

static retvalue cachedlistfile_delete(struct cachedlistfile *old) {
	int e;
	if (old->deleted)
		return RET_OK;
	e = deletefile(old->fullfilename);
	if (e != 0)
		return RET_ERRNO(e);
	old->deleted = true;
	return RET_OK;
}

struct remote_repository *remote_repository_prepare(const char *name, const char *method, const char *fallback, const struct strlist *config) {
	struct remote_repository *n;

	/* calling code ensures no two with the same name are created,
	 * so just create it... */

	n = zNEW(struct remote_repository);
	if (FAILEDTOALLOC(n))
		return NULL;
	n->name = name;
	n->method = method;
	n->fallback = fallback;
	n->config = config;

	n->next = repositories;
	if (n->next != NULL)
		n->next->prev = n;
	repositories = n;

	return n;
}

/* This escaping is quite harsh, but so nothing bad can happen... */
static inline size_t escapedlen(const char *p) {
	size_t l = 0;
	if (*p == '-') {
		l = 3;
		p++;
	}
	while (*p != '\0') {
		if ((*p < 'A' || *p > 'Z') && (*p < 'a' || *p > 'z') &&
		    (*p < '0' || *p > '9') && *p != '-')
			l +=3;
		else
			l++;
		p++;
	}
	return l;
}

static inline char *escapedcopy(char *dest, const char *orig) {
	static char hex[16] = "0123456789ABCDEF";
	if (*orig == '-') {
		orig++;
		*dest = '%'; dest++;
		*dest = '2'; dest++;
		*dest = 'D'; dest++;
	}
	while (*orig != '\0') {
		if ((*orig < 'A' || *orig > 'Z')
				&& (*orig < 'a' || *orig > 'z')
				&& (*orig < '0' || *orig > '9')
				&& *orig != '-') {
			*dest = '%'; dest++;
			*dest = hex[(*orig >> 4)& 0xF ]; dest++;
			*dest = hex[*orig & 0xF ]; dest++;
		} else {
			*dest = *orig;
			dest++;
		}
		orig++;
	}
	return dest;
}

char *genlistsfilename(const char *type, unsigned int count, ...) {
	const char *fields[count];
	unsigned int i;
	size_t listdir_len, type_len, len;
	char *result, *p;
	va_list ap;

	len = 0;
	va_start(ap, count);
	for (i = 0 ; i < count ; i++) {
		fields[i] = va_arg(ap, const char*);
		assert (fields[i] != NULL);
		len += escapedlen(fields[i]) + 1;
	}
	/* check sentinel */
	assert (va_arg(ap, const char*) == NULL);
	va_end(ap);
	listdir_len = strlen(global.listdir);
	if (type != NULL)
		type_len = strlen(type);
	else
		type_len = 0;

	result = malloc(listdir_len + type_len + len + 2);
	if (FAILEDTOALLOC(result))
		return NULL;
	memcpy(result, global.listdir, listdir_len);
	p = result + listdir_len;
	*(p++) = '/';
	for (i = 0 ; i < count ; i++) {
		p = escapedcopy(p, fields[i]);
		*(p++) = '_';
	}
	assert ((size_t)(p - result) == listdir_len + len + 1);
	if (type != NULL)
		memcpy(p, type, type_len + 1);
	else
		*(--p) = '\0';
	return result;
}

void cachedlistfile_need(struct cachedlistfile *list, const char *type, unsigned int count, ...) {
	struct cachedlistfile *file;
	const char *fields[count];
	unsigned int i;
	va_list ap;

	va_start(ap, count);
	for (i = 0 ; i < count ; i++) {
		fields[i] = va_arg(ap, const char*);
		assert (fields[i] != NULL);
	}
	/* check sentinel */
	assert (va_arg(ap, const char*) == NULL);
	va_end(ap);

	for (file = list ; file != NULL ; file = file->next) {
		if (file->partcount != count + 1)
			continue;
		i = 0;
		while (i < count && strcmp(file->parts[i], fields[i]) == 0)
			i++;
		if (i < count)
			continue;
		if (strcmp(type, file->parts[i]) != 0)
			continue;
		file->needed = true;
	}
}

retvalue remote_distribution_prepare(struct remote_repository *repository, const char *suite, bool ignorerelease, bool getinrelease, const char *verifyrelease, bool flat, bool *ignorehashes, struct remote_distribution **out_p) {
	struct remote_distribution *n, **last;
	enum checksumtype cs;

	for (last = &repository->distributions ; (n = *last) != NULL
	                                       ; last = &n->next) {
		if (strcmp(n->suite, suite) != 0)
			continue;
		if (n->flat != flat) {
			if (verbose >= 0 && !n->flatnonflatwarned &&
					!IGNORABLE(flatandnonflat))
				fprintf(stderr,
"Warning: From the same remote repository '%s', distribution '%s'\n"
"is requested both flat and non-flat. While this is possible\n"
"(having %s/dists/%s and %s/%s), it is unlikely.\n"
"To no longer see this message, use --ignore=flatandnonflat.\n",
					repository->method, suite,
					repository->method, suite,
					repository->method, suite);
			n->flatnonflatwarned = true;
			continue;
		}
		break;
	}

	if (*last != NULL) {
		n = *last;
		assert (n->flat == flat);

		if ((n->ignorerelease && !ignorerelease) ||
		    (!n->ignorerelease && ignorerelease)) {
			// TODO a hint which two are at fault would be nice,
			// but how to get the information...
			if (verbose >= 0)
				fprintf(stderr,
"Warning: I was told to both ignore Release files for Suite '%s'\n"
"from remote repository '%s' and to not ignore it. Going to not ignore!\n",
						suite, repository->name);
			n->ignorerelease = false;
		}
		if ((n->noinrelease && getinrelease) ||
		    (!n->noinrelease && !getinrelease)) {
			if (verbose >= 0)
				fprintf(stderr,
"Warning: Conflicting GetInRelease values for Suite '%s'\n"
"from remote repository '%s'. Resolving to get InRelease files!\n",
						suite, repository->name);
			n->noinrelease = false;
		}
		for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++) {
			if ((n->ignorehashes[cs] && !ignorehashes[cs]) ||
			    (!n->ignorehashes[cs] && ignorehashes[cs])) {
				// TODO dito
				if (verbose >= 0)
					fprintf(stderr,
"Warning: I was told to both ignore '%s' for Suite '%s'\n"
"from remote repository '%s' and to not ignore it. Going to not ignore!\n",
						suite,
						release_checksum_names[cs],
						repository->name);
				n->ignorehashes[cs] = false;
			}
		}
		if (verifyrelease != NULL) {
			retvalue r;

			r = signature_requirement_add(&n->verify,
					verifyrelease);
			if (RET_WAS_ERROR(r))
				return r;
		}
		*out_p = n;
		return RET_OK;
	}

	n = zNEW(struct remote_distribution);
	if (FAILEDTOALLOC(n))
		return RET_ERROR_OOM;
	n->repository = repository;
	n->suite = strdup(suite);
	n->ignorerelease = ignorerelease;
	n->noinrelease = !getinrelease;
	if (verifyrelease != NULL) {
		retvalue r;

		r = signature_requirement_add(&n->verify, verifyrelease);
		if (RET_WAS_ERROR(r)) {
			remote_distribution_free(n);
			return r;
		}
	}
	memcpy(n->ignorehashes, ignorehashes, sizeof(bool [cs_hashCOUNT]));
	n->flat = flat;
	if (flat)
		n->suite_base_dir = strdup(suite);
	else
		n->suite_base_dir = calc_dirconcat("dists", suite);
	if (FAILEDTOALLOC(n->suite) ||
			FAILEDTOALLOC(n->suite_base_dir)) {
		remote_distribution_free(n);
		return RET_ERROR_OOM;
	}
	/* ignorerelease can be unset later, so always calculate the filename */
	if (flat)
		n->inreleasefile = genlistsfilename("InRelease", 3,
				repository->name, suite, "flat",
				ENDOFARGUMENTS);
	else
		n->inreleasefile = genlistsfilename("InRelease", 2,
				repository->name, suite, ENDOFARGUMENTS);
	if (FAILEDTOALLOC(n->inreleasefile)) {
		remote_distribution_free(n);
		return RET_ERROR_OOM;
	}
	if (flat)
		n->releasefile = genlistsfilename("Release", 3,
				repository->name, suite, "flat",
				ENDOFARGUMENTS);
	else
		n->releasefile = genlistsfilename("Release", 2,
				repository->name, suite, ENDOFARGUMENTS);
	if (FAILEDTOALLOC(n->releasefile)) {
		remote_distribution_free(n);
		return RET_ERROR_OOM;
	}
	n->releasegpgfile = calc_addsuffix(n->releasefile, "gpg");
	if (FAILEDTOALLOC(n->releasefile)) {
		remote_distribution_free(n);
		return RET_ERROR_OOM;
	}
	*last = n;
	*out_p = n;
	return RET_OK;
}

static retvalue copytoplace(const char *gotfilename, const char *wantedfilename, const char *method, struct checksums **checksums_p) {
	retvalue r;
	struct checksums *checksums = NULL;

	/* if the file is somewhere else, copy it: */
	if (strcmp(gotfilename, wantedfilename) != 0) {
		/* never link index files, but copy them */
		if (verbose > 1)
			fprintf(stderr,
"Copy file '%s' to '%s'...\n", gotfilename, wantedfilename);
		r = checksums_copyfile(wantedfilename, gotfilename, false,
				&checksums);
		if (r == RET_ERROR_EXIST) {
			fprintf(stderr,
"Unexpected error: '%s' exists while it should not!\n",
					wantedfilename);
		}
		if (r == RET_NOTHING) {
			fprintf(stderr,
"Cannot open '%s', obtained from '%s' method.\n",
					gotfilename, method);
			r = RET_ERROR_MISSING;
		}
		if (RET_WAS_ERROR(r)) {
			return r;
		}
	}
	if (checksums_p == NULL)
		checksums_free(checksums);
	else
		*checksums_p = checksums;
	return RET_OK;
}

static retvalue enqueue_old_release_files(struct remote_distribution *d);

/* handle a downloaded Release or Release.gpg file:
 * no checksums to test, nothing to trigger, as they have to be all
 * read at once to decide what is new and what actually needs downloading */
static retvalue release_callback(enum queue_action action, void *privdata, void *privdata2, UNUSED(const char *uri), const char *gotfilename, const char *wantedfilename, UNUSED(/*@null@*/const struct checksums *checksums), const char *methodname) {
	struct remote_distribution *d = privdata;
	retvalue r;

	/* if the InRelease file cannot be got,
	 * try Release (and Release.gpg if checking) instead */
	if (action == qa_error && privdata2 == d->inreleasefile) {
		assert (!d->noinrelease);

		return enqueue_old_release_files(d);
	}

	if (action != qa_got)
		return RET_ERROR;

	r = copytoplace(gotfilename, wantedfilename, methodname, NULL);
	if (RET_WAS_ERROR(r))
		return r;
	return r;
}

static retvalue enqueue_old_release_files(struct remote_distribution *d) {
	retvalue r;

	d->noinrelease = true;
	r = aptmethod_enqueueindex(d->repository->download,
			d->suite_base_dir, "Release", "",
			d->releasefile, "",
			release_callback, d, NULL);
	if (RET_WAS_ERROR(r))
		return r;
	if (d->verify != NULL) {
		r = aptmethod_enqueueindex(d->repository->download,
				d->suite_base_dir, "Release", ".gpg",
				d->releasegpgfile, "",
				release_callback, d, NULL);
		if (RET_WAS_ERROR(r))
			return r;
	}
	return RET_OK;
}

static retvalue remote_distribution_enqueuemetalists(struct remote_distribution *d) {
	struct remote_repository *repository = d->repository;

	assert (repository->download != NULL);

	if (d->ignorerelease)
		return RET_NOTHING;

	(void)unlink(d->inreleasefile);
	(void)unlink(d->releasefile);
	if (d->verify != NULL) {
		(void)unlink(d->releasegpgfile);
	}

	if (d->noinrelease)
		return enqueue_old_release_files(d);
	else
		return aptmethod_enqueueindex(repository->download,
			d->suite_base_dir, "InRelease", "", d->inreleasefile,
			"", release_callback, d, d->inreleasefile);
}

retvalue remote_startup(struct aptmethodrun *run) {
	struct remote_repository *rr;
	retvalue r;

	if (interrupted())
		return RET_ERROR_INTERRUPTED;

	for (rr = repositories ; rr != NULL ; rr = rr->next) {
		assert (rr->download == NULL);

		r = aptmethod_newmethod(run,
				rr->method, rr->fallback,
				rr->config, &rr->download);
		if (RET_WAS_ERROR(r))
			return r;
	}
	return RET_OK;
}

static void find_index(const struct strlist *files, struct remote_index *ri) {
	const char *filename = ri->filename_in_release;
	size_t len = strlen(filename);
	int i;
	enum compression c;

	for (i = 0 ; i < files->count ; i++) {
		const char *value = files->values[i];

		if (strncmp(value, filename, len) != 0)
			continue;

		value += len;

		if (*value == '\0') {
			ri->ofs[c_none] = i;
			continue;
		}
		if (*value != '.')
			continue;
		if (strcmp(value, ".diff/Index") == 0) {
			ri->diff_ofs = i;
			continue;
		}

		for (c = 0 ; c < c_COUNT ; c++)
			if (strcmp(value, uncompression_suffix[c]) == 0) {
				ri->ofs[c] = i;
				break;
			}
	}
}

/* get a strlist with the md5sums of a Release-file */
static inline retvalue release_getchecksums(const char *releasefile, const char *chunk, const bool ignorehash[cs_hashCOUNT], struct checksumsarray *out) {
	retvalue r;
	struct strlist files[cs_hashCOUNT];
	enum checksumtype cs;
	bool foundanything = false;

	for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++) {
		if (ignorehash[cs]) {
			strlist_init(&files[cs]);
			continue;
		}
		assert (release_checksum_names[cs] != NULL);
		r = chunk_getextralinelist(chunk, release_checksum_names[cs],
				&files[cs]);
		if (RET_WAS_ERROR(r)) {
			while (cs-- > cs_md5sum) {
				strlist_done(&files[cs]);
			}
			return r;
		} else if (r == RET_NOTHING)
			strlist_init(&files[cs]);
		else
			foundanything = true;
	}

	if (!foundanything) {
		fprintf(stderr, "Missing checksums in Release file '%s'!\n",
				releasefile);
		return RET_ERROR;
	}

	r = checksumsarray_parse(out, files, releasefile);
	for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++) {
		strlist_done(&files[cs]);
	}
	return r;
}

static retvalue process_remoterelease(struct remote_distribution *rd) {
	struct remote_repository *rr = rd->repository;
	struct remote_index *ri;
	retvalue r;
	char *releasedata;
	size_t releaselen;

	if (!rd->noinrelease) {
		r = signature_check_inline(rd->verify,
				rd->inreleasefile, &releasedata);
		assert (r != RET_NOTHING);
		if (r == RET_NOTHING)
			r = RET_ERROR_BADSIG;
		if (r == RET_ERROR_BADSIG) {
			fprintf(stderr,
"Error: Not enough signatures found for remote repository %s (%s %s)!\n",
					rr->name, rr->method, rd->suite);
			r = RET_ERROR_BADSIG;
		}
		if (RET_WAS_ERROR(r))
			return r;
		rd->usedreleasefile = rd->inreleasefile;
	} else {
		r = readtextfile(rd->releasefile, rd->releasefile,
				&releasedata, &releaselen);
		assert (r != RET_NOTHING);
		if (RET_WAS_ERROR(r))
			return r;
		rd->usedreleasefile = rd->releasefile;

		if (rd->verify != NULL) {
			r = signature_check(rd->verify,
					rd->releasegpgfile, rd->releasefile,
					releasedata, releaselen);
			assert (r != RET_NOTHING);
			if (r == RET_NOTHING)
				r = RET_ERROR_BADSIG;
			if (r == RET_ERROR_BADSIG) {
				fprintf(stderr,
"Error: Not enough signatures found for remote repository %s (%s %s)!\n",
					rr->name, rr->method, rd->suite);
				r = RET_ERROR_BADSIG;
			}
			if (RET_WAS_ERROR(r)) {
				free(releasedata);
				return r;
			}
		}
	}
	r = release_getchecksums(rd->usedreleasefile, releasedata,
			rd->ignorehashes, &rd->remotefiles);
	free(releasedata);
	if (RET_WAS_ERROR(r))
		return r;

	/* Check for our files in there */
	for (ri = rd->indices ; ri != NULL ; ri = ri->next) {
		find_index(&rd->remotefiles.names, ri);
	}
	// TODO: move checking if not exists at all to here?
	return RET_OK;
}

retvalue remote_preparemetalists(struct aptmethodrun *run, bool nodownload) {
	struct remote_repository *rr;
	struct remote_distribution *rd;
	retvalue r;

	if (!nodownload) {
		for (rr = repositories ; rr != NULL ; rr = rr->next) {
			for (rd = rr->distributions ; rd != NULL ;
			                              rd = rd->next) {
				r = remote_distribution_enqueuemetalists(rd);
				if (RET_WAS_ERROR(r))
					return r;
			}
		}
		r = aptmethod_download(run);
		if (RET_WAS_ERROR(r))
			return r;
	}
	for (rr = repositories ; rr != NULL ; rr = rr->next) {
		for (rd = rr->distributions ; rd != NULL ; rd = rd->next) {
			if (!rd->ignorerelease) {
				if (nodownload)
					if (!isregularfile(rd->inreleasefile))
						rd->noinrelease = true;
				r = process_remoterelease(rd);
				if (RET_WAS_ERROR(r))
					return r;
			}
		}
	}
	return RET_OK;
}

bool remote_index_isnew(/*@null@*/const struct remote_index *ri, struct donefile *done) {
	const char *basefilename;
	struct checksums *checksums;
	bool hashes_missing, improves;

	/* files without uncompressed checksum cannot be tested */
	if (ri->ofs[c_none] < 0)
		return true;
	/* if not there or the wrong files comes next, then something
	 * has changed and we better reload everything */
	if (!donefile_nextindex(done, &basefilename, &checksums))
		return true;
	if (strcmp(basefilename, ri->cachebasename) != 0) {
		checksums_free(checksums);
		return true;
	}
	/* otherwise check if the file checksums match */
	if (!checksums_check(checksums,
			ri->from->remotefiles.checksums[ri->ofs[c_none]],
			&hashes_missing)) {
		checksums_free(checksums);
		return true;
	}
	if (hashes_missing) {
		/* if Release has checksums we do not yet know about,
		 * process it to make sure those match as well */
		checksums_free(checksums);
		return true;
	}
	if (!checksums_check(ri->from->remotefiles.checksums[ri->ofs[c_none]],
				checksums, &improves)) {
		/* this should not happen, but ... */
		checksums_free(checksums);
		return true;
	}
	if (improves) {
		/* assume this is our file and add the other hashes so they
		 * will show up in the file again the next time.
		 * This is a bit unelegant in mixing stuff, but otherwise this
		 * will cause redownloading when remote adds more hashes.
		 * The only downside of mixing can reject files that have the
		 * same recorded hashes as a previously processed files.
		 * But that is quite inlikely unless on attack, so getting some
		 * hint in that case cannot harm.*/
		(void)checksums_combine(&ri->from->remotefiles.checksums[
				ri->ofs[c_none]], checksums, NULL);
	}
	checksums_free(checksums);
	return false;
}

static inline void remote_index_oldfiles(struct remote_index *ri, /*@null@*/struct cachedlistfile *oldfiles, /*@out@*/struct cachedlistfile *old[c_COUNT]) {
	struct cachedlistfile *o;
	size_t l;
	enum compression c;

	for (c = 0 ; c < c_COUNT ; c++)
		old[c] = NULL;

	l = strlen(ri->cachebasename);
	for (o = oldfiles ; o != NULL ; o = o->next) {
		if (o->deleted)
			continue;
		if (strncmp(o->basefilename, ri->cachebasename, l) != 0)
			continue;
		for (c = 0 ; c < c_COUNT ; c++)
			if (strcmp(o->basefilename + l,
			           uncompression_suffix[c]) == 0) {
				old[c] = o;
				o->needed = true;
				break;
			}
		if (strcmp(o->basefilename + l, ".diffindex") == 0)
			(void)cachedlistfile_delete(o);
		if (strncmp(o->basefilename + l, ".diff-", 6) == 0)
			(void)cachedlistfile_delete(o);
	}
}

static inline void remote_index_delete_oldfiles(struct remote_index *ri, /*@null@*/struct cachedlistfile *oldfiles) {
	struct cachedlistfile *o;
	size_t l;

	l = strlen(ri->cachebasename);
	for (o = oldfiles ; o != NULL ; o = o->next) {
		if (o->deleted)
			continue;
		if (strncmp(o->basefilename, ri->cachebasename, l) != 0)
			continue;
		(void)cachedlistfile_delete(o);
	}
}

static queue_callback index_callback;
static queue_callback diff_callback;

static retvalue queue_next_without_release(struct remote_distribution *rd, struct remote_index *ri) {
	const struct encoding_preferences *downloadas;
	static const struct encoding_preferences defaultdownloadas = {
		.count = 5,
		.requested = {
			{ .diff = false, .force = false, .compression = c_gzip },
			{ .diff = false, .force = false, .compression = c_bzip2 },
			{ .diff = false, .force = false, .compression = c_none },
			{ .diff = false, .force = false, .compression = c_lzma },
			{ .diff = false, .force = false, .compression = c_xz }
		}
	};
	int e;

	if (ri->downloadas.count == 0)
		downloadas = &defaultdownloadas;
	else
		downloadas = &ri->downloadas;

	for (e = ri->lasttriedencoding + 1 ; e < downloadas->count ; e++) {
		enum compression c = downloadas->requested[e].compression;

		if (downloadas->requested[e].diff)
			continue;
		if (uncompression_supported(c)) {
			ri->lasttriedencoding = e;
			ri->compression = c;
			return aptmethod_enqueueindex(rd->repository->download,
					rd->suite_base_dir,
					ri->filename_in_release,
					uncompression_suffix[c],
					ri->cachefilename,
					uncompression_suffix[c],
					index_callback, ri, NULL);
		}
	}
	if (ri->lasttriedencoding < 0)
		fprintf(stderr,
"ERROR: no supported compressions in DownloadListsAs for '%s' by '%s'!\n",
				rd->suite, rd->repository->method);
	ri->lasttriedencoding = e;
	return RET_ERROR;
}

static inline retvalue find_requested_encoding(struct remote_index *ri, const char *releasefile) {
	int e;
	enum compression c, stopat,
			 /* the most-preferred requested but unsupported */
			 unsupported = c_COUNT,
			 /* the best unrequested but supported */
			 unrequested = c_COUNT;

	if (ri->downloadas.count > 0) {
		bool found = false;
		for (e = ri->lasttriedencoding + 1 ;
		     e < ri->downloadas.count ;
		     e++) {
			struct compression_preference req;

			req = ri->downloadas.requested[e];

			if (req.diff) {
				if (ri->olduncompressed == NULL)
					continue;
				assert (ri->ofs[c_none] >= 0);
				if (!req.force && ri->diff_ofs < 0)
					continue;
				ri->compression = c_COUNT;
				ri->lasttriedencoding = e;
				return RET_OK;
			}
			if (ri->ofs[req.compression] < 0 &&
					(!req.force || ri->ofs[c_none] < 0))
				continue;
			if (uncompression_supported(req.compression)) {
				ri->compression = req.compression;
				ri->lasttriedencoding = e;
				return RET_OK;
			} else if (unsupported == c_COUNT)
				unsupported = req.compression;
		}
		if (ri->lasttriedencoding > -1) {
			/* we already tried something, and nothing else
			 * is available, so give up */
			ri->lasttriedencoding = e;
			return RET_ERROR;
		}

		/* nothing that is both requested by the user and supported
		 * and listed in the Release file found, check what is there
		 * to get a meaningfull error message */

		for (c = 0 ; c < c_COUNT ; c++) {
			if (ri->ofs[c] < 0)
				continue;
			found = true;
			if (uncompression_supported(c))
				unrequested = c;
		}

		if (!found) {
			// TODO: might be nice to check for not-yet-even
			// known about compressions and say they are not
			// yet know yet instead then here...
			fprintf(stderr,
"Could not find '%s' within '%s'\n",
					ri->filename_in_release, releasefile);
			return RET_ERROR_WRONG_MD5;
		}

		if (unsupported != c_COUNT && unrequested != c_COUNT) {
			fprintf(stderr,
"Error: '%s' only lists unusable or unrequested compressions of '%s'.\n"
"Try e.g the '%s' option (or check what it is set to) to make more useable.\n"
"Or change your DownloadListsAs to request e.g. '%s'.\n",
					releasefile, ri->filename_in_release,
					uncompression_option[unsupported],
					uncompression_config[unrequested]);
			return RET_ERROR;
		}
		if (unsupported != c_COUNT) {
			fprintf(stderr,
"Error: '%s' only lists unusable compressions of '%s'.\n"
"Try e.g the '%s' option (or check what it is set to) to make more useable.\n",
					releasefile, ri->filename_in_release,
					uncompression_option[unsupported]);
			return RET_ERROR;
		}
		if (unrequested != c_COUNT) {
			fprintf(stderr,
"Error: '%s' only lists unrequested compressions of '%s'.\n"
"Try changing your DownloadListsAs to request e.g. '%s'.\n",
					releasefile, ri->filename_in_release,
					uncompression_config[unrequested]);
			return RET_ERROR;
		}
		fprintf(stderr,
"Error: '%s' lists no requested and usable compressions of '%s'.\n",
					releasefile, ri->filename_in_release);
		return RET_ERROR;
	}
	/* When nothing specified, use the newest compression.
	 * This might make it slow on older computers (and perhaps
	 * on relatively new ones, too), but usually bandwidth costs
	 * and your time not.
	 * And you can always configure it to prefer a faster one...
	 */

	/* ri->lasttriedencoding -1 means nothing tried,
	 * 0 means Package.diff was tried,
	 * 1 means nothing c_COUNT - 1 was already tried,
	 * 2 means nothing c_COUNT - 2 was already tried,
	 * and so on...*/

	if (ri->lasttriedencoding < 0) {
		if (ri->olduncompressed != NULL && ri->diff_ofs >= 0) {
			ri->compression = c_COUNT;
			ri->lasttriedencoding = 0;
			return RET_OK;
		}
		stopat = c_COUNT;
	} else
		stopat = c_COUNT - ri->lasttriedencoding;

	ri->compression = c_COUNT;
	for (c = 0 ; c < stopat ; c++) {
		if (ri->ofs[c] < 0)
			continue;
		if (uncompression_supported(c))
			ri->compression = c;
		else
			unsupported = c;
	}
	if (ri->compression == c_COUNT) {
		if (ri->lasttriedencoding > -1) {
			/* not the first try, no error message needed */
			ri->lasttriedencoding = c_COUNT;
			return RET_ERROR;
		}
		if (unsupported != c_COUNT) {
			fprintf(stderr,
"Error: '%s' only lists unusable compressions of '%s'.\n"
"Try e.g the '%s' option (or check what it is set to) to enable more.\n",
					releasefile, ri->filename_in_release,
					uncompression_option[unsupported]);
			return RET_ERROR;
		}
		fprintf(stderr,
"Could not find '%s' within '%s'\n",
				ri->filename_in_release, releasefile);
		return RET_ERROR_WRONG_MD5;

	}
	ri->lasttriedencoding = c_COUNT - ri->compression;
	return RET_OK;
}

static inline retvalue remove_old_uncompressed(struct remote_index *ri) {
	retvalue r;

	if (ri->olduncompressed != NULL) {
		r = cachedlistfile_delete(ri->olduncompressed);
		ri->olduncompressed = NULL;
		return r;
	} else
		return RET_NOTHING;
}

static retvalue queue_next_encoding(struct remote_distribution *rd, struct remote_index *ri);

// TODO: check if this still makes sense.
// (might be left over to support switching from older versions
// of reprepro that also put compressed files there)
static inline retvalue reuse_old_compressed_index(struct remote_distribution *rd, struct remote_index *ri, enum compression c, const char *oldfullfilename) {
	retvalue r;

	r = uncompress_file(oldfullfilename, ri->cachefilename, c);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r))
		return r;
	if (ri->ofs[c_none] >= 0) {
		r = checksums_test(ri->cachefilename,
				rd->remotefiles.checksums[ri->ofs[c_none]],
				&rd->remotefiles.checksums[ri->ofs[c_none]]);
		if (r == RET_ERROR_WRONG_MD5) {
			fprintf(stderr,
"Error: File '%s' looked correct according to '%s',\n"
"but after unpacking '%s' looks wrong.\n"
"Something is seriously broken!\n",
					oldfullfilename, rd->usedreleasefile,
					ri->cachefilename);
		}
		if (r == RET_NOTHING) {
			fprintf(stderr,
"File '%s' mysteriously vanished!\n", ri->cachefilename);
			r = RET_ERROR_MISSING;
		}
		if (RET_WAS_ERROR(r))
			return r;
	}
	/* already there, nothing to do to get it... */
	ri->queued = true;
	ri->got = true;
	return RET_OK;
}

static inline retvalue queueindex(struct remote_distribution *rd, struct remote_index *ri, bool nodownload, /*@null@*/struct cachedlistfile *oldfiles) {
	enum compression c;
	retvalue r;
	struct cachedlistfile *old[c_COUNT];

	if (rd->ignorerelease) {
		ri->queued = true;
		if (nodownload) {
			ri->got = true;
			return RET_OK;
		}

		/* as there is no way to know which are current,
		 * just delete everything */
		remote_index_delete_oldfiles(ri, oldfiles);

		return queue_next_without_release(rd, ri);
	}

	/* check if this file is still available from an earlier download */
	remote_index_oldfiles(ri, oldfiles, old);
	ri->olduncompressed = NULL;
	ri->oldchecksums = NULL;
	if (ri->ofs[c_none] < 0 && old[c_none] != NULL) {
		/* if we know not what it should be,
		 * we canot use the old... */
		r = cachedlistfile_delete(old[c_none]);
		if (RET_WAS_ERROR(r))
			return r;
		old[c_none] = NULL;
	} else if (old[c_none] != NULL) {
		bool improves;
		int uo = ri->ofs[c_none];
		struct checksums **wanted_p = &rd->remotefiles.checksums[uo];

		r = checksums_read(old[c_none]->fullfilename,
				&ri->oldchecksums);
		if (r == RET_NOTHING) {
			fprintf(stderr, "File '%s' mysteriously vanished!\n",
					old[c_none]->fullfilename);
			r = RET_ERROR_MISSING;
		}
		if (RET_WAS_ERROR(r))
			return r;
		if (checksums_check(*wanted_p, ri->oldchecksums, &improves)) {
			/* already there, nothing to do to get it... */
			ri->queued = true;
			ri->got = true;
			if (improves)
				r = checksums_combine(wanted_p,
						ri->oldchecksums, NULL);
			else
				r = RET_OK;
			checksums_free(ri->oldchecksums);
			ri->oldchecksums = NULL;
			return r;
		}
		ri->olduncompressed = old[c_none];
		old[c_none] = NULL;
	}

	assert (old[c_none] == NULL);

	/* make sure everything old is deleted or check if it can be used */
	for (c = 0 ; c < c_COUNT ; c++) {
		if (old[c] == NULL)
			continue;
		if (c != c_none && ri->ofs[c] >= 0) {
			/* check if it can be used */
			r = checksums_test(old[c]->fullfilename,
					rd->remotefiles.checksums[ri->ofs[c]],
					&rd->remotefiles.checksums[ri->ofs[c]]);
			if (r == RET_ERROR_WRONG_MD5)
				r = RET_NOTHING;
			if (RET_WAS_ERROR(r))
				return r;
			if (RET_IS_OK(r)) {
				r = remove_old_uncompressed(ri);
				if (RET_WAS_ERROR(r))
					return r;
				assert (old[c_none] == NULL);
				return reuse_old_compressed_index(rd, ri, c,
						old[c]->fullfilename);
			}
		}
		r = cachedlistfile_delete(old[c]);
		if (RET_WAS_ERROR(r))
		return r;
		old[c] = NULL;
	}

	/* nothing found, we'll have to download: */

	if (nodownload) {
		if (ri->olduncompressed != NULL)
			fprintf(stderr,
"Error: '%s' does not match Release file, try without --nolistsdownload to download new one!\n",
				ri->cachefilename);
		else
			fprintf(stderr,
"Error: Missing '%s', try without --nolistsdownload to download it!\n",
				ri->cachefilename);
		return RET_ERROR_MISSING;
	}

	return queue_next_encoding(rd, ri);
}

static retvalue queue_next_encoding(struct remote_distribution *rd, struct remote_index *ri) {
	struct remote_repository *rr = rd->repository;
	retvalue r;

	if (rd->ignorerelease)
		return queue_next_without_release(rd, ri);

	r = find_requested_encoding(ri, rd->usedreleasefile);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r))
		return r;

	assert (ri->compression <= c_COUNT);

	/* check if downloading a .diff/Index (aka .pdiff) is requested */
	if (ri->compression == c_COUNT) {
		assert (ri->olduncompressed != NULL);
		assert (ri->oldchecksums != NULL);

		ri->queued = true;
		return aptmethod_enqueueindex(rr->download, rd->suite_base_dir,
				ri->filename_in_release, ".diff/Index",
				ri->cachefilename, ".diffindex",
				diff_callback, ri, NULL);
	}

	assert (ri->compression < c_COUNT);
	assert (uncompression_supported(ri->compression));

	if (ri->compression == c_none) {
		r = remove_old_uncompressed(ri);
		if (RET_WAS_ERROR(r))
			return r;
	}
/* as those checksums might be overwritten with completed data,
 * this assumes that the uncompressed checksums for one index is never
 * the compressed checksum for another... */

	ri->queued = true;
	return aptmethod_enqueueindex(rr->download, rd->suite_base_dir,
			ri->filename_in_release,
			uncompression_suffix[ri->compression],
			ri->cachefilename,
			uncompression_suffix[ri->compression],
			index_callback, ri, NULL);
}


static retvalue remote_distribution_enqueuelists(struct remote_distribution *rd, bool nodownload, struct cachedlistfile *oldfiles) {
	struct remote_index *ri;
	retvalue r;

	/* check what to get for the requested indicies */
	for (ri = rd->indices ; ri != NULL ; ri = ri->next) {
		if (ri->queued)
			continue;
		if (!ri->needed) {
			/* if we do not know anything about it,
			 * it cannot have got marked as old
			 * or otherwise as unneeded */
			assert (!rd->ignorerelease);
			continue;
		}
		r = queueindex(rd, ri, nodownload, oldfiles);
		if (RET_WAS_ERROR(r))
			return r;
	}
	return RET_OK;
}

retvalue remote_preparelists(struct aptmethodrun *run, bool nodownload) {
	struct remote_repository *rr;
	struct remote_distribution *rd;
	retvalue r;
	struct cachedlistfile *oldfiles;

	r = cachedlists_scandir(&oldfiles);
	if (RET_WAS_ERROR(r))
		return r;
	if (r == RET_NOTHING)
		oldfiles = NULL;

	for (rr = repositories ; rr != NULL ; rr = rr->next) {
		for (rd = rr->distributions ; rd != NULL
				; rd = rd->next) {
			r = remote_distribution_enqueuelists(rd,
					nodownload, oldfiles);
			if (RET_WAS_ERROR(r)) {
				cachedlistfile_freelist(oldfiles);
				return r;
			}
		}
	}
	r = aptmethod_download(run);
	if (RET_WAS_ERROR(r)) {
		cachedlistfile_freelist(oldfiles);
		return r;
	}

	cachedlistfile_freelist(oldfiles);
	return RET_OK;
}

static struct remote_index *addindex(struct remote_distribution *rd, /*@only@*/char *cachefilename, /*@only@*/char *filename, /*@null@*/const struct encoding_preferences *downloadas) {
	struct remote_index *ri, **last;
	enum compression c;
	const char *cachebasename;

	if (FAILEDTOALLOC(cachefilename) || FAILEDTOALLOC(filename))
		return NULL;

	cachebasename = dirs_basename(cachefilename);
	last = &rd->indices;
	while (*last != NULL && strcmp((*last)->cachebasename, cachebasename) != 0)
		last = &(*last)->next;
	if (*last != NULL) {
		ri = *last;
		// TODO: perhaps try to calculate some form of intersections
		// instead of just using the shorter one...
		if (downloadas != NULL &&
				(ri->downloadas.count == 0
				 || ri->downloadas.count > downloadas->count))
			ri->downloadas = *downloadas;
		free(cachefilename); free(filename);
		return ri;
	}

	ri = zNEW(struct remote_index);
	if (FAILEDTOALLOC(ri)) {
		free(cachefilename); free(filename);
		return NULL;
	}

	*last = ri;
	ri->from = rd;
	ri->cachefilename = cachefilename;
	ri->cachebasename = cachebasename;
	ri->filename_in_release = filename;
	if (downloadas != NULL)
		ri->downloadas = *downloadas;
	for (c = 0 ; c < c_COUNT ; c++)
		ri->ofs[c] = -1;
	ri->diff_ofs = -1;
	ri->lasttriedencoding = -1;
	return ri;
}

struct remote_index *remote_index(struct remote_distribution *rd, const char *architecture, const char *component, packagetype_t packagetype, /*@null@*/const struct encoding_preferences *downloadas) {
	char *cachefilename, *filename_in_release;

	assert (!rd->flat);
	if (packagetype == pt_deb) {
		filename_in_release = mprintf(
"%s/binary-%s/Packages",
				component, architecture);
		cachefilename = genlistsfilename("Packages", 4,
				rd->repository->name, rd->suite,
				component, architecture, ENDOFARGUMENTS);
	} else if (packagetype == pt_udeb) {
		filename_in_release = mprintf(
"%s/debian-installer/binary-%s/Packages",
				component, architecture);
		cachefilename = genlistsfilename("uPackages", 4,
				rd->repository->name, rd->suite,
				component, architecture, ENDOFARGUMENTS);
	} else if (packagetype == pt_dsc) {
		filename_in_release = mprintf(
"%s/source/Sources",
				component);
		cachefilename = genlistsfilename("Sources", 3,
				rd->repository->name, rd->suite,
				component, ENDOFARGUMENTS);
	} else {
		assert ("Unexpected package type" == NULL);
	}
	return addindex(rd, cachefilename, filename_in_release, downloadas);
}

void cachedlistfile_need_index(struct cachedlistfile *list, const char *repository, const char *suite, const char *architecture, const char *component, packagetype_t packagetype) {
	if (packagetype == pt_deb) {
		cachedlistfile_need(list, "Packages", 4,
				repository, suite,
				component, architecture, ENDOFARGUMENTS);
	} else if (packagetype == pt_udeb) {
		cachedlistfile_need(list, "uPackages", 4,
				repository, suite,
				component, architecture, ENDOFARGUMENTS);
	} else if (packagetype == pt_dsc) {
		cachedlistfile_need(list, "Sources", 3,
				repository, suite,
				component, ENDOFARGUMENTS);
	}
}

struct remote_index *remote_flat_index(struct remote_distribution *rd, packagetype_t packagetype, /*@null@*/const struct encoding_preferences *downloadas) {
	char *cachefilename, *filename_in_release;

	assert (rd->flat);
	if (packagetype == pt_deb) {
		filename_in_release = strdup("Packages");
		cachefilename = genlistsfilename("Packages", 2,
				rd->repository->name, rd->suite,
				ENDOFARGUMENTS);
	} else if (packagetype == pt_dsc) {
		filename_in_release = strdup("Sources");
		cachefilename = genlistsfilename("Sources", 2,
				rd->repository->name, rd->suite,
				ENDOFARGUMENTS);
	} else {
		assert ("Unexpected package type" == NULL);
	}
	return addindex(rd, cachefilename, filename_in_release, downloadas);
}

void cachedlistfile_need_flat_index(struct cachedlistfile *list, const char *repository, const char *suite, packagetype_t packagetype) {
	if (packagetype == pt_deb) {
		cachedlistfile_need(list, "Packages", 2,
				repository, suite, ENDOFARGUMENTS);
	} else if (packagetype == pt_dsc) {
		cachedlistfile_need(list, "Sources", 1,
				repository, suite, ENDOFARGUMENTS);
	}
}

const char *remote_index_file(const struct remote_index *ri) {
	assert (ri->needed && ri->queued && ri->got);
	return ri->cachefilename;
}
const char *remote_index_basefile(const struct remote_index *ri) {
	assert (ri->needed && ri->queued);
	return ri->cachebasename;
}

struct aptmethod *remote_aptmethod(const struct remote_distribution *rd) {
	return rd->repository->download;
}

void remote_index_markdone(const struct remote_index *ri, struct markdonefile *done) {
	if (ri->ofs[c_none] < 0)
		return;
	markdone_index(done, ri->cachebasename,
			ri->from->remotefiles.checksums[ri->ofs[c_none]]);
}
void remote_index_needed(struct remote_index *ri) {
	ri->needed = true;
}

static retvalue indexfile_mark_got(struct remote_distribution *rd, struct remote_index *ri, /*@null@*/const struct checksums *gotchecksums) {
	struct checksums **checksums_p;

	if (!rd->ignorerelease && ri->ofs[c_none] >= 0) {
		checksums_p = &rd->remotefiles.checksums[ri->ofs[c_none]];
		bool matches, improves;

		// TODO: this no longer calculates all the checksums if
		// the Release does not contain more and the apt method
		// returned not all (but all that are in Release).
		// This will then cause the done file not containing all
		// checksums. (but if the Release not contain them, this
		// does not harm, does it?)

		if (gotchecksums != NULL) {
			matches = checksums_check(*checksums_p, gotchecksums,
					&improves);
			/* that should have been tested earlier */
			assert (matches);
			if (! matches)
				return RET_ERROR_WRONG_MD5;
			if (improves) {
				retvalue r;

				r = checksums_combine(checksums_p,
						gotchecksums, NULL);
				if (RET_WAS_ERROR(r))
					return r;
			}
		}
	}
	ri->got = true;
	return RET_OK;
}

static retvalue indexfile_unpacked(void *privdata, const char *compressed, bool failed) {
	struct remote_index *ri = privdata;
	struct remote_distribution *rd = ri->from;
	retvalue r;
	struct checksums *readchecksums = NULL;

	if (failed) {
		// TODO: check if alternative can be used...
		return RET_ERROR;
	}

	/* file got uncompressed, check if it has the correct checksum */

	/* even with a Release file, an old-style one might
	 * not list the checksums for the uncompressed indices */
	if (!rd->ignorerelease && ri->ofs[c_none] >= 0) {
		int ofs = ri->ofs[c_none];
		const struct checksums *wantedchecksums =
			rd->remotefiles.checksums[ofs];
		bool matches, missing = false;

		r = checksums_read(ri->cachefilename, &readchecksums);
		if (r == RET_NOTHING) {
			fprintf(stderr,
"Cannot open '%s', though it should just have been unpacked from '%s'!\n",
					ri->cachefilename,
					compressed);
			r = RET_ERROR_MISSING;
		}
		if (RET_WAS_ERROR(r))
			return r;
		missing = false;
		matches = checksums_check(readchecksums,
				wantedchecksums, &missing);
		assert (!missing);
		if (!matches) {
			fprintf(stderr,
"Wrong checksum of uncompressed content of '%s':\n", compressed);
			checksums_printdifferences(stderr,
					wantedchecksums,
					readchecksums);
			checksums_free(readchecksums);
			return RET_ERROR_WRONG_MD5;
		}
		/* if the compressed file was downloaded or copied, delete it.
		 * This is only done if we know the uncompressed checksum, so
		 * that less downloading is needed (though as apt no longer
		 * supports such archieves, they are unlikely anyway). */

		if (strncmp(ri->cachefilename, compressed,
					strlen(ri->cachefilename)) == 0) {
			(void)unlink(compressed);
		}
	}
	r = indexfile_mark_got(rd, ri, readchecksums);
	checksums_free(readchecksums);
	if (RET_WAS_ERROR(r))
		return r;
	return RET_OK;
}

/* *checksums_p must be either NULL or gotchecksums list all known checksums */
static inline retvalue check_checksums(const char *methodname, const char *uri, const char *gotfilename, const struct checksums *wantedchecksums, /*@null@*/const struct checksums *gotchecksums, struct checksums **checksums_p) {
	bool matches, missing = false;
	struct checksums *readchecksums = NULL;
	retvalue r;

	if (gotchecksums == NULL) {
		matches = true;
		missing = true;
	} else
		matches = checksums_check(gotchecksums,
				wantedchecksums, &missing);
	/* if the apt method did not generate all checksums
	 * we want to check, we'll have to do so: */
	if (matches && missing) {
		/* we assume that everything we know how to
		 * extract from a Release file is something
		 * we know how to calculate out of a file */
		assert (checksums_p == NULL || *checksums_p == NULL);
		r = checksums_read(gotfilename, &readchecksums);
		if (r == RET_NOTHING) {
			fprintf(stderr,
"Cannot open '%s', though apt-method '%s' claims it is there!\n",
					gotfilename, methodname);
			r = RET_ERROR_MISSING;
		}
		if (RET_WAS_ERROR(r))
			return r;
		gotchecksums = readchecksums;
		missing = false;
		matches = checksums_check(gotchecksums,
				wantedchecksums, &missing);
		assert (!missing);
	}
	if (!matches) {
		fprintf(stderr, "Wrong checksum during receive of '%s':\n",
				uri);
		checksums_printdifferences(stderr,
				wantedchecksums,
				gotchecksums);
		checksums_free(readchecksums);
		return RET_ERROR_WRONG_MD5;
	}
	if (checksums_p == NULL)
		checksums_free(readchecksums);
	else if (readchecksums != NULL)
		*checksums_p = readchecksums;
	return RET_OK;
}

static retvalue index_callback(enum queue_action action, void *privdata, UNUSED(void *privdata2), const char *uri, const char *gotfilename, const char *wantedfilename, /*@null@*/const struct checksums *gotchecksums, const char *methodname) {
	struct remote_index *ri = privdata;
	struct remote_distribution *rd = ri->from;
	struct checksums *readchecksums = NULL;
	retvalue r;

	if (action == qa_error)
		return queue_next_encoding(rd, ri);
	if (action != qa_got)
		return RET_ERROR;

	if (ri->compression == c_none) {
		assert (strcmp(wantedfilename, ri->cachefilename) == 0);
		r = copytoplace(gotfilename, wantedfilename, methodname,
				&readchecksums);
		if (RET_WAS_ERROR(r))
			return r;
		gotfilename = wantedfilename;
		if (readchecksums != NULL)
			gotchecksums = readchecksums;
	}

	if (!rd->ignorerelease && ri->ofs[ri->compression] >= 0) {
		int ofs = ri->ofs[ri->compression];
		const struct checksums *wantedchecksums =
			rd->remotefiles.checksums[ofs];

		r = check_checksums(methodname, uri, gotfilename,
				wantedchecksums, gotchecksums, &readchecksums);
		if (RET_WAS_ERROR(r)) {
			checksums_free(readchecksums);
			return r;
		}
		if (readchecksums != NULL)
			gotchecksums = readchecksums;
	}

	if (ri->compression == c_none) {
		assert (strcmp(gotfilename, wantedfilename) == 0);
		r = indexfile_mark_got(rd, ri, gotchecksums);
		checksums_free(readchecksums);
		if (RET_WAS_ERROR(r))
			return r;
		return RET_OK;
	} else {
		checksums_free(readchecksums);
		r = remove_old_uncompressed(ri);
		if (RET_WAS_ERROR(r))
			return r;
		r = uncompress_queue_file(gotfilename, ri->cachefilename,
				ri->compression,
				indexfile_unpacked, privdata);
		if (RET_WAS_ERROR(r))
			return r;
		return RET_OK;
	}
}

static queue_callback diff_got_callback;

static retvalue queue_next_diff(struct remote_index *ri) {
	struct remote_distribution *rd = ri->from;
	struct remote_repository *rr = rd->repository;
	int i;
	retvalue r;

	for (i = 0 ; i < ri->diffindex->patchcount ; i++) {
		bool improves;
		struct diffindex_patch *p = &ri->diffindex->patches[i];
		char *patchsuffix, *c;

		if (p->done || p->frompackages == NULL)
			continue;

		if (!checksums_check(ri->oldchecksums, p->frompackages,
					&improves))
			continue;
		/* p->frompackages should only have sha1 and oldchecksums
		 * should definitly list a sha1 hash */
		assert (!improves);

		p->done = true;

		free(ri->patchfilename);
		ri->patchfilename = mprintf("%s.diff-%s", ri->cachefilename,
				p->name);
		if (FAILEDTOALLOC(ri->patchfilename))
			return RET_ERROR_OOM;
		c = ri->patchfilename + strlen(ri->cachefilename);
		while (*c != '\0') {
			if ((*c < '0' || *c > '9')
					&& (*c < 'A' || *c > 'Z')
					&& (*c < 'a' || *c > 'z')
					&& *c != '.' && *c != '-')
				*c = '_';
			c++;
		}
		ri->selectedpatch = p;
		patchsuffix = mprintf(".diff/%s.gz", p->name);
		if (FAILEDTOALLOC(patchsuffix))
			return RET_ERROR_OOM;

		/* found a matching patch, tell the downloader we want it */
		r = aptmethod_enqueueindex(rr->download, rd->suite_base_dir,
				ri->filename_in_release,
				patchsuffix,
				ri->patchfilename, ".gz",
				diff_got_callback, ri, p);
		free(patchsuffix);
		return r;
	}
	/* no patch matches, try next possibility... */
	fprintf(stderr, "Error: available '%s' not listed in '%s.diffindex'.\n",
			ri->cachefilename, ri->cachefilename);
	return queue_next_encoding(rd, ri);
}

static retvalue diff_uncompressed(void *privdata, const char *compressed, bool failed) {
	struct remote_index *ri = privdata;
	struct remote_distribution *rd = ri->from;
	const struct diffindex_patch *p = ri->selectedpatch;
	char *tempfilename;
	struct rred_patch *rp;
	FILE *f;
	int i;
	retvalue r;
	bool dummy;

	if (ri->deletecompressedpatch)
		(void)unlink(compressed);
	if (failed)
		return RET_ERROR;

	r = checksums_test(ri->patchfilename, p->checksums, NULL);
	if (r == RET_NOTHING) {
		fprintf(stderr, "Mysteriously vanished file '%s'!\n",
				ri->patchfilename);
		r = RET_ERROR_MISSING;
	}
	if (r == RET_ERROR_WRONG_MD5)
		fprintf(stderr, "Corrupted package diff '%s'!\n",
				ri->patchfilename);
	if (RET_WAS_ERROR(r))
		return r;

	r = patch_load(ri->patchfilename,
			checksums_getfilesize(p->checksums), &rp);
	ASSERT_NOT_NOTHING(r);
	if (RET_WAS_ERROR(r))
		return r;

	tempfilename = calc_addsuffix(ri->cachefilename, "tmp");
	if (FAILEDTOALLOC(tempfilename)) {
		patch_free(rp);
		return RET_ERROR_OOM;
	}
	(void)unlink(tempfilename);
	i = rename(ri->cachefilename, tempfilename);
	if (i != 0) {
		int e = errno;
		fprintf(stderr, "Error %d moving '%s' to '%s': %s\n",
				e, ri->cachefilename, tempfilename,
				strerror(e));
		free(tempfilename);
		patch_free(rp);
		return RET_ERRNO(e);
	}
	f = fopen(ri->cachefilename, "w");
	if (f == NULL) {
		int e = errno;
		fprintf(stderr, "Error %d creating '%s': %s\n",
				e, ri->cachefilename, strerror(e));
		(void)unlink(tempfilename);
		ri->olduncompressed->deleted = true;
		ri->olduncompressed = NULL;
		free(tempfilename);
		patch_free(rp);
		return RET_ERRNO(e);
	}
	r = patch_file(f, tempfilename, patch_getconstmodifications(rp));
	(void)unlink(tempfilename);
	(void)unlink(ri->patchfilename);
	free(ri->patchfilename);
	ri->patchfilename = NULL;
	free(tempfilename);
	patch_free(rp);
	if (RET_WAS_ERROR(r)) {
		(void)fclose(f);
		remove_old_uncompressed(ri);
		// TODO: fall back to downloading at once?
		return r;
	}
	i = ferror(f);
	if (i != 0) {
		int e = errno;
		(void)fclose(f);
		fprintf(stderr, "Error %d writing to '%s': %s\n",
				e, ri->cachefilename, strerror(e));
		remove_old_uncompressed(ri);
		return RET_ERRNO(e);
	}
	i = fclose(f);
	if (i != 0) {
		int e = errno;
		fprintf(stderr, "Error %d writing to '%s': %s\n",
				e, ri->cachefilename, strerror(e));
		remove_old_uncompressed(ri);
		return RET_ERRNO(e);
	}
	checksums_free(ri->oldchecksums);
	ri->oldchecksums = NULL;
	r = checksums_read(ri->cachefilename, &ri->oldchecksums);
	if (r == RET_NOTHING) {
		fprintf(stderr, "Myteriously vanished file '%s'!\n",
				ri->cachefilename);
		r = RET_ERROR;
	}
	if (RET_WAS_ERROR(r))
		return r;
	if (checksums_check(ri->oldchecksums,
				rd->remotefiles.checksums[ri->ofs[c_none]],
				&dummy)) {
		ri->olduncompressed->deleted = true;
		ri->olduncompressed = NULL;
		/* we have a winner */
		return indexfile_mark_got(rd, ri, ri->oldchecksums);
	}
	/* let's see what patch we need next */
	return queue_next_diff(ri);
}

static retvalue diff_got_callback(enum queue_action action, void *privdata, UNUSED(void *privdata2), UNUSED(const char *uri), const char *gotfilename, const char *wantedfilename, UNUSED(/*@null@*/const struct checksums *gotchecksums), UNUSED(const char *methodname)) {
	struct remote_index *ri = privdata;
	retvalue r;

	if (action == qa_error)
		return queue_next_encoding(ri->from, ri);
	if (action != qa_got)
		return RET_ERROR;

	ri->deletecompressedpatch = strcmp(gotfilename, wantedfilename) == 0;
	r = uncompress_queue_file(gotfilename, ri->patchfilename,
			c_gzip, diff_uncompressed, ri);
	if (RET_WAS_ERROR(r))
		(void)unlink(gotfilename);
	return r;
}

static retvalue diff_callback(enum queue_action action, void *privdata, UNUSED(void *privdata2), const char *uri, const char *gotfilename, const char *wantedfilename, /*@null@*/const struct checksums *gotchecksums, const char *methodname) {
	struct remote_index *ri = privdata;
	struct remote_distribution *rd = ri->from;
	struct checksums *readchecksums = NULL;
	int ofs;
	retvalue r;

	if (action == qa_error)
		return queue_next_encoding(rd, ri);
	if (action != qa_got)
		return RET_ERROR;

	r = copytoplace(gotfilename, wantedfilename, methodname,
			&readchecksums);
	if (RET_WAS_ERROR(r))
		return r;
	if (readchecksums != NULL)
		gotchecksums = readchecksums;

	ofs = ri->diff_ofs;
	if (ofs >= 0) {
		const struct checksums *wantedchecksums =
			rd->remotefiles.checksums[ofs];
		bool matches, missing = false;

		if (gotchecksums == NULL) {
			matches = true;
			missing = true;
		} else
			matches = checksums_check(gotchecksums,
					wantedchecksums, &missing);
		/* if the apt method did not generate all checksums
		 * we want to check, we'll have to do so: */
		if (matches && missing) {
			/* we assume that everything we know how to
			 * extract from a Release file is something
			 * we know how to calculate out of a file */
			assert (readchecksums == NULL);
			r = checksums_read(gotfilename, &readchecksums);
			if (r == RET_NOTHING) {
				fprintf(stderr,
"Cannot open '%s', though apt-method '%s' claims it is there!\n",
					gotfilename, methodname);
				r = RET_ERROR_MISSING;
			}
			if (RET_WAS_ERROR(r))
				return r;
			gotchecksums = readchecksums;
			missing = false;
			matches = checksums_check(gotchecksums,
					wantedchecksums, &missing);
			assert (!missing);
		}
		if (!matches) {
			fprintf(stderr,
"Wrong checksum during receive of '%s':\n", uri);
			checksums_printdifferences(stderr,
					wantedchecksums, gotchecksums);
			checksums_free(readchecksums);
			return RET_ERROR_WRONG_MD5;
		}
	}
	checksums_free(readchecksums);
	r = diffindex_read(wantedfilename, &ri->diffindex);
	ASSERT_NOT_NOTHING(r);
	if (RET_WAS_ERROR(r))
		return queue_next_encoding(rd, ri);
	if (ri->ofs[c_none] >= 0) {
		bool dummy;
		if (!checksums_check(rd->remotefiles.checksums[
				ri->ofs[c_none]],
				ri->diffindex->destination, &dummy)) {
			fprintf(stderr,
"'%s' does not match file requested in '%s'. Aborting diff processing...\n",
					gotfilename, rd->usedreleasefile);
			/* as this is claimed to be a common error
			 * (outdated .diff/Index file), proceed with
			 * other requested way to retrieve index file */
			return queue_next_encoding(rd, ri);
		}
	}
	return queue_next_diff(ri);
}
