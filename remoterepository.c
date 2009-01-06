/*  This file is part of "reprepro"
 *  Copyright (C) 2008 Bernhard R. Link
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
#include "names.h"
#include "aptmethod.h"
#include "signature.h"
#include "readrelease.h"
#include "uncompression.h"
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

	/* list of key descriptions to check against, each must match */
	struct strlist verify;

	/* local copy of Release and Release.gpg file, once and if available */
	char *releasefile;
	char *releasegpgfile;

	/* filenames and checksums from the Release file */
	struct checksumsarray remotefiles;

	/* the index files we need */
	struct remote_index *indices;
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

	/* the choosen file */
	/* - the compression used */
	enum compression compression;

	bool queued;
	bool needed;
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
	if( i == NULL )
		return;
	free(i->cachefilename);
	free(i->filename_in_release);
	free(i);
}

static void remote_distribution_free(/*@only@*/struct remote_distribution *d) {
	if( d == NULL )
		return;
	free(d->suite);
	strlist_done(&d->verify);
	free(d->releasefile);
	free(d->releasegpgfile);
	free(d->suite_base_dir);
	checksumsarray_done(&d->remotefiles);
	while( d->indices != NULL ) {
		struct remote_index *h = d->indices;
		d->indices = h->next;
		remote_index_free(h);
	}
	free(d);
}

void remote_repository_free(struct remote_repository *remote) {
	if( remote == NULL )
		return;
	while( remote->distributions != NULL ) {
		struct remote_distribution *h = remote->distributions;
		remote->distributions = h->next;
		remote_distribution_free(h);
	}
	if( remote->next != NULL )
		remote->next->prev = remote->prev;
	if( remote->prev != NULL )
		remote->prev->next = remote->next;
	free(remote);
	return;
}

void cachedlistfile_freelist(struct cachedlistfile *c) {
	while( c != NULL ) {
		struct cachedlistfile *n = c->next;
		free(c);
		c = n;
	}
}

void cachedlistfile_deleteunneeded(const struct cachedlistfile *c) {
	for( ; c != NULL ; c = c->next ) {
		if( c->needed )
			continue;
		if( verbose >= 0 )
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
	if( FAILEDTOALLOC(c) )
		return NULL;
	c->next = NULL;
	c->needed = false;
	c->deleted = false;
	p = c->fullfilename;
	assert( (size_t)(p - (char*)c) <= sizeof(struct cachedlistfile) );
	memcpy(p, global.listdir, listdirlen);
	p += listdirlen;
	*(p++) = '/';
	assert( (size_t)(p - c->fullfilename) == listdirlen + 1 );
	c->basefilename = p;
	memcpy(p, basefilename, len); p += len;
	*(p++) = '\0';
	assert( (size_t)(p - c->fullfilename) == listdirlen + len + 2 );

	c->parts[0] = p;
	c->partcount = 1;
	l = len;
	while( l-- > 0 && (ch = *(basefilename++)) != '\0' ) {
		if( ch == '_' ) {
			*(p++) = '\0';
			if( c->partcount < MAXPARTS )
				c->parts[c->partcount] = p;
			c->partcount++;
		} else if( ch == '%' ) {
			char first, second;

			if( len <= 1 ) {
				c->partcount = 0;
				return c;
			}
			first = *(basefilename++);
			second = *(basefilename++);
			if( first >= '0' && first <= '9' )
				*p = (first - '0') << 4;
			else if( first >= 'a' && first <= 'f' )
				*p = (first - 'a' + 10) << 4;
			else {
				c->partcount = 0;
				return c;
			}
			if( second >= '0' && second <= '9' )
				*p |= (second - '0');
			else if( second >= 'a' && second <= 'f' )
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
	assert( (size_t)(p - c->fullfilename) <= listdirlen + 2*len + 3 );
	return c;
}

retvalue cachedlists_scandir(/*@out@*/struct cachedlistfile **cachedfiles_p) {
	struct cachedlistfile *cachedfiles = NULL, **next_p;
	struct dirent *r;
	size_t listdirlen = strlen(global.listdir);
	DIR *dir;

	// TODO: check if it is always created before...
	dir = opendir(global.listdir);
	if( dir == NULL ) {
		int e = errno;
		fprintf(stderr,"Error %d opening directory '%s': %s!\n",
				e, global.listdir, strerror(e));
		return RET_ERRNO(e);
	}
	next_p = &cachedfiles;
	while( true ) {
		size_t namelen;
		int e;

		errno = 0;
		r = readdir(dir);
		if( r == NULL ) {
			e = errno;
			if( e == 0 )
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
		if( namelen == 1 && r->d_name[0] == '.' )
			continue;
		if( namelen == 2 && r->d_name[0] == '.' && r->d_name[1] == '.' )
			continue;
		*next_p = cachedlistfile_new(r->d_name, namelen, listdirlen);
		if( FAILEDTOALLOC(*next_p) ) {
			(void)closedir(dir);
			cachedlistfile_freelist(cachedfiles);
			return RET_ERROR_OOM;
		}
		next_p = &(*next_p)->next;
	}
	if( closedir(dir) != 0 ) {
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
	if( old->deleted )
		return RET_OK;
	e = deletefile(old->fullfilename);
	if( e != 0 )
		return RET_ERRNO(e);
	old->deleted = true;
	return RET_OK;
}

struct remote_repository *remote_repository_prepare(const char *name, const char *method, const char *fallback, const struct strlist *config) {
	struct remote_repository *n;

	/* calling code ensures no two with the same name are created,
	 * so just create it... */

	n = calloc(1, sizeof(struct remote_repository));
	if( FAILEDTOALLOC(n) )
		return NULL;
	n->name = name;
	n->method = method;
	n->fallback = fallback;
	n->config = config;

	n->next = repositories;
	if( n->next != NULL )
		n->next->prev = n;
	repositories = n;

	return n;
}

/* This escaping is quite harsh, but so nothing bad can happen... */
static inline size_t escapedlen(const char *p) {
	size_t l = 0;
	if( *p == '-' ) {
		l = 3;
		p++;
	}
	while( *p != '\0' ) {
		if( (*p < 'A' || *p > 'Z' ) && (*p < 'a' || *p > 'z' ) &&
		    ( *p < '0' || *p > '9') && *p != '-' )
			l +=3;
		else
			l++;
		p++;
	}
	return l;
}

static inline char *escapedcopy(char *dest, const char *orig) {
	static char hex[16] = "0123456789ABCDEF";
	if( *orig == '-' ) {
		orig++;
		*dest = '%'; dest++;
		*dest = '2'; dest++;
		*dest = 'D'; dest++;
	}
	while( *orig != '\0' ) {
		if( (*orig < 'A' || *orig > 'Z' ) && (*orig < 'a' || *orig > 'z' ) && ( *orig < '0' || *orig > '9') && *orig != '-' ) {
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
	for( i = 0 ; i < count ; i++ ) {
		fields[i] = va_arg(ap, const char*);
		assert( fields[i] != NULL );
		len += escapedlen(fields[i]) + 1;
	}
	/* check sentinel */
	assert( va_arg(ap, const char*) == NULL );
	va_end(ap);
	listdir_len = strlen(global.listdir);
	if( type != NULL )
		type_len = strlen(type);
	else
		type_len = 0;

	result = malloc(listdir_len + type_len + len + 2);
	if( FAILEDTOALLOC(result) )
		return NULL;
	memcpy(result, global.listdir, listdir_len);
	p = result + listdir_len;
	*(p++) = '/';
	for( i = 0 ; i < count ; i++ ) {
		p = escapedcopy(p, fields[i]);
		*(p++) = '_';
	}
	assert( (size_t)(p - result) == listdir_len + len + 1);
	if( type != NULL )
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
	for( i = 0 ; i < count ; i++ ) {
		fields[i] = va_arg(ap, const char*);
		assert( fields[i] != NULL );
	}
	/* check sentinel */
	assert( va_arg(ap, const char*) == NULL );
	va_end(ap);

	for( file = list ; file != NULL ; file = file->next ) {
		if( file->partcount != count + 1 )
			continue;
		i = 0;
		while( i < count && strcmp(file->parts[i], fields[i]) == 0 )
			i++;
		if( i < count )
			continue;
		if( strcmp(type, file->parts[i]) != 0 )
			continue;
		file->needed = true;
	}
}

struct remote_distribution *remote_distribution_prepare(struct remote_repository *repository, const char *suite, bool ignorerelease, const char *verifyrelease, bool flat, bool *ignorehashes) {
	struct remote_distribution *n, **last;
	enum checksumtype cs;

	for( last = &repository->distributions ; (n = *last) != NULL
	                                       ; last = &n->next) {
		if( strcmp(n->suite, suite) != 0 )
			continue;
		if( n->flat != flat ) {
			if( verbose >= 0 && !n->flatnonflatwarned &&
					!IGNORABLE(flatandnonflat) )
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

	if( *last != NULL ) {
		n = *last;
		assert( n->flat == flat );

		if( (n->ignorerelease && !ignorerelease) ||
		    (!n->ignorerelease && ignorerelease) ) {
			// TODO a hint which two are at fault would be nice,
			// but how to get the information...
			if( verbose >= 0 )
				fprintf(stderr,
"Warning: I was told to both ignore Release files for Suite '%s'\n"
"from remote repository '%s' and to not ignore it. Going to not ignore!\n",
						suite, repository->name);
			n->ignorerelease = false;
		}
		for( cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++ ) {
			if( (n->ignorehashes[cs] && !ignorehashes[cs]) ||
			    (!n->ignorehashes[cs] && ignorehashes[cs]) ) {
				// TODO dito
				if( verbose >= 0 )
					fprintf(stderr,
"Warning: I was told to both ignore '%s' for Suite '%s'\n"
"from remote repository '%s' and to not ignore it. Going to not ignore!\n",
						suite,
						release_checksum_names[cs],
						repository->name);
				n->ignorehashes[cs] = false;
			}
		}
		if( verifyrelease != NULL
				&& strcmp(verifyrelease, "blindtrust") != 0
				&& !strlist_in(&n->verify, verifyrelease) ) {
			retvalue r;

			r = strlist_add_dup(&n->verify, verifyrelease);
			if( RET_WAS_ERROR(r) )
				return NULL;
		}
		return n;
	}

	n = calloc(1, sizeof(struct remote_distribution));
	if( FAILEDTOALLOC(n) )
		return NULL;
	n->repository = repository;
	n->suite = strdup(suite);
	n->ignorerelease = ignorerelease;
	if( verifyrelease != NULL && strcmp(verifyrelease, "blindtrust") != 0 ) {
		retvalue r;

		r = strlist_add_dup(&n->verify, verifyrelease);
		if( RET_WAS_ERROR(r) ) {
			remote_distribution_free(n);
			return NULL;
		}
	}
	memcpy(n->ignorehashes, ignorehashes, sizeof(bool [cs_hashCOUNT]));
	n->flat = flat;
	if( flat )
		n->suite_base_dir = strdup(suite);
	else
		n->suite_base_dir = calc_dirconcat("dists", suite);
	if( FAILEDTOALLOC(n->suite) ||
			FAILEDTOALLOC(n->suite_base_dir) ) {
		remote_distribution_free(n);
		return NULL;
	}
	/* ignorerelease can be unset later, so always calculate the filename */
	if( flat )
		n->releasefile = genlistsfilename("Release", 3,
				repository->name, suite, "flat",
				ENDOFARGUMENTS);
	else
		n->releasefile = genlistsfilename("Release", 2,
				repository->name, suite, ENDOFARGUMENTS);
	if( FAILEDTOALLOC(n->releasefile) ) {
		remote_distribution_free(n);
		return NULL;
	}
	n->releasegpgfile = calc_addsuffix(n->releasefile, "gpg");
	if( FAILEDTOALLOC(n->releasefile) ) {
		remote_distribution_free(n);
		return NULL;
	}
	*last = n;
	return n;
}

static retvalue remote_distribution_metalistqueue(struct remote_distribution *d) {
	struct remote_repository *repository = d->repository;
	retvalue r;

	assert( repository->download != NULL );

	if( d->ignorerelease )
		return RET_NOTHING;

	(void)unlink(d->releasefile);
	r = aptmethod_queueindexfile(repository->download, d->suite_base_dir,
			"Release", d->releasefile, NULL, c_none, NULL);
	if( RET_WAS_ERROR(r) )
		return r;

	if( d->verify.count != 0 ) {
		(void)unlink(d->releasegpgfile);
		r = aptmethod_queueindexfile(repository->download,
				d->suite_base_dir, "Release.gpg",
				d->releasegpgfile, NULL, c_none, NULL);
		if( RET_WAS_ERROR(r) )
			return r;
	}
	return RET_OK;
}

retvalue remote_startup(struct aptmethodrun *run) {
	struct remote_repository *rr;
	retvalue r;

	if( interrupted() )
		return RET_ERROR_INTERRUPTED;

	for( rr = repositories ; rr != NULL ; rr = rr->next ) {
		assert( rr->download == NULL );

		r = aptmethod_newmethod(run,
				rr->method, rr->fallback,
				rr->config, &rr->download);
		if( RET_WAS_ERROR(r) )
			return r;
	}
	return RET_OK;
}

static void find_index(const struct strlist *files, struct remote_index *ri) {
	const char *filename = ri->filename_in_release;
	size_t len = strlen(filename);
	int i;
	enum compression c;

	for( i = 0 ; i < files->count ; i++ ) {
		const char *value = files->values[i];

		if( strncmp(value, filename, len) != 0 )
			continue;

		value += len;

		if( *value == '\0' ) {
			ri->ofs[c_none] = i;
			continue;
		}
		if( *value != '.' )
			continue;
		if( strcmp(value, ".diff/Index") == 0 ) {
			ri->diff_ofs = i;
			continue;
		}

		for( c = 0 ; c < c_COUNT ; c++ )
			if( strcmp(value, uncompression_suffix[c]) == 0 ) {
				ri->ofs[c] = i;
				break;
			}
	}
}

static retvalue process_remoterelease(struct remote_distribution *rd) {
	struct remote_repository *rr = rd->repository;
	struct remote_index *ri;
	retvalue r;

	if( rd->verify.count != 0 ) {
		r = signature_check(&rd->verify,
				rd->releasegpgfile,
				rd->releasefile);
		assert( r != RET_NOTHING );
		if( r == RET_NOTHING )
			r = RET_ERROR_BADSIG;
		if( r == RET_ERROR_BADSIG ) {
			fprintf(stderr,
"Error: Not enough signatures found for remote repository %s (%s %s)!\n",
					rr->name, rr->method, rd->suite);
			r = RET_ERROR_BADSIG;
		}
		if( RET_WAS_ERROR(r) )
			return r;
	}
	r = release_getchecksums(rd->releasefile, rd->ignorehashes,
			&rd->remotefiles);
	if( RET_WAS_ERROR(r) )
		return r;

	/* Check for our files in there */
	for( ri = rd->indices ; ri != NULL ; ri = ri->next ) {
		find_index(&rd->remotefiles.names, ri);
	}
	// TODO: move checking if not exists at all to here?
	return RET_OK;
}

retvalue remote_preparemetalists(struct aptmethodrun *run, bool nodownload) {
	struct remote_repository *rr;
	struct remote_distribution *rd;
	retvalue r;
	bool tobecontinued;

	if( !nodownload ) {
		for( rr = repositories ; rr != NULL ; rr = rr->next ) {
			for( rd = rr->distributions ; rd != NULL ;
			                              rd = rd->next ) {
				r = remote_distribution_metalistqueue(rd);
				if( RET_WAS_ERROR(r) )
					return r;
			}
		}
		r = aptmethod_download(run, NULL);
		if( RET_WAS_ERROR(r) )
			return r;
	}

	tobecontinued = false;
	for( rr = repositories ; rr != NULL ; rr = rr->next ) {
		for( rd = rr->distributions ; rd != NULL ; rd = rd->next ) {
			if( !rd->ignorerelease ) {
				r = process_remoterelease(rd);
				if( RET_WAS_ERROR(r) )
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
	if( ri->ofs[c_none] < 0 )
		return true;
	/* if not there or the wrong files comes next, then something
	 * has changed and we better reload everything */
	if( !donefile_nextindex(done, &basefilename, &checksums) )
		return true;
	if( strcmp(basefilename, ri->cachebasename) != 0 ) {
		checksums_free(checksums);
		return true;
	}
	/* otherwise check if the file checksums match */
	if( !checksums_check(checksums,
			ri->from->remotefiles.checksums[ri->ofs[c_none]],
			&hashes_missing) ) {
		checksums_free(checksums);
		return true;
	}
	if( hashes_missing ) {
		/* if Release has checksums we do not yet know about,
		 * process it to make sure those match as well */
		checksums_free(checksums);
		return true;
	}
	if( !checksums_check(ri->from->remotefiles.checksums[ri->ofs[c_none]],
				checksums, &improves) ) {
		/* this should not happen, but ... */
		checksums_free(checksums);
		return true;
	}
	if( improves ) {
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

static inline void remote_index_oldfiles(struct remote_index *ri, /*@null@*/struct cachedlistfile *oldfiles, /*@ouz@*/struct cachedlistfile *old[c_COUNT]) {
	struct cachedlistfile *o;
	size_t l;
	enum compression c;

	for( c = 0 ; c < c_COUNT ; c++ )
		old[c] = NULL;

	l = strlen(ri->cachebasename);
	for( o = oldfiles ; o != NULL ; o = o->next ) {
		if( o->deleted )
			continue;
		if( strncmp(o->basefilename, ri->cachebasename, l) != 0 )
			continue;
		for( c = 0 ; c < c_COUNT ; c++ )
			if( strcmp(o->basefilename + l,
			           uncompression_suffix[c]) == 0 ) {
				old[c] = o;
				o->needed = true;
				break;
			}
	}
}

static inline enum compression firstsupportedencoding(const struct encoding_preferences *downloadas) {
	int e;

	if( downloadas->count == 0 )
		/* if nothing is specified, get .gz */
		return c_gzip;

	for( e = 0 ; e < downloadas->count ; e++ ) {
		enum compression c = downloadas->requested[e];
		if( uncompression_supported(c) )
			return c;
	}
	// TODO: instead give an good warning or error...
	return c_gzip;
}

static inline retvalue find_requested_encoding(struct remote_index *ri, const char *releasefile) {
	int e;
	enum compression c,
			 /* the most-preferred requested but unsupported */
			 unsupported = c_COUNT,
			 /* the best unrequested but supported */
			 unrequested = c_COUNT;

	if( ri->downloadas.count > 0 ) {
		bool found = false;
		for( e = 0 ; e < ri->downloadas.count ; e++ ) {
			c = ri->downloadas.requested[e];

			if( ri->ofs[c] < 0 )
				continue;
			if( uncompression_supported(c) ) {
				ri->compression = c;
				return RET_OK;
			} else if( unsupported == c_COUNT )
				unsupported = c;
		}

		/* nothing that is both requested by the user and supported
		 * and listed in the Release file found, check what is there
		 * to get a meaningfull error message */

		for( c = 0 ; c < c_COUNT ; c++ ) {
			if( ri->ofs[c] < 0 )
				continue;
			found = true;
			if( uncompression_supported(c) )
				unrequested = c;
		}

		if( !found ) {
			// TODO: might be nice to check for not-yet-even
			// known about compressions and say they are not
			// yet know yet instead then here...
			fprintf(stderr,
					"Could not find '%s' within '%s'\n",
					ri->filename_in_release, releasefile);
			return RET_ERROR_WRONG_MD5;
		}

		if( !found ) {
			fprintf(stderr,
"Error: '%s' only lists unusable or unrequested compressions of '%s'.\n"
"Try e.g the '%s' option (or check what it is set to) to make more useable.\n"
"Or change your DownloadListsAs to request e.g. '%s'.\n",
					releasefile, ri->filename_in_release,
					uncompression_option[unsupported],
					uncompression_suffix[unrequested]);
			return RET_ERROR;
		}
		if( unsupported != c_COUNT ) {
			fprintf(stderr,
"Error: '%s' only lists unusable compressions of '%s'.\n"
"Try e.g the '%s' option (or check what it is set to) to make more useable.\n",
					releasefile, ri->filename_in_release,
					uncompression_option[unsupported]);
			return RET_ERROR;
		}
		if( unrequested != c_COUNT ) {
			fprintf(stderr,
"Error: '%s' only lists unrequested compressions of '%s'.\n"
"Try changing your DownloadListsAs to request e.g. '%s'.\n",
					releasefile, ri->filename_in_release,
					uncompression_suffix[unrequested]);
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
	ri->compression = c_COUNT;

	for( c = 0 ; c < c_COUNT ; c++ ) {
		if( ri->ofs[c] < 0 )
			continue;
		if( uncompression_supported(c) )
			ri->compression = c;
		else
			unsupported = c;
	}
	if( ri->compression == c_COUNT ) {
		if( unsupported != c_COUNT ) {
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
	return RET_OK;
}

static inline retvalue queueindex(struct remote_distribution *rd, struct remote_index *ri, bool nodownload, /*@null@*/struct cachedlistfile *oldfiles, bool *tobecontinued) {
	struct remote_repository *rr = rd->repository;
	enum compression c;
	retvalue r;
	int ofs;
	struct cachedlistfile *old[c_COUNT];

	if( rd->ignorerelease ) {
		char *toget;

		ri->queued = true;
		if( nodownload )
			return RET_OK;

		/* Without a Release file there is no knowing what compression
		 * upstream uses. Situation gets worse as we miss the means yet
		 * to try something and continue with something else if it
		 * fails (as aptmethod error reporting would need to be extended
		 * for it). Thus use the first supported of the requested and
		 * use .gz as fallback.
		 *
		 * Using this preferences means users will have to set it for
		 * IgnoreRelease-distributions, that default in an parent
		 * rule to something already, but better than to invent another
		 * mechanism to specify this... */

		c = firstsupportedencoding(&ri->downloadas);
		assert( uncompression_supported(c) );

		ri->compression = c;
		toget = mprintf("%s%s", ri->filename_in_release,
					uncompression_suffix[c]);
		r = aptmethod_queueindexfile(rr->download, rd->suite_base_dir,
				toget, ri->cachefilename, NULL, c, NULL);
		free(toget);
		return r;
	}

	/* check if this file is still available from an earlier download */
	remote_index_oldfiles(ri, oldfiles, old);
	if( old[c_none] != NULL ) {
		if( ri->ofs[c_none] < 0 ) {
			r = cachedlistfile_delete(old[c_none]);
			/* we'll need to download this there,
			 * so errors to remove are fatal */
			if( RET_WAS_ERROR(r) )
				return r;
			old[c_none] = NULL;
			r = RET_NOTHING;
		} else
			r = checksums_test(old[c_none]->fullfilename,
					rd->remotefiles.checksums[ri->ofs[c_none]],
					&rd->remotefiles.checksums[ri->ofs[c_none]]);
		if( RET_IS_OK(r) ) {
			/* already there, nothing to do to get it... */
			ri->queued = true;
			return r;
		}
		if( r == RET_ERROR_WRONG_MD5 ) {
			// TODO: implement diff
			if( 0 )
				*tobecontinued = true;
			r = cachedlistfile_delete(old[c_none]);
			/* we'll need to download this there,
			 * so errors to remove are fatal */
			if( RET_WAS_ERROR(r) )
				return r;
			old[c_none] = NULL;
			r = RET_NOTHING;
		}
		if( RET_WAS_ERROR(r) )
			return r;
	}

	/* make sure everything old is deleted or check if it can be used */
	for( c = 0 ; c < c_COUNT ; c++ ) {
		if( old[c] == NULL )
			continue;
		if( c != c_none && ri->ofs[c] >= 0 ) {
			/* check if it can be used */
			r = checksums_test(old[c]->fullfilename,
					rd->remotefiles.checksums[ri->ofs[c]],
					&rd->remotefiles.checksums[ri->ofs[c]]);
			if( r == RET_ERROR_WRONG_MD5 )
				r = RET_NOTHING;
			if( RET_WAS_ERROR(r) )
				return r;
			if( RET_IS_OK(r) ) {
				r = uncompress_file(old[c]->fullfilename,
						ri->cachefilename,
						c);
				assert( r != RET_NOTHING );
				if( RET_WAS_ERROR(r) )
					return r;
				if( ri->ofs[c_none] >= 0 ) {
					r = checksums_test(ri->cachefilename,
						rd->remotefiles.checksums[ri->ofs[c_none]],
						&rd->remotefiles.checksums[ri->ofs[c_none]]);
					if( r == RET_ERROR_WRONG_MD5 ) {
						fprintf(stderr,
"Error: File '%s' looked correct according to '%s',\n"
"but after unpacking '%s' looks wrong.\n"
"Something is seriously broken!\n",
							old[c]->fullfilename,
							rd->releasefile,
							ri->cachefilename);
					}
					if( r == RET_NOTHING ) {
						fprintf(stderr, "File '%s' mysteriously vanished!\n", ri->cachefilename);
						r = RET_ERROR_MISSING;
					}
					if( RET_WAS_ERROR(r) )
						return r;
				}
				/* already there, nothing to do to get it... */
				ri->queued = true;
				return RET_OK;
			}
		}
		r = cachedlistfile_delete(old[c]);
		if( RET_WAS_ERROR(r) )
		return r;
		old[c] = NULL;
	}

	/* nothing found, we'll have to download: */

	if( nodownload ) {
		fprintf(stderr, "Error: Missing '%s', try without --nolistsdownload to download it!\n",
				ri->cachefilename);
		return RET_ERROR_MISSING;
	}

	r = find_requested_encoding(ri, rd->releasefile);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) )
		return r;

	assert( ri->compression < c_COUNT );
	assert( uncompression_supported(ri->compression) );

	ofs = ri->ofs[ri->compression];
	assert( ofs >= 0 );

/* as those checksums might be overwritten with completed data,
 * this assumes that the uncompressed checksums for one index is never
 * the compressed checksum for another... */

	ri->queued = true;
	return aptmethod_queueindexfile(rr->download, rd->suite_base_dir,
			rd->remotefiles.names.values[ofs],
			ri->cachefilename,
/* not having this defeats the point, but it only hurts when it is missing
 * now but next update it will be there... */
			(ri->ofs[c_none] < 0)?NULL:
			 &rd->remotefiles.checksums[ri->ofs[c_none]],
			ri->compression,
			(ri->compression == c_none)?NULL:
			rd->remotefiles.checksums[ofs]);
}


static retvalue remote_distribution_listqueue(struct remote_distribution *rd, bool nodownload, struct cachedlistfile *oldfiles, bool *tobecontinued) {
	struct remote_index *ri;
	retvalue r;
	/* check what to get for the requested indicies */
	for( ri = rd->indices ; ri != NULL ; ri = ri->next ) {
		if( ri->queued )
			continue;
		if( !ri->needed ) {
			/* if we do not know anything about it, it cannot have got
			 * marked as old or otherwise as unneeded */
			assert( !rd->ignorerelease );
			continue;
		}
		r = queueindex(rd, ri, nodownload, oldfiles, tobecontinued);
		if( RET_WAS_ERROR(r) )
			return r;
	}
	return RET_OK;
}

retvalue remote_preparelists(struct aptmethodrun *run, bool nodownload) {
	struct remote_repository *rr;
	struct remote_distribution *rd;
	retvalue r;
	bool tobecontinued;
	struct cachedlistfile *oldfiles IFSTUPIDCC(=NULL);

	r = cachedlists_scandir(&oldfiles);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING )
		oldfiles = NULL;

	do {
		tobecontinued = false;
		for( rr = repositories ; rr != NULL ; rr = rr->next ) {
			for( rd = rr->distributions ; rd != NULL
			                            ; rd = rd->next ) {
				r = remote_distribution_listqueue(rd,
						nodownload,
						oldfiles, &tobecontinued);
				if( RET_WAS_ERROR(r) ) {
					cachedlistfile_freelist(oldfiles);
					return r;
				}
			}
		}
		r = aptmethod_download(run, NULL);
		if( RET_WAS_ERROR(r) ) {
			cachedlistfile_freelist(oldfiles);
			return r;
		}
	} while( tobecontinued );

	cachedlistfile_freelist(oldfiles);
	return RET_OK;
}

static struct remote_index *addindex(struct remote_distribution *rd, /*@only@*/char *cachefilename, /*@only@*/char *filename, /*@null@*/const struct encoding_preferences *downloadas) {
	struct remote_index *ri, **last;
	enum compression c;
	const char *cachebasename;

	if( FAILEDTOALLOC(cachefilename) || FAILEDTOALLOC(filename) )
		return NULL;

	cachebasename = dirs_basename(cachefilename);
	last = &rd->indices;
	while( *last != NULL && strcmp((*last)->cachebasename, cachebasename) != 0 )
		last = &(*last)->next;
	if( *last != NULL ) {
		free(cachefilename); free(filename);
		return *last;
	}

	ri = calloc(1, sizeof(struct remote_index));
	if( FAILEDTOALLOC(ri) ) {
		free(cachefilename); free(filename);
		return NULL;
	}

	*last = ri;
	ri->from = rd;
	ri->cachefilename = cachefilename;
	ri->cachebasename = cachebasename;
	ri->filename_in_release = filename;
	// TODO: perhaps try to calculate some form of intersections
	// instead of just using the shorter one...
	if( downloadas != NULL && (ri->downloadas.count == 0
			|| ri->downloadas.count > downloadas->count) )
		ri->downloadas = *downloadas;
	for( c = 0 ; c < c_COUNT ; c++ )
		ri->ofs[c] = -1;
	ri->diff_ofs = -1;
	return ri;
}

struct remote_index *remote_index(struct remote_distribution *rd, const char *architecture, const char *component, packagetype_t packagetype, /*@null@*/const struct encoding_preferences *downloadas) {
	char *cachefilename, *filename_in_release;

	assert( !rd->flat );
	if( packagetype == pt_deb ) {
		filename_in_release = mprintf("%s/binary-%s/Packages",
				component, architecture);
		cachefilename = genlistsfilename("Packages", 4,
				rd->repository->name, rd->suite,
				component, architecture, ENDOFARGUMENTS);
	} else if( packagetype == pt_udeb ) {
		filename_in_release = mprintf(
				"%s/debian-installer/binary-%s/Packages",
				component, architecture);
		cachefilename = genlistsfilename("uPackages", 4,
				rd->repository->name, rd->suite,
				component, architecture, ENDOFARGUMENTS);
	} else if( packagetype == pt_dsc ) {
		filename_in_release = mprintf("%s/source/Sources",
				component);
		cachefilename = genlistsfilename("Sources", 3,
				rd->repository->name, rd->suite,
				component, ENDOFARGUMENTS);
	} else {
		assert( "Unexpected package type" == NULL );
	}
	return addindex(rd, cachefilename, filename_in_release, downloadas);
}

void cachedlistfile_need_index(struct cachedlistfile *list, const char *repository, const char *suite, const char *architecture, const char *component, packagetype_t packagetype) {
	if( packagetype == pt_deb ) {
		cachedlistfile_need(list, "Packages", 4,
				repository, suite,
				component, architecture, ENDOFARGUMENTS);
	} else if( packagetype == pt_udeb ) {
		cachedlistfile_need(list, "uPackages", 4,
				repository, suite,
				component, architecture, ENDOFARGUMENTS);
	} else if( packagetype == pt_dsc ) {
		cachedlistfile_need(list, "Sources", 3,
				repository, suite,
				component, ENDOFARGUMENTS);
	}
}

struct remote_index *remote_flat_index(struct remote_distribution *rd, packagetype_t packagetype, /*@null@*/const struct encoding_preferences *downloadas) {
	char *cachefilename, *filename_in_release;

	assert( rd->flat );
	if( packagetype == pt_deb ) {
		filename_in_release = strdup("Packages");
		cachefilename = genlistsfilename("Packages", 2,
				rd->repository->name, rd->suite,
				ENDOFARGUMENTS);
	} else if( packagetype == pt_dsc ) {
		filename_in_release = strdup("Sources");
		cachefilename = genlistsfilename("Sources", 2,
				rd->repository->name, rd->suite,
				ENDOFARGUMENTS);
	} else {
		assert( "Unexpected package type" == NULL );
	}
	return addindex(rd, cachefilename, filename_in_release, downloadas);
}

void cachedlistfile_need_flat_index(struct cachedlistfile *list, const char *repository, const char *suite, packagetype_t packagetype) {
	if( packagetype == pt_deb ) {
		cachedlistfile_need(list, "Packages", 2,
				repository, suite, ENDOFARGUMENTS);
	} else if( packagetype == pt_dsc ) {
		cachedlistfile_need(list, "Sources", 1,
				repository, suite, ENDOFARGUMENTS);
	}
}

const char *remote_index_file(const struct remote_index *ri) {
	assert( ri->needed && ri->queued );
	return ri->cachefilename;
}
const char *remote_index_basefile(const struct remote_index *ri) {
	assert( ri->needed && ri->queued );
	return ri->cachebasename;
}

struct aptmethod *remote_aptmethod(const struct remote_distribution *rd) {
	return rd->repository->download;
}

void remote_index_markdone(const struct remote_index *ri, struct markdonefile *done) {
	if( ri->ofs[c_none] < 0 )
		return;
	markdone_index(done, ri->cachebasename,
			ri->from->remotefiles.checksums[ri->ofs[c_none]]);
}
void remote_index_needed(struct remote_index *ri) {
	ri->needed = true;
}
