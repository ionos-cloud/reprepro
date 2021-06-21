/*  This file is part of "reprepro"
 *  Copyright (C) 2006,2007,2008,2009,2010 Bernhard R. Link
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

#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include "error.h"
#include "filecntl.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"
#include "dirs.h"
#include "checksums.h"
#include "chunks.h"
#include "chunkedit.h"
#include "signature.h"
#include "debfile.h"
#include "sourceextraction.h"
#include "uncompression.h"

/* for compatibility with used code */
int verbose=0;
bool interrupted(void) {
	return false;
}

static void about(bool help) NORETURN;
static void about(bool help) {
	fprintf(help?stdout:stderr,
"changestool: Modify a Debian style .changes file\n"
"Syntax: changestool [--create] <changesfile> <commands>\n"
"Possible commands include:\n"
" verify\n"
" updatechecksums [<files to update>]\n"
" includeallsources [<files to copy from .dsc to .changes>]\n"
" adddeb <.deb filenames>\n"
" adddsc <.dsc filenames>\n"
" addrawfile <filenames>\n"
" add <filenames processed by filename suffix>\n"
" setdistribution <distributions to list>\n"
" dumbremove <filenames>\n"
);
	if (help)
		exit(EXIT_SUCCESS);
	else
		exit(EXIT_FAILURE);
}

struct binaryfile {
	struct binaryfile *next; // in binaries.files list
	struct binary *binary; // parent
	struct fileentry *file;
	char *controlchunk;
	char *name, *version, *architecture;
	char *sourcename, *sourceversion;
	char *maintainer;
	char *section, *priority;
	char *shortdescription;
	bool hasmd5sums;
};

static void binaryfile_free(struct binaryfile *p) {
	if (p == NULL)
		return;

	free(p->controlchunk);
	free(p->name);
	free(p->version);
	free(p->architecture);
	free(p->sourcename);
	free(p->sourceversion);
	free(p->maintainer);
	free(p->section);
	free(p->priority);
	free(p->shortdescription);
	free(p);
}

enum filetype { ft_UNKNOWN,
			ft_TAR, ft_ORIG_TAR, ft_DIFF,
#define ft_MaxInSource ft_DSC-1
			ft_DSC, ft_DEB, ft_UDEB , ft_Count};
#define ft_Max ft_Count-1

static const struct {
	const char *suffix;
	size_t len;
	bool allowcompressed;
} typesuffix[ft_Count] = {
	{ "?", -1, false},
	{ ".tar", 4, true},
	{ ".orig.tar", 9, true},
	{ ".diff", 5, true},
	{ ".dsc", 4, false},
	{ ".deb", 4, false},
	{ ".udeb", 5, false}
};

struct dscfile {
	struct fileentry *file;
	char *name;
	char *version;
	struct strlist binaries;
	char *maintainer;
	char *controlchunk;
	// hard to get:
	char *section, *priority;
	// TODO: check Architectures?
	struct checksumsarray expected;
	struct fileentry **uplink;
	bool parsed, modified;
};

static void dscfile_free(struct dscfile *p) {
	if (p == NULL)
		return;

	free(p->name);
	free(p->version);
	free(p->maintainer);
	free(p->controlchunk);
	free(p->section);
	free(p->priority);
	checksumsarray_done(&p->expected);
	free(p->uplink);
	free(p);
}

struct fileentry {
	struct fileentry *next;
	char *basename; size_t namelen;
	char *fullfilename;
	/* NULL means was not listed there yet: */
	struct checksums *checksumsfromchanges,
			 *realchecksums;
	char *section, *priority;
	enum filetype type;
	enum compression compression;
	/* only if type deb or udeb */
	struct binaryfile *deb;
	/* only if type dsc */
	struct dscfile *dsc;
	int refcount;
};
struct changes;
static struct fileentry *add_fileentry(struct changes *c, const char *basefilename, size_t len, bool source, /*@null@*//*@out@*/size_t *ofs_p);

struct changes {
	/* the filename of the .changes file */
	char *filename;
	/* directory of filename */
	char *basedir;
	/* Contents of the .changes file: */
	char *name;
	char *version;
	char *maintainer;
	char *control;
	struct strlist architectures;
	struct strlist distributions;
	size_t binarycount;
	struct binary {
		char *name;
		char *description;
		struct binaryfile *files;
		bool missedinheader, uncheckable;
	} *binaries;
	struct fileentry *files;
	bool modified;
};

static void fileentry_free(/*@only@*/struct fileentry *f) {
	if (f == NULL)
		return;
	free(f->basename);
	free(f->fullfilename);
	checksums_free(f->checksumsfromchanges);
	checksums_free(f->realchecksums);
	free(f->section);
	free(f->priority);
	if (f->type == ft_DEB || f->type == ft_UDEB) {
		binaryfile_free(f->deb);
	} else if (f->type == ft_DSC) {
		dscfile_free(f->dsc);
	}
	free(f);
}

static void changes_free(struct changes *c) {
	unsigned int i;

	if (c == NULL)
		return;

	free(c->filename);
	free(c->basedir);
	free(c->name);
	free(c->version);
	free(c->maintainer);
	free(c->control);
	strlist_done(&c->architectures);
	strlist_done(&c->distributions);
	for (i = 0 ; i < c->binarycount ; i++) {
		free(c->binaries[i].name);
		free(c->binaries[i].description);
		// .files belongs elsewhere
	}
	free(c->binaries);
	while (c->files) {
		struct fileentry *f = c->files;
		c->files = f->next;
		fileentry_free(f);
	}
	free(c);
}

static struct fileentry **find_fileentry(struct changes *c, const char *basefilename, size_t basenamelen, size_t *ofs_p) {
	struct fileentry **fp = &c->files;
	struct fileentry *f;
	size_t ofs = 0;

	while ((f=*fp) != NULL) {
		if (f->namelen == basenamelen &&
		    strncmp(basefilename, f->basename, basenamelen) == 0) {
			break;
		}
		fp = &f->next;
		ofs++;
	}
	if (ofs_p != NULL)
		*ofs_p = ofs;
	return fp;
}

static struct fileentry *add_fileentry(struct changes *c, const char *basefilename, size_t len, bool source, size_t *ofs_p) {
	size_t ofs = 0;
	struct fileentry **fp = find_fileentry(c, basefilename, len, &ofs);
	struct fileentry *f = *fp;

	if (f == NULL) {
		enum compression;

		f = zNEW(struct fileentry);
		if (FAILEDTOALLOC(f))
			return NULL;
		f->basename = strndup(basefilename, len);
		f->namelen = len;

		if (FAILEDTOALLOC(f->basename)) {
			free(f);
			return NULL;
		}
		*fp = f;

		/* guess compression */
		f->compression = compression_by_suffix(f->basename, &len);

		/* guess type */
		for (f->type = source?ft_MaxInSource:ft_Max ;
				f->type > ft_UNKNOWN ; f->type--) {
			size_t l = typesuffix[f->type].len;

			if (f->compression != c_none &&
					!typesuffix[f->type].allowcompressed)
				continue;
			if (len <= l)
				continue;
			if (strncmp(f->basename + (len-l),
						typesuffix[f->type].suffix,
						l) == 0)
				break;
		}
	}
	if (ofs_p != NULL)
		*ofs_p = ofs;
	return f;
}

static retvalue searchforfile(const char *changesdir, const char *basefilename, /*@null@*/const struct strlist *searchpath, /*@null@*/const char *searchfirstin, char **result) {
	int i; bool found;
	char *fullname;

	if (searchfirstin != NULL) {
		fullname = calc_dirconcat(searchfirstin, basefilename);
		if (FAILEDTOALLOC(fullname))
			return RET_ERROR_OOM;
		if (isregularfile(fullname)) {
			*result = fullname;
			return RET_OK;
		}
		free(fullname);
	}

	fullname = calc_dirconcat(changesdir, basefilename);
	if (FAILEDTOALLOC(fullname))
		return RET_ERROR_OOM;

	found = isregularfile(fullname);
	i = 0;
	while (!found && searchpath != NULL && i < searchpath->count) {
		free(fullname);
		fullname = calc_dirconcat(searchpath->values[i],
				basefilename);
		if (FAILEDTOALLOC(fullname))
			return RET_ERROR_OOM;
		if (isregularfile(fullname)) {
			found = true;
			break;
		}
		i++;
	}
	if (found) {
		*result = fullname;
		return RET_OK;
	} else {
		free(fullname);
		return RET_NOTHING;
	}
}

static retvalue findfile(const char *filename, const struct changes *c, /*@null@*/const struct strlist *searchpath, /*@null@*/const char *searchfirstin, char **result) {
	char *fullfilename;

	if (rindex(filename, '/') == NULL) {
		retvalue r;

		r = searchforfile(c->basedir, filename,
				searchpath, searchfirstin, &fullfilename);
		if (!RET_IS_OK(r))
			return r;
	} else {
		if (!isregularfile(filename))
			return RET_NOTHING;
		fullfilename = strdup(filename);
		if (FAILEDTOALLOC(fullfilename))
			return RET_ERROR_OOM;
	}
	*result = fullfilename;
	return RET_OK;
}

static retvalue add_file(struct changes *c, /*@only@*/char *basefilename, /*@only@*/char *fullfilename, enum filetype type, struct fileentry **file) {
	size_t basenamelen = strlen(basefilename);
	struct fileentry **fp;
	struct fileentry *f;

	fp = find_fileentry(c, basefilename, basenamelen, NULL);
	f = *fp;

	if (f != NULL) {
		*file = f;
		free(basefilename);
		free(fullfilename);
		return RET_NOTHING;
	}
	assert (f == NULL);
	f = zNEW(struct fileentry);
	if (FAILEDTOALLOC(f)) {
		free(basefilename);
		free(fullfilename);
		return RET_ERROR_OOM;
	}
	f->basename = basefilename;
	f->namelen = basenamelen;
	f->fullfilename = fullfilename;
	f->type = type;
	f->compression = c_none;

	*fp = f;
	*file = f;
	return RET_OK;
}


static struct binary *get_binary(struct changes *c, const char *p, size_t len) {
	unsigned int j;

	for (j = 0 ; j < c->binarycount ; j++) {
		if (strncmp(c->binaries[j].name, p, len) == 0 &&
				c->binaries[j].name[len] == '\0')
			break;
	}
	if (j == c->binarycount) {
		char *name = strndup(p, len);
		struct binary *n;

		if (FAILEDTOALLOC(name))
			return NULL;
		n = realloc(c->binaries, (j+1)*sizeof(struct binary));
		if (FAILEDTOALLOC(n)) {
			free(name);
			return NULL;
		}
		c->binaries = n;
		c->binarycount = j+1;
		c->binaries[j].name = name;
		c->binaries[j].description = NULL;
		c->binaries[j].files = NULL;
		c->binaries[j].missedinheader = true;
		c->binaries[j].uncheckable = false;
	}
	assert (j < c->binarycount);
	return &c->binaries[j];
}

static retvalue parse_changes_description(struct changes *c, struct strlist *tmp) {
	int i;

	for (i = 0 ; i < tmp->count ; i++) {
		struct binary *b;
		const char *p = tmp->values[i];
		const char *e = p;
		const char *d;
		while (*e != '\0' && *e != ' ' && *e != '\t')
			e++;
		d = e;
		while (*d == ' ' || *d == '\t')
			d++;
		if (*d == '-')
			d++;
		while (*d == ' ' || *d == '\t')
			d++;

		b = get_binary(c, p, e-p);
		if (FAILEDTOALLOC(b))
			return RET_ERROR_OOM;

		b->description = strdup(d);
		if (FAILEDTOALLOC(b->description))
			return RET_ERROR_OOM;
	}
	return RET_OK;
}

static retvalue parse_changes_files(struct changes *c, struct strlist filelines[cs_hashCOUNT]) {
	int i;
	struct fileentry *f;
	retvalue r;
	struct hashes *hashes;
	struct strlist *tmp;
	size_t ofs, count = 0;
	enum checksumtype cs;

	tmp = &filelines[cs_md5sum];
	hashes = nzNEW(tmp->count, struct hashes);
	if (FAILEDTOALLOC(hashes))
		return RET_ERROR_OOM;

	for (i = 0 ; i < tmp->count ; i++) {
		char *p;
		const char *md5start, *md5end, *sizestart, *sizeend,
		           *sectionstart, *sectionend, *priostart, *prioend,
		           *filestart, *fileend;
		p = tmp->values[i];
#undef xisspace
#define xisspace(c) (c == ' ' || c == '\t')
		while (*p !='\0' && xisspace(*p))
			p++;
		md5start = p;
		while ((*p >= '0' && *p <= '9') ||
				(*p >= 'A' && *p <= 'F') ||
				(*p >= 'a' && *p <= 'f')) {
			if (*p >= 'A' && *p <= 'F')
				(*p) += 'a' - 'A';
			p++;
		}
		md5end = p;
		while (*p !='\0' && !xisspace(*p))
			p++;
		while (*p !='\0' && xisspace(*p))
			p++;
		while (*p == '0' && ('0' <= p[1] && p[1] <= '9'))
			p++;
		sizestart = p;
		while ((*p >= '0' && *p <= '9'))
			p++;
		sizeend = p;
		while (*p !='\0' && !xisspace(*p))
			p++;
		while (*p !='\0' && xisspace(*p))
			p++;
		sectionstart = p;
		while (*p !='\0' && !xisspace(*p))
			p++;
		sectionend = p;
		while (*p !='\0' && xisspace(*p))
			p++;
		priostart = p;
		while (*p !='\0' && !xisspace(*p))
			p++;
		prioend = p;
		while (*p !='\0' && xisspace(*p))
			p++;
		filestart = p;
		while (*p !='\0' && !xisspace(*p))
			p++;
		fileend = p;
		while (*p !='\0' && xisspace(*p))
			p++;
		if (*p != '\0') {
			fprintf(stderr,
"Unexpected sixth argument in '%s'!\n",
					tmp->values[i]);
			free(hashes);
			return RET_ERROR;
		}
		if (fileend - filestart == 0)
			continue;
		f = add_fileentry(c, filestart, fileend-filestart, false, &ofs);
		assert (ofs <= count);
		if (ofs == count)
			count++;
		if (hashes[ofs].hashes[cs_md5sum].start != NULL) {
			fprintf(stderr,
"WARNING: Multiple occourance of '%s' in .changes file!\nIgnoring all but the first one.\n",
					f->basename);
			continue;
		}
		hashes[ofs].hashes[cs_md5sum].start = md5start;
		hashes[ofs].hashes[cs_md5sum].len = md5end - md5start;
		hashes[ofs].hashes[cs_length].start = sizestart;
		hashes[ofs].hashes[cs_length].len = sizeend - sizestart;

		if (sectionend - sectionstart == 1 && *sectionstart == '-') {
			f->section = NULL;
		} else {
			f->section = strndup(sectionstart,
					sectionend - sectionstart);
			if (FAILEDTOALLOC(f->section))
				return RET_ERROR_OOM;
		}
		if (prioend - priostart == 1 && *priostart == '-') {
			f->priority = NULL;
		} else {
			f->priority = strndup(priostart, prioend - priostart);
			if (FAILEDTOALLOC(f->priority))
				return RET_ERROR_OOM;
		}
	}
	const char * const hashname[cs_hashCOUNT] = {"Md5", "Sha1", "Sha256" };
	for (cs = cs_firstEXTENDED ; cs < cs_hashCOUNT ; cs++) {
		tmp = &filelines[cs];

		for (i = 0 ; i < tmp->count ; i++) {
			char *p;
			const char *hashstart, *hashend, *sizestart, *sizeend,
			      *filestart, *fileend;
			p = tmp->values[i];
			while (*p !='\0' && xisspace(*p))
				p++;
			hashstart = p;
			while ((*p >= '0' && *p <= '9') ||
					(*p >= 'A' && *p <= 'F') ||
					(*p >= 'a' && *p <= 'f') ) {
				if (*p >= 'A' && *p <= 'F')
					(*p) += 'a' - 'A';
				p++;
			}
			hashend = p;
			while (*p !='\0' && !xisspace(*p))
				p++;
			while (*p !='\0' && xisspace(*p))
				p++;
			while (*p == '0' && ('0' <= p[1] && p[1] <= '9'))
				p++;
			sizestart = p;
			while ((*p >= '0' && *p <= '9'))
				p++;
			sizeend = p;
			while (*p !='\0' && !xisspace(*p))
				p++;
			while (*p !='\0' && xisspace(*p))
				p++;
			filestart = p;
			while (*p !='\0' && !xisspace(*p))
				p++;
			fileend = p;
			while (*p !='\0' && xisspace(*p))
				p++;
			if (*p != '\0') {
				fprintf(stderr,
"Unexpected forth argument in '%s'!\n",
						tmp->values[i]);
				return RET_ERROR;
			}
			if (fileend - filestart == 0)
				continue;
			f = add_fileentry(c, filestart, fileend-filestart,
					false, &ofs);
			assert (ofs <= count);
			// until md5sums are no longer obligatory:
			if (ofs == count)
				continue;
			if (hashes[ofs].hashes[cs].start != NULL) {
				fprintf(stderr,
"WARNING: Multiple occourance of '%s' in Checksums-'%s' of .changes file!\n"
"Ignoring all but the first one.\n",
						f->basename, hashname[cs]);
				continue;
			}
			hashes[ofs].hashes[cs].start = hashstart;
			hashes[ofs].hashes[cs].len = hashend - hashstart;

			size_t sizelen = sizeend - sizestart;

			if (hashes[ofs].hashes[cs_length].start == NULL) {
				hashes[ofs].hashes[cs_length].start = sizestart;
				hashes[ofs].hashes[cs_length].len = sizelen;

			} else if (hashes[ofs].hashes[cs_length].len != sizelen
			           || memcmp(sizestart,
			                     hashes[ofs].hashes[cs_length].start,
			                     sizelen) != 0) {
				fprintf(stderr,
"Error: Contradicting file size information for '%s' ('%.*s' vs '%.*s') in .changes file\n",
					f->basename,
					(int)sizelen, sizestart,
					(int)hashes[ofs].hashes[cs_length].len,
					hashes[ofs].hashes[cs_length].start);
				return RET_ERROR;
			}
		}
	}
	ofs = 0;
	for (f = c->files ; f != NULL ; f = f->next, ofs++) {
		r = checksums_initialize(&f->checksumsfromchanges,
				hashes[ofs].hashes);
		if (RET_WAS_ERROR(r))
			return r;
	}
	assert (count == ofs);
	free(hashes);

	return RET_OK;
}

static retvalue read_dscfile(const char *fullfilename, struct dscfile **dsc) {
	struct dscfile *n;
	struct strlist filelines[cs_hashCOUNT];
	enum checksumtype cs;
	retvalue r;

	n = zNEW(struct dscfile);
	if (FAILEDTOALLOC(n))
		return RET_ERROR_OOM;
	r = signature_readsignedchunk(fullfilename, fullfilename,
			&n->controlchunk, NULL, NULL);
	assert (r != RET_NOTHING);
	// TODO: can this be ignored sometimes?
	if (RET_WAS_ERROR(r)) {
		free(n);
		return r;
	}
	r = chunk_getname(n->controlchunk, "Source", &n->name, false);
	if (RET_WAS_ERROR(r)) {
		dscfile_free(n);
		return r;
	}
	r = chunk_getvalue(n->controlchunk, "Maintainer", &n->maintainer);
	if (RET_WAS_ERROR(r)) {
		dscfile_free(n);
		return r;
	}
	r = chunk_getvalue(n->controlchunk, "Version", &n->version);
	if (RET_WAS_ERROR(r)) {
		dscfile_free(n);
		return r;
	}

	/* usually not here, but hidden in the contents */
	r = chunk_getvalue(n->controlchunk, "Section", &n->section);
	if (RET_WAS_ERROR(r)) {
		dscfile_free(n);
		return r;
	}
	/* dito */
	r = chunk_getvalue(n->controlchunk, "Priority", &n->priority);
	if (RET_WAS_ERROR(r)) {
		dscfile_free(n);
		return r;
	}

	for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++) {
		assert (source_checksum_names[cs] != NULL);
		r = chunk_getextralinelist(n->controlchunk,
				source_checksum_names[cs], &filelines[cs]);
		if (r == RET_NOTHING) {
			if (cs == cs_md5sum) {
				fprintf(stderr,
"Error: Missing 'Files' entry in '%s'!\n",             fullfilename);
				r = RET_ERROR;
			}
			strlist_init(&filelines[cs]);
		}
		if (RET_WAS_ERROR(r)) {
			while (cs-- > cs_md5sum) {
				strlist_done(&filelines[cs]);
			}
			dscfile_free(n);
			return r;
		}
	}
	r = checksumsarray_parse(&n->expected, filelines, fullfilename);
	for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++) {
		strlist_done(&filelines[cs]);
	}
	if (RET_WAS_ERROR(r)) {
		dscfile_free(n);
		return r;
	}
	if (n->expected.names.count > 0) {
		n->uplink = nzNEW(n->expected.names.count, struct fileentry *);
		if (FAILEDTOALLOC(n->uplink)) {
			dscfile_free(n);
			return RET_ERROR_OOM;
		}
	}
	*dsc = n;
	return RET_OK;
}

static retvalue parse_dsc(struct fileentry *dscfile, struct changes *changes) {
	struct dscfile *n;
	retvalue r;
	int i;

	if (dscfile->fullfilename == NULL)
		return RET_NOTHING;
	r = read_dscfile(dscfile->fullfilename, &n);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r))
		return r;
	for (i =  0 ; i < n->expected.names.count ; i++) {
		const char *basefilename = n->expected.names.values[i];
		n->uplink[i] = add_fileentry(changes,
				basefilename, strlen(basefilename),
				true, NULL);
		if (FAILEDTOALLOC(n->uplink[i])) {
			dscfile_free(n);
			return RET_ERROR_OOM;
		}
	}
	dscfile->dsc = n;
	return RET_OK;
}

#define DSC_WRITE_FILES 1
#define DSC_WRITE_ALL 0xFFFF
#define flagset(a) (flags & a) != 0

static retvalue write_dsc_file(struct fileentry *dscfile, unsigned int flags) {
	struct dscfile *dsc = dscfile->dsc;
	int i;
	struct chunkeditfield *cef;
	retvalue r;
	char *control; size_t controllen;
	struct checksums *checksums;
	char *destfilename;
	enum checksumtype cs;

	if (flagset(DSC_WRITE_FILES)) {
		cef = NULL;
		for (cs = cs_hashCOUNT ; (cs--) > cs_md5sum ; ) {
			cef = cef_newfield(source_checksum_names[cs],
					CEF_ADD, CEF_LATE,
					dsc->expected.names.count, cef);
			if (FAILEDTOALLOC(cef))
				return RET_ERROR_OOM;
			for (i = 0 ; i < dsc->expected.names.count ; i++) {
				const char *basefilename =
					dsc->expected.names.values[i];
				const char *hash, *size;
				size_t hashlen, sizelen;

				if (!checksums_gethashpart(dsc->expected.checksums[i],
							cs, &hash, &hashlen,
							&size, &sizelen)) {
					assert (cs != cs_md5sum);
					cef = cef_pop(cef);
					break;
				}
				cef_setline2(cef, i, hash, hashlen,
						size, sizelen,
						1, basefilename, NULL);
			}
		}
	} else
		cef = NULL;

	r = chunk_edit(dsc->controlchunk, &control, &controllen, cef);
	cef_free(cef);
	if (RET_WAS_ERROR(r))
		return r;
	assert (RET_IS_OK(r));

	// TODO: try to add the signatures to it again...

	// TODO: add options to place changed files in different directory...
	if (dscfile->fullfilename != NULL)
		destfilename = strdup(dscfile->fullfilename);
	else
		destfilename = strdup(dscfile->basename);
	if (FAILEDTOALLOC(destfilename)) {
		free(control);
		return RET_ERROR_OOM;
	}

	r = checksums_replace(destfilename, control, controllen, &checksums);
	if (RET_WAS_ERROR(r)) {
		free(destfilename);
		free(control);
		return r;
	}
	assert (RET_IS_OK(r));

	free(dscfile->fullfilename);
	dscfile->fullfilename = destfilename;
	checksums_free(dscfile->realchecksums);
	dscfile->realchecksums = checksums;
	free(dsc->controlchunk);
	dsc->controlchunk = control;
	return RET_OK;
}

static retvalue read_binaryfile(const char *fullfilename, struct binaryfile **result) {
	retvalue r;
	struct binaryfile *n;

	n = zNEW(struct binaryfile);
	if (FAILEDTOALLOC(n))
		return RET_ERROR_OOM;

	r = extractcontrol(&n->controlchunk, fullfilename);
	if (!RET_IS_OK(r)) {
		free(n);
		if (r == RET_ERROR_OOM)
			return r;
		else
			return RET_NOTHING;
	}

	r = chunk_getname(n->controlchunk, "Package", &n->name, false);
	if (RET_WAS_ERROR(r)) {
		binaryfile_free(n);
		return r;
	}
	r = chunk_getvalue(n->controlchunk, "Version", &n->version);
	if (RET_WAS_ERROR(r)) {
		binaryfile_free(n);
		return r;
	}
	r = chunk_getnameandversion(n->controlchunk, "Source",
			&n->sourcename, &n->sourceversion);
	if (RET_WAS_ERROR(r)) {
		binaryfile_free(n);
		return r;
	}
	r = chunk_getvalue(n->controlchunk, "Maintainer", &n->maintainer);
	if (RET_WAS_ERROR(r)) {
		binaryfile_free(n);
		return r;
	}
	r = chunk_getvalue(n->controlchunk, "Architecture", &n->architecture);
	if (RET_WAS_ERROR(r)) {
		binaryfile_free(n);
		return r;
	}
	r = chunk_getvalue(n->controlchunk, "Section", &n->section);
	if (RET_WAS_ERROR(r)) {
		binaryfile_free(n);
		return r;
	}
	r = chunk_getvalue(n->controlchunk, "Priority", &n->priority);
	if (RET_WAS_ERROR(r)) {
		binaryfile_free(n);
		return r;
	}
	r = chunk_getvalue(n->controlchunk, "Description", &n->shortdescription);
	if (RET_WAS_ERROR(r)) {
		binaryfile_free(n);
		return r;
	}
	*result = n;
	return RET_OK;
}

static retvalue parse_deb(struct fileentry *debfile, struct changes *changes) {
	retvalue r;
	struct binaryfile *n;

	if (debfile->fullfilename == NULL)
		return RET_NOTHING;
	r = read_binaryfile(debfile->fullfilename, &n);
	if (!RET_IS_OK(r))
		return r;
	if (n->name != NULL) {
		n->binary = get_binary(changes, n->name, strlen(n->name));
		if (FAILEDTOALLOC(n->binary)) {
			binaryfile_free(n);
			return RET_ERROR_OOM;
		}
		n->next = n->binary->files;
		n->binary->files = n;
	}

	debfile->deb = n;
	return RET_OK;
}

static retvalue processfiles(const char *changesfilename, struct changes *changes,
		const struct strlist *searchpath) {
	char *dir;
	struct fileentry *file;
	retvalue r;

	r = dirs_getdirectory(changesfilename, &dir);
	if (RET_WAS_ERROR(r))
		return r;

	for (file = changes->files; file != NULL ; file = file->next) {
		assert (file->fullfilename == NULL);

		r = searchforfile(dir, file->basename, searchpath, NULL,
				&file->fullfilename);

		if (RET_IS_OK(r)) {
			if (file->type == ft_DSC)
				r = parse_dsc(file, changes);
			else if (file->type == ft_DEB || file->type == ft_UDEB)
				r = parse_deb(file, changes);
			if (RET_WAS_ERROR(r)) {
				free(dir);
				return r;
			}
		}

		if (r == RET_NOTHING) {
			/* apply heuristics when not readable */
			if (file->type == ft_DSC) {
			} else if (file->type == ft_DEB || file->type == ft_UDEB) {
				struct binary *b; size_t len;

				len = 0;
				while (file->basename[len] != '_' &&
						file->basename[len] != '\0')
					len++;
				b = get_binary(changes, file->basename, len);
				if (FAILEDTOALLOC(b)) {
					free(dir);
					return RET_ERROR_OOM;
				}
				b->uncheckable = true;
			}
		}
	}
	free(dir);
	return RET_OK;
}

static retvalue parse_changes(const char *changesfile, const char *chunk, struct changes **changes, const struct strlist *searchpath) {
	retvalue r;
	struct strlist tmp;
	struct strlist filelines[cs_hashCOUNT];
	enum checksumtype cs;
#define R if (RET_WAS_ERROR(r)) { changes_free(n); return r; }

	struct changes *n = zNEW(struct changes);
	if (FAILEDTOALLOC(n))
		return RET_ERROR_OOM;
	n->filename = strdup(changesfile);
	if (FAILEDTOALLOC(n->filename)) {
		changes_free(n);
		return RET_ERROR_OOM;
	}
	r = dirs_getdirectory(changesfile, &n->basedir);
	R;
	// TODO: do getname here? trim spaces?
	r = chunk_getvalue(chunk, "Source", &n->name);
	R;
	if (r == RET_NOTHING) {
		fprintf(stderr, "Missing 'Source:' field in %s!\n",
				changesfile);
		n->name = NULL;
	}
	r = chunk_getvalue(chunk, "Version", &n->version);
	R;
	if (r == RET_NOTHING) {
		fprintf(stderr, "Missing 'Version:' field in %s!\n",
				changesfile);
		n->version = NULL;
	}
	r = chunk_getwordlist(chunk, "Architecture", &n->architectures);
	R;
	if (r == RET_NOTHING)
		strlist_init(&n->architectures);
	r = chunk_getwordlist(chunk, "Distribution", &n->distributions);
	R;
	if (r == RET_NOTHING)
		strlist_init(&n->distributions);
	r = chunk_getvalue(chunk, "Maintainer", &n->maintainer);
	R;
	if (r == RET_NOTHING) {
		fprintf(stderr, "Missing 'Maintainer:' field in %s!\n",
				changesfile);
		n->maintainer = NULL;
	}
	r = chunk_getuniqwordlist(chunk, "Binary", &tmp);
	R;
	if (r == RET_NOTHING) {
		n->binaries = NULL;
	} else {
		int i;

		assert (RET_IS_OK(r));
		n->binaries = nzNEW(tmp.count, struct binary);
		if (FAILEDTOALLOC(n->binaries)) {
			changes_free(n);
			return RET_ERROR_OOM;
		}
		for (i = 0 ; i < tmp.count ; i++) {
			n->binaries[i].name = tmp.values[i];
		}
		n->binarycount = tmp.count;
		free(tmp.values);
	}
	r = chunk_getextralinelist(chunk, "Description", &tmp);
	R;
	if (RET_IS_OK(r)) {
		r = parse_changes_description(n, &tmp);
		strlist_done(&tmp);
		if (RET_WAS_ERROR(r)) {
			changes_free(n);
			return r;
		}
	}
	for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++) {
		assert (changes_checksum_names[cs] != NULL);
		r = chunk_getextralinelist(chunk,
				changes_checksum_names[cs], &filelines[cs]);
		if (r == RET_NOTHING) {
			if (cs == cs_md5sum)
				break;
			strlist_init(&filelines[cs]);
		}
		if (RET_WAS_ERROR(r)) {
			while (cs-- > cs_md5sum) {
				strlist_done(&filelines[cs]);
			}
			changes_free(n);
			return r;
		}
	}
	if (cs == cs_hashCOUNT) {
		r = parse_changes_files(n, filelines);
		for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++) {
			strlist_done(&filelines[cs]);
		}
		if (RET_WAS_ERROR(r)) {
			changes_free(n);
			return r;
		}
	}
	r = processfiles(changesfile, n, searchpath);
	R;
	*changes = n;
	return RET_OK;
}

#define CHANGES_WRITE_FILES		0x01
#define CHANGES_WRITE_BINARIES		0x02
#define CHANGES_WRITE_SOURCE		0x04
#define CHANGES_WRITE_VERSION		0x08
#define CHANGES_WRITE_ARCHITECTURES	0x10
#define CHANGES_WRITE_MAINTAINER 	0x20
#define CHANGES_WRITE_DISTRIBUTIONS 	0x40
#define CHANGES_WRITE_ALL 	      0xFFFF

static retvalue write_changes_file(const char *changesfilename, struct changes *c, unsigned int flags, bool fakefields) {
	struct chunkeditfield *cef;
	char datebuffer[100];
	retvalue r;
	char *control; size_t controllen;
	unsigned int filecount = 0;
	struct fileentry *f;
	struct tm *tm; time_t t;
	unsigned int i;
	struct strlist binaries;
	enum checksumtype cs;

	strlist_init(&binaries);

	for (f = c->files; f != NULL ; f = f->next) {
		if (f->checksumsfromchanges != NULL)
			filecount++;
	}

	if (flagset(CHANGES_WRITE_FILES)) {
		cef = NULL;
		for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++) {
			cef = cef_newfield(changes_checksum_names[cs],
					CEF_ADD, CEF_LATE, filecount, cef);
			if (FAILEDTOALLOC(cef))
				return RET_ERROR_OOM;
			i = 0;
			for (f = c->files; f != NULL ; f = f->next) {
				const char *hash, *size;
				size_t hashlen, sizelen;

				if (f->checksumsfromchanges == NULL)
					continue;
				if (!checksums_gethashpart(f->checksumsfromchanges,
							cs, &hash, &hashlen,
							&size, &sizelen)) {
					assert (cs != cs_md5sum);
					cef = cef_pop(cef);
					break;
				}
				if (cs == cs_md5sum)
					cef_setline2(cef, i,
						hash, hashlen, size, sizelen,
						3,
						f->section?f->section:"-",
						f->priority?f->priority:"-",
						f->basename, NULL);
				else
					/* strange way, but as dpkg-genchanges
					 * does it this way... */
					cef_setline2(cef, i,
						hash, hashlen, size, sizelen,
						1,
						f->basename, NULL);
				i++;
			}
			assert (f != NULL || i == filecount);
		}
	} else {
		cef = cef_newfield("Files", CEF_KEEP, CEF_LATE, 0, NULL);
		if (FAILEDTOALLOC(cef))
			return RET_ERROR_OOM;
	}
	if (fakefields) {
		cef = cef_newfield("Changes", CEF_ADDMISSED, CEF_LATE, 0, cef);
		if (FAILEDTOALLOC(cef))
			return RET_ERROR_OOM;
		cef_setdata(cef,
"\n Changes information missing, as not an original .changes file");
	} else {
		cef = cef_newfield("Changes", CEF_KEEP, CEF_LATE, 0, cef);
		if (FAILEDTOALLOC(cef))
			return RET_ERROR_OOM;
	}
	cef = cef_newfield("Closes", CEF_KEEP, CEF_LATE, 0, cef);
	if (FAILEDTOALLOC(cef))
		return RET_ERROR_OOM;
	if (flagset(CHANGES_WRITE_BINARIES)) {
		unsigned int count = 0;
		for (i = 0 ; i < c->binarycount ; i++) {
			const struct binary *b = c->binaries + i;
			if (b->description != NULL)
				count++;
		}
		cef = cef_newfield("Description", CEF_ADD, CEF_LATE,
				count, cef);
		if (FAILEDTOALLOC(cef))
			return RET_ERROR_OOM;
		count = 0;
		for (i = 0 ; i < c->binarycount ; i++) {
			const struct binary *b = c->binaries + i;
			if (b->description == NULL)
				continue;
			cef_setline(cef, count++, 3,
					b->name,
					"-",
					b->description,
					NULL);
		}

	}
	// Changed-by: line
	if (flagset(CHANGES_WRITE_MAINTAINER) && c->maintainer != NULL) {
		cef = cef_newfield("Maintainer", CEF_ADD, CEF_EARLY, 0, cef);
		if (FAILEDTOALLOC(cef))
			return RET_ERROR_OOM;
		cef_setdata(cef, c->maintainer);
	} else {
		cef = cef_newfield("Maintainer", CEF_KEEP, CEF_EARLY, 0, cef);
		if (FAILEDTOALLOC(cef))
			return RET_ERROR_OOM;
	}
	if (fakefields) {
		cef = cef_newfield("Urgency", CEF_ADDMISSED, CEF_EARLY, 0, cef);
		if (FAILEDTOALLOC(cef)) {
			return RET_ERROR_OOM;
		}
		cef_setdata(cef, "low");
	} else {
		cef = cef_newfield("Urgency", CEF_KEEP, CEF_EARLY, 0, cef);
		if (FAILEDTOALLOC(cef))
			return RET_ERROR_OOM;
	}
	cef = cef_newfield("Distribution", CEF_KEEP, CEF_EARLY, 0, cef);
	if (FAILEDTOALLOC(cef))
		return RET_ERROR_OOM;
	if (c->distributions.count > 0) {
		if (flagset(CHANGES_WRITE_DISTRIBUTIONS))
			cef = cef_newfield("Distribution", CEF_ADD,
					CEF_EARLY, 0, cef);
		else
			cef = cef_newfield("Distribution", CEF_ADDMISSED,
					CEF_EARLY, 0, cef);
		if (FAILEDTOALLOC(cef))
			return RET_ERROR_OOM;
		cef_setwordlist(cef, &c->distributions);
	} else if (flagset(CHANGES_WRITE_DISTRIBUTIONS)) {
		cef = cef_newfield("Distribution", CEF_DELETE,
				CEF_EARLY, 0, cef);
		if (FAILEDTOALLOC(cef))
			return RET_ERROR_OOM;
	}
	if (c->version != NULL) {
		if (flagset(CHANGES_WRITE_VERSION))
			cef = cef_newfield("Version", CEF_ADD,
					CEF_EARLY, 0, cef);
		else
			cef = cef_newfield("Version", CEF_ADDMISSED,
					CEF_EARLY, 0, cef);
		if (FAILEDTOALLOC(cef))
			return RET_ERROR_OOM;
		cef_setdata(cef, c->version);
	} else if (flagset(CHANGES_WRITE_VERSION)) {
		cef = cef_newfield("Version", CEF_DELETE,
				CEF_EARLY, 0, cef);
		if (FAILEDTOALLOC(cef))
			return RET_ERROR_OOM;
	}
	if (flagset(CHANGES_WRITE_ARCHITECTURES)) {
		cef = cef_newfield("Architecture", CEF_ADD, CEF_EARLY, 0, cef);
		if (FAILEDTOALLOC(cef))
			return RET_ERROR_OOM;
		cef_setwordlist(cef, &c->architectures);
	} else {
		cef = cef_newfield("Architecture", CEF_KEEP, CEF_EARLY, 0, cef);
		if (FAILEDTOALLOC(cef))
			return RET_ERROR_OOM;
	}
	if (flagset(CHANGES_WRITE_BINARIES)) {
		r = strlist_init_n(c->binarycount, &binaries);
		if (RET_WAS_ERROR(r)) {
			cef_free(cef);
			return r;
		}
		assert (RET_IS_OK(r));
		for (i = 0 ; i < c->binarycount ; i++) {
			const struct binary *b = c->binaries + i;
			if (!b->missedinheader) {
				r = strlist_add_dup(&binaries, b->name);
				if (RET_WAS_ERROR(r)) {
					strlist_done(&binaries);
					cef_free(cef);
					return r;
				}
			}
		}
		cef = cef_newfield("Binary", CEF_ADD, CEF_EARLY, 0, cef);
		if (FAILEDTOALLOC(cef)) {
			strlist_done(&binaries);
			return RET_ERROR_OOM;
		}
		cef_setwordlist(cef, &binaries);
	} else {
		cef = cef_newfield("Binary", CEF_KEEP, CEF_EARLY, 0, cef);
		if (FAILEDTOALLOC(cef))
			return RET_ERROR_OOM;
	}
	if (c->name != NULL) {
		if (flagset(CHANGES_WRITE_SOURCE))
			cef = cef_newfield("Source", CEF_ADD,
					CEF_EARLY, 0, cef);
		else
			cef = cef_newfield("Source", CEF_ADDMISSED,
					CEF_EARLY, 0, cef);
		if (FAILEDTOALLOC(cef)) {
			strlist_done(&binaries);
			return RET_ERROR_OOM;
		}
		cef_setdata(cef, c->name);
	} else if (flagset(CHANGES_WRITE_SOURCE)) {
		cef = cef_newfield("Source", CEF_DELETE,
				CEF_EARLY, 0, cef);
		if (FAILEDTOALLOC(cef)) {
			strlist_done(&binaries);
			return RET_ERROR_OOM;
		}
	}
	// TODO: if localized make sure this uses C locale....
	t = time(NULL);
        if ((tm = localtime(&t)) != NULL &&
	    strftime(datebuffer, sizeof(datebuffer)-1,
		    "%a, %e %b %Y %H:%M:%S %Z", tm) > 0) {
		cef = cef_newfield("Date", CEF_ADD, CEF_EARLY, 0, cef);
		if (FAILEDTOALLOC(cef)) {
			strlist_done(&binaries);
			return RET_ERROR_OOM;
		}
		cef_setdata(cef, datebuffer);
	} else {
		cef = cef_newfield("Date", CEF_DELETE,
				CEF_EARLY, 0, cef);
		if (FAILEDTOALLOC(cef)) {
			strlist_done(&binaries);
			return RET_ERROR_OOM;
		}
	}
	cef = cef_newfield("Format", CEF_ADDMISSED, CEF_EARLY, 0, cef);
	if (FAILEDTOALLOC(cef)) {
		strlist_done(&binaries);
		return RET_ERROR_OOM;
	}
	cef_setdata(cef, "1.7");

	r = chunk_edit((c->control==NULL)?"":c->control, &control, &controllen,
			cef);
	strlist_done(&binaries);
	cef_free(cef);
	if (RET_WAS_ERROR(r))
		return r;
	assert (RET_IS_OK(r));

	// TODO: try to add the signatures to it again...

	// TODO: add options to place changed files in different directory...

	r = checksums_replace(changesfilename, control, controllen, NULL);
	if (RET_WAS_ERROR(r)) {
		free(control);
		return r;
	}
	assert (RET_IS_OK(r));

	free(c->control);
	c->control = control;
	return RET_OK;
}

static retvalue getchecksums(struct changes *changes) {
	struct fileentry *file;
	retvalue r;

	for (file = changes->files; file != NULL ; file = file->next) {

		if (file->fullfilename == NULL)
			continue;
		assert (file->realchecksums == NULL);

		r = checksums_read(file->fullfilename, &file->realchecksums);
		if (r == RET_ERROR_OOM)
			return r;
		else if (!RET_IS_OK(r)) {
			// assume everything else is not fatal and means
			// a file not readable...
			file->realchecksums = NULL;
		}
	}
	return RET_OK;
}

static bool may_be_type(const char *name, enum filetype ft) {
	enum compression c;
	size_t len = strlen(name);

	c = compression_by_suffix(name, &len);
	if (c != c_none && !typesuffix[ft].allowcompressed)
		return false;
	return strncmp(name + (len - typesuffix[ft].len),
			typesuffix[ft].suffix,
			typesuffix[ft].len) == 0;
}

static void verify_sourcefile_checksums(struct dscfile *dsc, int i, const char *dscfile) {
	const struct fileentry * const file = dsc->uplink[i];
	const struct checksums * const expectedchecksums
		= dsc->expected.checksums[i];
	const char * const basefilename = dsc->expected.names.values[i];
	assert (file != NULL);

	if (file->checksumsfromchanges == NULL) {
		if (may_be_type(basefilename, ft_ORIG_TAR)) {
			fprintf(stderr,
"Not checking checksums of '%s', as not included in .changes file.\n",
				basefilename);
			return;
		} else if (file->realchecksums == NULL) {
			fprintf(stderr,
"ERROR: File '%s' mentioned in '%s' was not found and is not mentioned in the .changes!\n",
				basefilename, dscfile);
			return;
		}
	}
	if (file->realchecksums == NULL)
		/* there will be an message later about that */
		return;
	if (checksums_check(expectedchecksums, file->realchecksums, NULL))
		return;

	if (file->checksumsfromchanges != NULL &&
	    checksums_check(expectedchecksums, file->checksumsfromchanges, NULL))
		fprintf(stderr,
"ERROR: checksums of '%s' differ from the ones listed in both '%s' and the .changes file!\n",
				basefilename, dscfile);
	else {
		fprintf(stderr,
"ERROR: checksums of '%s' differ from those listed in '%s':\n!\n",
				basefilename, dscfile);
		checksums_printdifferences(stderr,
				expectedchecksums, file->realchecksums);
	}
}

static void verify_binary_name(const char *basefilename, const char *name, const char *version, const char *architecture, enum filetype type, enum compression c) {
	size_t nlen, vlen, alen, slen;
	const char *versionwithoutepoch;

	if (name == NULL)
		return;
	nlen = strlen(name);
	if (strncmp(basefilename, name, nlen) != 0 || basefilename[nlen] != '_') {
		fprintf(stderr,
"ERROR: '%s' does not start with '%s_' as expected!\n",
					basefilename, name);
		return;
	}
	if (version == NULL)
		return;
	versionwithoutepoch = strchr(version, ':');
	if (versionwithoutepoch == NULL)
		versionwithoutepoch = version;
	else
		versionwithoutepoch++;
	vlen = strlen(versionwithoutepoch);
	if (strncmp(basefilename+nlen+1, versionwithoutepoch, vlen) != 0
			|| basefilename[nlen+1+vlen] != '_') {
		fprintf(stderr,
"ERROR: '%s' does not start with '%s_%s_' as expected!\n",
			basefilename, name, version);
		return;
	}
	if (architecture == NULL)
		return;
	alen = strlen(architecture);
	slen = typesuffix[type].len;
	if (strncmp(basefilename+nlen+1+vlen+1, architecture, alen) != 0
			|| strncmp(basefilename+nlen+1+vlen+1+alen,
				typesuffix[type].suffix, slen) != 0
			|| strcmp(basefilename+nlen+1+vlen+1+alen+slen,
				uncompression_suffix[c]) != 0)
		fprintf(stderr,
"ERROR: '%s' is not called '%s_%s_%s%s%s' as expected!\n",
			basefilename, name, versionwithoutepoch,
			architecture, typesuffix[type].suffix,
			uncompression_suffix[c]);
}

static retvalue verify(const char *changesfilename, struct changes *changes) {
	retvalue r;
	struct fileentry *file;
	size_t k;

	printf("Checking Source packages...\n");
	for (file = changes->files; file != NULL ; file = file->next) {
		const char *name, *version, *p;
		size_t namelen, versionlen, l;
		bool has_tar, has_diff, has_orig, has_format_tar;
		int i;

		if (file->type != ft_DSC)
			continue;
		if (!strlist_in(&changes->architectures, "source")) {
			fprintf(stderr,
"ERROR: '%s' contains a .dsc, but does not list Architecture 'source'!\n",
				changesfilename);
		}
		if (file->fullfilename == NULL) {
			fprintf(stderr,
"ERROR: Could not find '%s'!\n", file->basename);
			continue;
		}
		if (file->dsc == NULL) {
			fprintf(stderr,
"WARNING: Could not read '%s', thus it cannot be checked!\n",
				file->fullfilename);
			continue;
		}
		if (file->dsc->name == NULL)
			fprintf(stderr,
"ERROR: '%s' does not contain a 'Source:' header!\n", file->fullfilename);
		else if (changes->name != NULL &&
				strcmp(changes->name, file->dsc->name) != 0)
			fprintf(stderr,
"ERROR: '%s' lists Source '%s' while .changes lists '%s'!\n",
				file->fullfilename,
				file->dsc->name, changes->name);
		if (file->dsc->version == NULL)
			fprintf(stderr,
"ERROR: '%s' does not contain a 'Version:' header!\n", file->fullfilename);
		else if (changes->version != NULL &&
				strcmp(changes->version,
					file->dsc->version) != 0)
			fprintf(stderr,
"ERROR: '%s' lists Version '%s' while .changes lists '%s'!\n",
				file->fullfilename,
				file->dsc->version, changes->version);
		if (file->dsc->maintainer == NULL)
			fprintf(stderr,
"ERROR: No maintainer specified in '%s'!\n", file->fullfilename);
		else if (changes->maintainer != NULL &&
				strcmp(changes->maintainer,
					file->dsc->maintainer) != 0)
			fprintf(stderr,
"Warning: '%s' lists Maintainer '%s' while .changes lists '%s'!\n",
				file->fullfilename,
				file->dsc->maintainer, changes->maintainer);
		if (file->dsc->section != NULL && file->section != NULL &&
				strcmp(file->section, file->dsc->section) != 0)
			fprintf(stderr,
"Warning: '%s' has Section '%s' while .changes says it is '%s'!\n",
				file->fullfilename,
				file->dsc->section, file->section);
		if (file->dsc->priority != NULL && file->priority != NULL
				&& strcmp(file->priority,
					file->dsc->priority) != 0)
			fprintf(stderr,
"Warning: '%s' has Priority '%s' while .changes says it is '%s'!\n",
				file->fullfilename,
				file->dsc->priority, file->priority);
		// Todo: check types of files it contains...
		// check names are sensible
		p = file->basename;
		while (*p != '\0' && *p != '_')
			p++;
		if (*p == '_') {
			l = strlen(p+1);
			assert (l >= 4); /* It ends in ".dsc" to come here */
		} else
			l = 0;

		if (file->dsc->name != NULL) {
			name = file->dsc->name;
			namelen = strlen(name);
		} else {
			// TODO: more believe file name or changes name?
			if (changes->name != NULL) {
				name = changes->name;
				namelen = strlen(name);
			} else {
				if (*p != '_') {
					name = NULL;
					namelen = 0;
					fprintf(stderr,
"Warning: '%s' does not contain a '_' separating name and version!\n",
						file->basename);
				}else {
					name = file->basename;
					namelen = p-name;
				}
			}
		}
		if (file->dsc->version != NULL) {
			version = file->dsc->version;
			versionlen = strlen(version);
		} else {
			// TODO: dito
			if (changes->version != NULL) {
				version = changes->version;
				versionlen = strlen(version);
			} else {
				if (*p != '_') {
					version = NULL;
					SETBUTNOTUSED( versionlen = 0; )
					if (name != NULL)
						fprintf(stderr,
"ERROR: '%s' does not contain a '_' separating name and version!\n",
							file->basename);
				} else {
					version = p+1;
					versionlen = l-4;
				}
			}
		}
		if (version != NULL) {
			const char *colon = strchr(version, ':');
			if (colon != NULL) {
				colon++;
				versionlen -= (colon-version);
				version = colon;
			}
		}
		if (name != NULL && version != NULL) {
			if (*p != '_'
			|| (size_t)(p-file->basename) != namelen || l-4 != versionlen
			|| strncmp(p+1, version, versionlen) != 0
			|| strncmp(file->basename, name, namelen) != 0)
				fprintf(stderr,
"ERROR: '%s' is not called '%*s_%*s.dsc' as expected!\n",
					file->basename,
					(unsigned int)namelen, name,
					(unsigned int)versionlen, version);
		}
		has_tar = false;
		has_format_tar = false;
		has_diff = false;
		has_orig = false;
		for (i = 0 ; i < file->dsc->expected.names.count ; i++) {
			const char *basefilename
				= file->dsc->expected.names.values[i];
			const struct fileentry *sfile = file->dsc->uplink[i];
			size_t expectedversionlen, expectedformatlen;
			const char *expectedformat;
			bool istar = false, versionok;

			switch (sfile->type) {
				case ft_UNKNOWN:
					fprintf(stderr,
"ERROR: '%s' lists a file '%s' with unrecognized suffix!\n",
						file->fullfilename,
						basefilename);
					break;
				case ft_TAR:
					istar = true;
					has_tar = true;
					break;
				case ft_ORIG_TAR:
					if (has_orig)
						fprintf(stderr,
"ERROR: '%s' lists multiple .orig..tar files!\n",
						file->fullfilename);
					has_orig = true;
					break;
				case ft_DIFF:
					if (has_diff)
						fprintf(stderr,
"ERROR: '%s' lists multiple .diff files!\n",
						file->fullfilename);
					has_diff = true;
					break;
				default:
					assert (sfile->type == ft_UNKNOWN);
			}

			if (name == NULL) // TODO: try extracting it from this
				continue;
			if (strncmp(sfile->basename, name, namelen) != 0
					|| sfile->basename[namelen] != '_') {
				fprintf(stderr,
"ERROR: '%s' does not begin with '%*s_' as expected!\n",
					sfile->basename,
					(unsigned int)namelen, name);
				/* cannot check further */
				continue;
			}

			if (version == NULL)
				continue;
			/* versionlen is now always initialized */

			if (sfile->type == ft_ORIG_TAR) {
				const char *q, *revision;
				revision = NULL;
				for (q = version; *q != '\0'; q++) {
					if (*q == '-')
						revision = q;
				}
				if (revision == NULL)
					expectedversionlen = versionlen;
				else
					expectedversionlen = revision - version;
			} else
				expectedversionlen = versionlen;

			versionok = strncmp(sfile->basename+namelen+1,
					version, expectedversionlen) == 0;
			if (istar) {
				if (!versionok) {
					fprintf(stderr,
"ERROR: '%s' does not start with '%*s_%*s' as expected!\n",
						sfile->basename,
						(unsigned int)namelen, name,
						(unsigned int)expectedversionlen,
						version);
					continue;
				}
				expectedformat = sfile->basename + namelen + 1 +
					expectedversionlen;
				if (strncmp(expectedformat, ".tar.", 5) == 0)
					expectedformatlen = 0;
				else {
					const char *dot;

					dot = strchr(expectedformat + 1, '.');
					if (dot == NULL)
						expectedformatlen = 0;
					else {
						expectedformatlen =
							dot - expectedformat;
						has_format_tar = true;
					}
				}
			} else {
				expectedformat = "";
				expectedformatlen = 0;
			}

			if (sfile->type == ft_UNKNOWN)
				continue;
			if (versionok
			    && strncmp(sfile->basename+namelen+1
				    +expectedversionlen
				    +expectedformatlen,
				    typesuffix[sfile->type].suffix,
				    typesuffix[sfile->type].len) == 0
			    && strcmp(sfile->basename+namelen+1
				    +expectedversionlen
				    +expectedformatlen
				    +typesuffix[sfile->type].len,
				    uncompression_suffix[sfile->compression])
			    == 0)
				continue;
			fprintf(stderr,
"ERROR: '%s' is not called '%.*s_%.*s%.*s%s%s' as expected!\n",
					sfile->basename,
					(unsigned int)namelen, name,
					(unsigned int)expectedversionlen,
					version,
					(unsigned int)expectedformatlen,
					expectedformat,
					typesuffix[sfile->type].suffix,
					uncompression_suffix[sfile->compression]);
		}
		if (!has_tar && !has_orig)
			if (has_diff)
				fprintf(stderr,
"ERROR: '%s' lists only a .diff, but no .orig.tar!\n",
						file->fullfilename);
			else
				fprintf(stderr,
"ERROR: '%s' lists no source files!\n",
						file->fullfilename);
		else if (has_diff && !has_orig)
			fprintf(stderr,
"ERROR: '%s' lists a .diff, but the .tar is not called .orig.tar!\n",
					file->fullfilename);
		else if (!has_format_tar && !has_diff && has_orig)
			fprintf(stderr,
"ERROR: '%s' lists a .orig.tar, but no .diff!\n",
					file->fullfilename);
	}
	printf("Checking Binary consistency...\n");
	for (k = 0 ; k < changes->binarycount ; k++) {
		struct binary *b = &changes->binaries[k];

		if (b->files == NULL && !b->uncheckable) {
			/* no files - not even conjectured -,
			 * headers must be wrong */

			if (b->description != NULL && !b->missedinheader) {
				fprintf(stderr,
"ERROR: '%s' has binary '%s' in 'Binary:' and 'Description:' header, but no files for it found!\n",
					changesfilename, b->name);
			} else if (b->description != NULL) {
				fprintf(stderr,
"ERROR: '%s' has unexpected description of '%s'\n",
					changesfilename, b->name);
			} else {
				assert (!b->missedinheader);
				fprintf(stderr,
"ERROR: '%s' has unexpected Binary: '%s'\n",
					changesfilename, b->name);
			}
		}
		if (b->files == NULL)
			continue;
		/* files are there, make sure they are listed and
		 * have a description*/

		if (b->description == NULL) {
			fprintf(stderr,
"ERROR: '%s' has no description for '%s'\n",
				changesfilename, b->name);
		}
		if (b->missedinheader) {
				fprintf(stderr,
"ERROR: '%s' does not list '%s' in its Binary header!\n",
					changesfilename, b->name);
		}
		// TODO: check if the files have the names they should
		// have an architectures as they are listed...
	}
	for (file = changes->files; file != NULL ; file = file->next) {
		const struct binary *b;
		const struct binaryfile *deb;

		if (file->type != ft_DEB && file->type != ft_UDEB)
			continue;
		if (file->fullfilename == NULL) {
			fprintf(stderr,
"ERROR: Could not find '%s'!\n", file->basename);
			continue;
		}
		if (file->deb == NULL) {
			fprintf(stderr,
"WARNING: Could not read '%s', thus it cannot be checked!\n", file->fullfilename);
			continue;
		}
		deb = file->deb;
		b = deb->binary;

		if (deb->shortdescription == NULL)
			fprintf(stderr,
"Warning: '%s' contains no description!\n",
				file->fullfilename);
		else if (b->description != NULL &&
			 strcmp(b->description, deb->shortdescription) != 0)
				fprintf(stderr,
"Warning: '%s' says '%s' has description '%s' while '%s' has '%s'!\n",
					changesfilename, b->name,
					b->description,
					file->fullfilename,
					deb->shortdescription);
		if (deb->name == NULL)
			fprintf(stderr,
"ERROR: '%s' does not contain a 'Package:' header!\n", file->fullfilename);
		if (deb->sourcename != NULL) {
			if (strcmp(changes->name, deb->sourcename) != 0)
				fprintf(stderr,
"ERROR: '%s' lists Source '%s' while .changes lists '%s'!\n",
					file->fullfilename,
					deb->sourcename, changes->name);
		} else if (deb->name != NULL &&
				strcmp(changes->name, deb->name) != 0) {
			fprintf(stderr,
"ERROR: '%s' lists Source '%s' while .changes lists '%s'!\n",
				file->fullfilename,
				deb->name, changes->name);
		}
		if (deb->version == NULL)
			fprintf(stderr,
"ERROR: '%s' does not contain a 'Version:' header!\n", file->fullfilename);
		if (deb->sourceversion != NULL) {
			if (strcmp(changes->version, deb->sourceversion) != 0)
				fprintf(stderr,
"ERROR: '%s' lists Source version '%s' while .changes lists '%s'!\n",
					file->fullfilename,
					deb->sourceversion, changes->version);
		} else if (deb->version != NULL &&
				strcmp(changes->version, deb->version) != 0) {
			fprintf(stderr,
"ERROR: '%s' lists Source version '%s' while .changes lists '%s'!\n",
				file->fullfilename,
				deb->version, changes->name);
		}

		if (deb->maintainer == NULL)
			fprintf(stderr,
"ERROR: No maintainer specified in '%s'!\n", file->fullfilename);
		else if (changes->maintainer != NULL &&
				strcmp(changes->maintainer,
					deb->maintainer) != 0)
			fprintf(stderr,
"Warning: '%s' lists Maintainer '%s' while .changes lists '%s'!\n",
				file->fullfilename,
				deb->maintainer, changes->maintainer);
		if (deb->section == NULL)
			fprintf(stderr,
"ERROR: No section specified in '%s'!\n", file->fullfilename);
		else if (file->section != NULL &&
				strcmp(file->section, deb->section) != 0)
			fprintf(stderr,
"Warning: '%s' has Section '%s' while .changes says it is '%s'!\n",
				file->fullfilename,
				deb->section, file->section);
		if (deb->priority == NULL)
			fprintf(stderr,
"ERROR: No priority specified in '%s'!\n", file->fullfilename);
		else if (file->priority != NULL &&
				strcmp(file->priority, deb->priority) != 0)
			fprintf(stderr,
"Warning: '%s' has Priority '%s' while .changes says it is '%s'!\n",
				file->fullfilename,
				deb->priority, file->priority);
		verify_binary_name(file->basename, deb->name, deb->version,
				deb->architecture, file->type, file->compression);
		if (deb->architecture != NULL
				&& !strlist_in(&changes->architectures,
					deb->architecture)) {
			fprintf(stderr,
"ERROR: '%s' does not list Architecture: '%s' needed for '%s'!\n",
				changesfilename, deb->architecture,
				file->fullfilename);
		}
		// todo: check for md5sums file, verify it...
	}

	printf("Checking checksums...\n");
	r = getchecksums(changes);
	if (RET_WAS_ERROR(r))
		return r;
	for (file = changes->files; file != NULL ; file = file->next) {

		if (file->checksumsfromchanges == NULL)
			/* nothing to check here */
			continue;

		if (file->fullfilename == NULL) {
			fprintf(stderr,
"WARNING: Could not check checksums of '%s' as file not found!\n",
					file->basename);
			if (file->type == ft_DSC) {
				fprintf(stderr,
"WARNING: This file most likely contains additional checksums which could also not be checked because it was not found!\n");
			}
			continue;
		}
		if (file->realchecksums == NULL) {
			fprintf(stderr,
"WARNING: Could not check checksums of '%s'! File vanished while checking or not readable?\n",
					file->basename);
		} else if (!checksums_check(file->realchecksums,
					file->checksumsfromchanges, NULL)) {
			fprintf(stderr,
"ERROR: checksums of '%s' differ from those listed in .changes:\n",
					file->fullfilename);
			checksums_printdifferences(stderr,
					file->checksumsfromchanges,
					file->realchecksums);
		}

		if (file->type == ft_DSC) {
			int i;

			if (file->dsc == NULL) {
				fprintf(stderr,
"WARNING: Could not read '%s', thus the content cannot be checked\n"
" and may be faulty and other things depending on it may be incorrect!\n", file->basename);
				continue;
			}

			for (i = 0 ; i < file->dsc->expected.names.count ; i++) {
				verify_sourcefile_checksums(file->dsc, i,
						file->fullfilename);
			}
		}
		// TODO: check .deb files
	}
	return RET_OK;
}

static bool isarg(int argc, char **argv, const char *name) {
	while (argc > 0) {
		if (strcmp(*argv, name) == 0)
			return true;
		argc--;
		argv++;
	}
	return false;
}

static bool improvedchecksum_supported(const struct changes *c, bool improvedfilehashes[cs_hashCOUNT]) {
	enum checksumtype cs;
	struct fileentry *file;

	for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++) {
		if (!improvedfilehashes[cs])
			continue;
		for (file = c->files; file != NULL ; file = file->next) {
			const char *dummy1, *dummy3;
			size_t dummy2, dummy4;

			if (file->checksumsfromchanges == NULL)
				continue;

			if (!checksums_gethashpart(file->checksumsfromchanges,
						cs,
						&dummy1, &dummy2,
						&dummy3, &dummy4))
				break;
		}
		if (file == NULL)
			return true;
	}
	return false;
}

static bool anyset(bool *list, size_t count) {
	while (count > 0)
		if (list[--count])
			return true;
	return false;
}

static retvalue updatechecksums(const char *changesfilename, struct changes *c, int argc, char **argv) {
	retvalue r;
	struct fileentry *file;
	bool improvedfilehashes[cs_hashCOUNT];

	r = getchecksums(c);
	if (RET_WAS_ERROR(r))
		return r;
	/* first update all .dsc files and perhaps recalculate their checksums*/
	for (file = c->files; file != NULL ; file = file->next) {
		int i;
		bool improvedhash[cs_hashCOUNT];

		if (file->type != ft_DSC)
			continue;

		if (file->dsc == NULL) {
			fprintf(stderr,
"WARNING: Could not read '%s', hopeing the content and its checksums are correct!\n",
					file->basename);
			continue;
		}
		memset(improvedhash, 0, sizeof(improvedhash));

		assert (file->fullfilename != NULL);
		for (i = 0 ; i < file->dsc->expected.names.count ; i++) {
			const char *basefilename = file->dsc->expected.names.values[i];
			const struct fileentry *sfile = file->dsc->uplink[i];
			struct checksums **expected_p = &file->dsc->expected.checksums[i];
			const struct checksums * const expected = *expected_p;
			const char *hashes1, *hashes2;
			size_t dummy;
			bool doit;
			bool improves;

			assert (expected != NULL);
			assert (basefilename != NULL);

			doit = isarg(argc, argv, basefilename);
			if (argc > 0 && !doit)
				continue;

			assert (sfile != NULL);
			if (sfile->checksumsfromchanges == NULL) {
				if (!doit) {
					fprintf(stderr,
"Not checking/updating '%s' as not in .changes and not specified on command line.\n",
						basefilename);
					continue;
				}
				if (sfile->realchecksums == NULL) {
					fprintf(stderr,
"WARNING: Could not check checksums of '%s'!\n", basefilename);
					continue;
				}
			} else {
				if (sfile->realchecksums == NULL) {
					fprintf(stderr,
"WARNING: Could not check checksums of '%s'!\n",
							basefilename);
					continue;
				}
			}

			if (checksums_check(expected, sfile->realchecksums,
						&improves)) {
				if (!improves) {
					/* already correct */
					continue;
				}
				/* future versions might be able to store them
				 * in the dsc */
				r = checksums_combine(expected_p,
						sfile->realchecksums,
						improvedhash);
				if (RET_WAS_ERROR(r))
					return r;
				continue;
			}
			r = checksums_getcombined(expected, &hashes1, &dummy);
			if (!RET_IS_OK(r))
				hashes1 = "<unknown>";
			r = checksums_getcombined(sfile->realchecksums,
					&hashes2, &dummy);
			if (!RET_IS_OK(r))
				hashes2 = "<unknown>";
			fprintf(stderr,
"Going to update '%s' in '%s'\nfrom '%s'\nto   '%s'.\n",
					basefilename, file->fullfilename,
					hashes1, hashes2);
			checksums_free(*expected_p);
			*expected_p = checksums_dup(sfile->realchecksums);
			if (FAILEDTOALLOC(*expected_p))
				return RET_ERROR_OOM;
			file->dsc->modified = true;
		}
		checksumsarray_resetunsupported(&file->dsc->expected,
				improvedhash);
		if (file->dsc->modified | anyset(improvedhash, cs_hashCOUNT)) {
			r = write_dsc_file(file, DSC_WRITE_FILES);
			if (RET_WAS_ERROR(r))
				return r;
		}
	}
	memset(improvedfilehashes, 0, sizeof(improvedfilehashes));
	for (file = c->files; file != NULL ; file = file->next) {
		bool improves;
		const char *hashes1, *hashes2;
		size_t dummy;

		if (file->checksumsfromchanges == NULL)
			/* nothing to check here */
			continue;
		if (file->realchecksums == NULL) {
			fprintf(stderr,
"WARNING: Could not check checksums of '%s'! Leaving it as it is.\n",
					file->basename);
			continue;
		}
		if (checksums_check(file->checksumsfromchanges,
					file->realchecksums, &improves)) {
			if (!improves)
				continue;
			/* future versions might store sha sums in .changes: */
			r = checksums_combine(&file->checksumsfromchanges,
				file->realchecksums, improvedfilehashes);
			if (RET_WAS_ERROR(r))
				return r;
			continue;
		}
		r = checksums_getcombined(file->checksumsfromchanges,
				&hashes1, &dummy);
		if (!RET_IS_OK(r))
			hashes1 = "<unknown>";
		r = checksums_getcombined(file->realchecksums,
				&hashes2, &dummy);
		if (!RET_IS_OK(r))
			hashes2 = "<unknown>";
		fprintf(stderr,
"Going to update '%s' in '%s'\nfrom '%s'\nto   '%s'.\n",
				file->basename, changesfilename,
				hashes1, hashes2);
		checksums_free(file->checksumsfromchanges);
		file->checksumsfromchanges = checksums_dup(file->realchecksums);
		if (FAILEDTOALLOC(file->checksumsfromchanges))
			return RET_ERROR_OOM;
		c->modified = true;
	}
	if (c->modified) {
		return write_changes_file(changesfilename, c,
				CHANGES_WRITE_FILES, false);
	} else if (improvedchecksum_supported(c, improvedfilehashes)) {
		return write_changes_file(changesfilename, c,
				CHANGES_WRITE_FILES, false);
	} else
		return RET_NOTHING;
}

static retvalue includeallsources(const char *changesfilename, struct changes *c, int argc, char **argv) {
	struct fileentry *file;

	for (file = c->files; file != NULL ; file = file->next) {
		int i;

		if (file->type != ft_DSC)
			continue;

		if (file->dsc == NULL) {
			fprintf(stderr,
"WARNING: Could not read '%s', thus cannot determine if it depends on unlisted files!\n",
					file->basename);
			continue;
		}
		assert (file->fullfilename != NULL);
		for (i = 0 ; i < file->dsc->expected.names.count ; i++) {
			const char *basefilename = file->dsc->expected.names.values[i];
			struct fileentry * const sfile = file->dsc->uplink[i];
			struct checksums **expected_p = &file->dsc->expected.checksums[i];
			const struct checksums * const expected = *expected_p;

			assert (expected != NULL);
			assert (basefilename != NULL);
			assert (sfile != NULL);

			if (sfile->checksumsfromchanges != NULL)
				continue;

			if (argc > 0 && !isarg(argc, argv, basefilename))
				continue;

			sfile->checksumsfromchanges = checksums_dup(expected);
			if (FAILEDTOALLOC(sfile->checksumsfromchanges))
				return RET_ERROR_OOM;
			/* copy section and priority information from the dsc */
			if (sfile->section == NULL && file->section != NULL) {
				sfile->section = strdup(file->section);
				if (FAILEDTOALLOC(sfile->section))
					return RET_ERROR_OOM;
			}
			if (sfile->priority == NULL && file->priority != NULL) {
				sfile->priority = strdup(file->priority);
				if (FAILEDTOALLOC(sfile->priority))
					return RET_ERROR_OOM;
			}

			fprintf(stderr, "Going to add '%s' to '%s'.\n",
					basefilename, changesfilename);
			c->modified = true;
		}
	}
	if (c->modified) {
		return write_changes_file(changesfilename, c,
				CHANGES_WRITE_FILES, false);
	} else
		return RET_NOTHING;
}

static retvalue adddsc(struct changes *c, const char *dscfilename, const struct strlist *searchpath) {
	retvalue r;
	struct fileentry *f;
	struct dscfile *dsc;
	char *fullfilename, *basefilename;
	char *origdirectory;
	const char *v;
	int i;

	r = findfile(dscfilename, c, searchpath, ".", &fullfilename);
	if (RET_WAS_ERROR(r))
		return r;
	if (r == RET_NOTHING) {
		fprintf(stderr, "Cannot find '%s'!\n", dscfilename);
		return RET_ERROR_MISSING;
	}
	r = read_dscfile(fullfilename, &dsc);
	if (r == RET_NOTHING) {
		fprintf(stderr, "Error reading '%s'!\n", fullfilename);
		r = RET_ERROR;
	}
	if (RET_WAS_ERROR(r)) {
		free(fullfilename);
		return r;
	}
	if (dsc->name == NULL || dsc->version == NULL) {
		if (dsc->name == NULL)
			fprintf(stderr, "Could not extract name of '%s'!\n",
					fullfilename);
		else
			fprintf(stderr, "Could not extract version of '%s'!\n",
					fullfilename);
		dscfile_free(dsc);
		free(fullfilename);
		return RET_ERROR;
	}
	if (c->name != NULL) {
		if (strcmp(c->name, dsc->name) != 0) {
			fprintf(stderr,
"ERROR: '%s' lists source '%s' while '%s' already is '%s'!\n",
					fullfilename, dsc->name,
					c->filename, c->name);
			dscfile_free(dsc);
			free(fullfilename);
			return RET_ERROR;
		}
	} else {
		c->name = strdup(dsc->name);
		if (FAILEDTOALLOC(c->name)) {
			dscfile_free(dsc);
			free(fullfilename);
			return RET_ERROR_OOM;
		}
	}
	if (c->version != NULL) {
		if (strcmp(c->version, dsc->version) != 0)
			fprintf(stderr,
"WARNING: '%s' lists version '%s' while '%s' already lists '%s'!\n",
					fullfilename, dsc->version,
					c->filename, c->version);
	} else {
		c->version = strdup(dsc->version);
		if (FAILEDTOALLOC(c->version)) {
			dscfile_free(dsc);
			free(fullfilename);
			return RET_ERROR_OOM;
		}
	}
	// TODO: make sure if the .changes name/version are modified they will
	// also be written...
	v = strchr(dsc->version, ':');
	if (v != NULL)
		v++;
	else
		v = dsc->version;
	basefilename = mprintf("%s_%s.dsc", dsc->name, v);
	if (FAILEDTOALLOC(basefilename)) {
		dscfile_free(dsc);
		free(fullfilename);
		return RET_ERROR_OOM;
	}

	r = dirs_getdirectory(fullfilename, &origdirectory);
	if (RET_WAS_ERROR(r)) {
		dscfile_free(dsc);
		free(origdirectory);
		free(fullfilename);
		return r;
	}

	// TODO: add rename/copy option to be activated when old and new
	// basefilename differ

	r = add_file(c, basefilename, fullfilename, ft_DSC, &f);
	if (RET_WAS_ERROR(r)) {
		dscfile_free(dsc);
		free(origdirectory);
		return r;
	}
	if (r == RET_NOTHING) {
		fprintf(stderr,
"ERROR: '%s' already contains a file of the same name!\n",
				c->filename);
		dscfile_free(dsc);
		free(origdirectory);
		// TODO: check instead if it is already the same...
		return RET_ERROR;
	}
	/* f owns dsc, fullfilename and basefilename now */
	f->dsc = dsc;

	/* now include the files needed by this */
	for (i =  0 ; i < dsc->expected.names.count ; i++) {
		struct fileentry *file;
		const char *b = dsc->expected.names.values[i];
		const struct checksums *checksums = dsc->expected.checksums[i];

		file = add_fileentry(c, b, strlen(b), true, NULL);
		if (FAILEDTOALLOC(file)) {
			free(origdirectory);
			return RET_ERROR_OOM;
		}
		dsc->uplink[i] = file;
		/* make them appear in the .changes file if not there: */
		// TODO: add missing checksums here from file
		if (file->checksumsfromchanges == NULL) {
			file->checksumsfromchanges = checksums_dup(checksums);
			if (FAILEDTOALLOC(file->checksumsfromchanges)) {
				free(origdirectory);
				return RET_ERROR_OOM;
			}
		} // TODO: otherwise warn if not the same
	}

	c->modified = true;
	r = checksums_read(f->fullfilename, &f->realchecksums);
	if (RET_WAS_ERROR(r)) {
		free(origdirectory);
		return r;
	}
	f->checksumsfromchanges = checksums_dup(f->realchecksums);
	if (FAILEDTOALLOC(f->checksumsfromchanges)) {
		free(origdirectory);
		return RET_ERROR_OOM;
	}
	/* for a "extended" dsc with section or priority */
	if (dsc->section != NULL) {
		free(f->section);
		f->section = strdup(dsc->section);
		if (FAILEDTOALLOC(f->section)) {
			free(origdirectory);
			return RET_ERROR_OOM;
		}
	}
	if (dsc->priority != NULL) {
		free(f->priority);
		f->priority = strdup(dsc->priority);
		if (FAILEDTOALLOC(f->priority)) {
			free(origdirectory);
			return RET_ERROR_OOM;
		}
	}
	if (f->section == NULL || f->priority == NULL) {
		struct sourceextraction *extraction;
		int j;

		extraction = sourceextraction_init(
				(f->section == NULL)?&f->section:NULL,
				(f->priority == NULL)?&f->priority:NULL);
		if (FAILEDTOALLOC(extraction)) {
			free(origdirectory);
			return RET_ERROR_OOM;
		}
		for (j = 0 ; j < dsc->expected.names.count ; j++) {
			sourceextraction_setpart(extraction, j,
					dsc->expected.names.values[j]);
		}
		while (sourceextraction_needs(extraction, &j)) {
			if (dsc->uplink[j]->fullfilename == NULL) {
				/* look for file */
				r = findfile(dsc->expected.names.values[j], c,
					searchpath, origdirectory,
					&dsc->uplink[j]->fullfilename);
				if (RET_WAS_ERROR(r)) {
					sourceextraction_abort(extraction);
					free(origdirectory);
					return r;
				}
				if (r == RET_NOTHING ||
				    dsc->uplink[j]->fullfilename == NULL)
					break;
			}
			r = sourceextraction_analyse(extraction,
					dsc->uplink[j]->fullfilename);
			if (RET_WAS_ERROR(r)) {
				sourceextraction_abort(extraction);
				free(origdirectory);
				return r;
			}
		}
		r = sourceextraction_finish(extraction);
		if (RET_WAS_ERROR(r)) {
			free(origdirectory);
			return r;
		}
	}
	free(origdirectory);
	/* update information in the main .changes file if not there already */
	if (c->maintainer == NULL && dsc->maintainer != NULL) {
		c->maintainer = strdup(dsc->maintainer);
		if (FAILEDTOALLOC(c->maintainer))
			return RET_ERROR_OOM;
	}
	if (!strlist_in(&c->architectures, "source")) {
		r = strlist_add_dup(&c->architectures, "source");
		if (RET_WAS_ERROR(r))
			return r;
	}
	return RET_OK;
}

static retvalue adddscs(const char *changesfilename, struct changes *c, int argc, char **argv, const struct strlist *searchpath, bool fakefields) {
	if (argc <= 0) {
		fprintf(stderr,
"Filenames of .dsc files to include expected!\n");
		return RET_ERROR;
	}
	while (argc > 0) {
		retvalue r = adddsc(c, argv[0], searchpath);
		if (RET_WAS_ERROR(r))
			return r;
		argc--; argv++;
	}
	if (c->modified) {
		return write_changes_file(changesfilename, c,
				CHANGES_WRITE_ALL, fakefields);
	} else
		return RET_NOTHING;
}

static retvalue adddeb(struct changes *c, const char *debfilename, const struct strlist *searchpath) {
	retvalue r;
	struct fileentry *f;
	struct binaryfile *deb;
	const char *packagetype;
	enum filetype type;
	char *fullfilename, *basefilename;
	const char *v;

	r = findfile(debfilename, c, searchpath, ".", &fullfilename);
	if (RET_WAS_ERROR(r))
		return r;
	if (r == RET_NOTHING) {
		fprintf(stderr, "Cannot find '%s'!\n", debfilename);
		return RET_ERROR_MISSING;
	}
	r = read_binaryfile(fullfilename, &deb);
	if (r == RET_NOTHING) {
		fprintf(stderr, "Error reading '%s'!\n", fullfilename);
		r = RET_ERROR;
	}
	if (RET_WAS_ERROR(r)) {
		free(fullfilename);
		return r;
	}
	// TODO: check if there are other things but the name to distinguish them
	if (strlen(fullfilename) > 5 &&
			strcmp(fullfilename+strlen(fullfilename)-5, ".udeb") == 0) {
		packagetype = "udeb";
		type = ft_UDEB;
	} else {
		packagetype = "deb";
		type = ft_DEB;
	}
	if (deb->name == NULL || deb->version == NULL || deb->architecture == NULL) {
		if (deb->name == NULL)
			fprintf(stderr,
"Could not extract packagename of '%s'!\n",
					fullfilename);
		else if (deb->version == NULL)
			fprintf(stderr,
"Could not extract version of '%s'!\n",
					fullfilename);
		else
			fprintf(stderr,
"Could not extract architecture of '%s'!\n",
					fullfilename);
		binaryfile_free(deb);
		free(fullfilename);
		return RET_ERROR;
	}
	if (c->name != NULL) {
		const char *sourcename;
		if (deb->sourcename != NULL)
			sourcename = deb->sourcename;
		else
			sourcename = deb->name;
		if (strcmp(c->name, sourcename) != 0) {
			fprintf(stderr,
"ERROR: '%s' lists source '%s' while '%s' already is '%s'!\n",
					fullfilename, sourcename,
					c->filename, c->name);
			binaryfile_free(deb);
			free(fullfilename);
			return RET_ERROR;
		}
	} else {
		if (deb->sourcename != NULL)
			c->name = strdup(deb->sourcename);
		else
			c->name = strdup(deb->name);
		if (FAILEDTOALLOC(c->name)) {
			binaryfile_free(deb);
			free(fullfilename);
			return RET_ERROR_OOM;
		}
	}
	if (c->version != NULL) {
		const char *sourceversion;
		if (deb->sourceversion != NULL)
			sourceversion = deb->sourceversion;
		else
			sourceversion = deb->version;
		if (strcmp(c->version, sourceversion) != 0)
			fprintf(stderr,
"WARNING: '%s' lists source version '%s' while '%s' already lists '%s'!\n",
					fullfilename, sourceversion,
					c->filename, c->version);
	} else {
		if (deb->sourceversion != NULL)
			c->version = strdup(deb->sourceversion);
		else
			c->version = strdup(deb->version);
		if (FAILEDTOALLOC(c->version)) {
			binaryfile_free(deb);
			free(fullfilename);
			return RET_ERROR_OOM;
		}
	}
	// TODO: make sure if the .changes name/version are modified they will
	// also be written...
	v = strchr(deb->version, ':');
	if (v != NULL)
		v++;
	else
		v = deb->version;
	basefilename = mprintf("%s_%s_%s.%s", deb->name, v, deb->architecture,
			packagetype);
	if (FAILEDTOALLOC(basefilename)) {
		binaryfile_free(deb);
		free(fullfilename);
		return RET_ERROR_OOM;
	}

	// TODO: add rename/copy option to be activated when old and new
	// basefilename differ

	r = add_file(c, basefilename, fullfilename, type, &f);
	if (RET_WAS_ERROR(r)) {
		binaryfile_free(deb);
		return r;
	}
	if (r == RET_NOTHING) {
		fprintf(stderr,
"ERROR: '%s' already contains a file of the same name!\n",
				c->filename);
		binaryfile_free(deb);
		// TODO: check instead if it is already the same...
		return RET_ERROR;
	}
	/* f owns deb, fullfilename and basefilename now */
	f->deb = deb;
	deb->binary = get_binary(c, deb->name, strlen(deb->name));
	if (FAILEDTOALLOC(deb->binary))
		return RET_ERROR_OOM;
	deb->next = deb->binary->files;
	deb->binary->files = deb;
	deb->binary->missedinheader = false;
	c->modified = true;
	r = checksums_read(f->fullfilename, &f->realchecksums);
	if (RET_WAS_ERROR(r))
		return r;
	f->checksumsfromchanges = checksums_dup(f->realchecksums);
	if (FAILEDTOALLOC(f->checksumsfromchanges))
		return RET_ERROR_OOM;
	if (deb->shortdescription != NULL) {
		if (deb->binary->description == NULL) {
			deb->binary->description = strdup(deb->shortdescription);
			deb->binary->missedinheader = false;
		} else if (strcmp(deb->binary->description,
		                  deb->shortdescription) != 0) {
			fprintf(stderr,
"WARNING: '%s' already lists a different description for '%s' than contained in '%s'!\n",
					c->filename, deb->name, fullfilename);
		}
	}
	if (deb->section != NULL) {
		free(f->section);
		f->section = strdup(deb->section);
	}
	if (deb->priority != NULL) {
		free(f->priority);
		f->priority = strdup(deb->priority);
	}
	if (c->maintainer == NULL && deb->maintainer != NULL) {
		c->maintainer = strdup(deb->maintainer);
	}
	if (deb->architecture != NULL &&
			!strlist_in(&c->architectures, deb->architecture)) {
		strlist_add_dup(&c->architectures, deb->architecture);
	}
	return RET_OK;
}

static retvalue adddebs(const char *changesfilename, struct changes *c, int argc, char **argv, const struct strlist *searchpath, bool fakefields) {
	if (argc <= 0) {
		fprintf(stderr,
"Filenames of .deb files to include expected!\n");
		return RET_ERROR;
	}
	while (argc > 0) {
		retvalue r = adddeb(c, argv[0], searchpath);
		if (RET_WAS_ERROR(r))
			return r;
		argc--; argv++;
	}
	if (c->modified) {
		return write_changes_file(changesfilename, c,
				CHANGES_WRITE_ALL, fakefields);
	} else
		return RET_NOTHING;
}

static retvalue addrawfile(struct changes *c, const char *filename, const struct strlist *searchpath) {
	retvalue r;
	struct fileentry *f;
	char *fullfilename, *basefilename;
	struct checksums *checksums;

	r = findfile(filename, c, searchpath, ".", &fullfilename);
	if (RET_WAS_ERROR(r))
		return r;
	if (r == RET_NOTHING) {
		fprintf(stderr, "Cannot find '%s'!\n", filename);
		return RET_ERROR_MISSING;
	}
	basefilename = strdup(dirs_basename(filename));
	if (FAILEDTOALLOC(basefilename)) {
		free(fullfilename);
		return RET_ERROR_OOM;
	}
	r = checksums_read(fullfilename, &checksums);
	if (RET_WAS_ERROR(r)) {
		free(fullfilename);
		free(basefilename);
		return r;
	}
	r = add_file(c, basefilename, fullfilename, ft_UNKNOWN, &f);
	// fullfilename and basefilename now belong to *f or are already free'd
	basefilename = NULL;
	fullfilename = NULL;
	if (RET_WAS_ERROR(r)) {
		checksums_free(checksums);
		return r;
	}
	if (r == RET_NOTHING) {

		assert (f != NULL);

		if (f->checksumsfromchanges != NULL) {
			/* already listed in .changes */

			if (!checksums_check(f->checksumsfromchanges, checksums,
					       NULL)) {
				fprintf(stderr,
"ERROR: '%s' already contains a file with name '%s' but different checksums!\n",
						c->filename, f->basename);
				checksums_free(checksums);
				return RET_ERROR;
			}
			printf(
"'%s' already lists '%s' with same checksums. Doing nothing.\n",
					c->filename, f->basename);
			checksums_free(checksums);
			return RET_NOTHING;
		} else {
			/* file already expected by some other part (e.g. a .dsc) */

			// TODO: find out whom this files belong to and warn if different
		}
	}

	c->modified = true;
	assert (f->checksumsfromchanges == NULL);
	f->checksumsfromchanges = checksums;
	checksums = NULL;
	if (f->realchecksums == NULL)
		f->realchecksums = checksums_dup(f->checksumsfromchanges);
	if (FAILEDTOALLOC(f->realchecksums))
		return RET_ERROR_OOM;
	return RET_OK;
}

static retvalue addrawfiles(const char *changesfilename, struct changes *c, int argc, char **argv, const struct strlist *searchpath, bool fakefields) {
	if (argc <= 0) {
		fprintf(stderr,
"Filenames of files to add (without further parsing) expected!\n");
		return RET_ERROR;
	}
	while (argc > 0) {
		retvalue r = addrawfile(c, argv[0], searchpath);
		if (RET_WAS_ERROR(r))
			return r;
		argc--; argv++;
	}
	if (c->modified) {
		return write_changes_file(changesfilename, c,
				CHANGES_WRITE_FILES, fakefields);
	} else
		return RET_NOTHING;
}

static retvalue addfiles(const char *changesfilename, struct changes *c, int argc, char **argv, const struct strlist *searchpath, bool fakefields) {
	if (argc <= 0) {
		fprintf(stderr, "Filenames of files to add expected!\n");
		return RET_ERROR;
	}
	while (argc > 0) {
		retvalue r;
		const char *filename = argv[0];
		size_t l = strlen(filename);

		if ((l > 4 && strcmp(filename+l-4, ".deb") == 0) ||
		    (l > 5 && strcmp(filename+l-5, ".udeb") == 0))
			r = adddeb(c, filename, searchpath);
		else if ((l > 4 && strcmp(filename+l-4, ".dsc") == 0))
			r = adddsc(c, filename, searchpath);
		else
			r = addrawfile(c, argv[0], searchpath);
		if (RET_WAS_ERROR(r))
			return r;
		argc--; argv++;
	}
	if (c->modified) {
		return write_changes_file(changesfilename, c,
				CHANGES_WRITE_ALL, fakefields);
	} else
		return RET_NOTHING;
}

static retvalue dumbremovefiles(const char *changesfilename, struct changes *c, int argc, char **argv) {
	if (argc <= 0) {
		fprintf(stderr,
"Filenames of files to remove (without further parsing) expected!\n");
		return RET_ERROR;
	}
	while (argc > 0) {
		struct fileentry **fp;
		/*@null@*/ struct fileentry *f;

		fp = find_fileentry(c, argv[0], strlen(argv[0]), NULL);
		f = *fp;
		if (f == NULL) {
			fprintf(stderr,
"Not removing '%s' as not listed in '%s'!\n",
					argv[0], c->filename);
		} else if (f->checksumsfromchanges != NULL) {
			/* removing its checksums makes it vanish from the
			 * .changes file generated, while still keeping pointers
			 * from other files intact */
			checksums_free(f->checksumsfromchanges);
			f->checksumsfromchanges = NULL;
			c->modified = true;
		}
		argc--; argv++;
	}
	if (c->modified) {
		return write_changes_file(changesfilename, c,
				CHANGES_WRITE_FILES, false);
	} else
		return RET_NOTHING;
}

static retvalue setdistribution(const char *changesfilename, struct changes *c, int argc, char **argv) {
	retvalue r;
	struct strlist distributions;
	int i;

	if (argc <= 0) {
		fprintf(stderr, "expected Distribution name to set!\n");
		return RET_ERROR;
	}
	r = strlist_init_n(argc, &distributions);
	if (RET_WAS_ERROR(r))
		return r;
	for (i = 0 ; i < argc ; i++) {
		r = strlist_add_dup(&distributions, argv[i]);
		if (RET_WAS_ERROR(r)) {
			strlist_done(&distributions);
			return r;
		}
	}
	strlist_done(&c->distributions);
	strlist_move(&c->distributions, &distributions);
	return write_changes_file(changesfilename, c,
			CHANGES_WRITE_DISTRIBUTIONS, false);
}

static int execute_command(int argc, char **argv, const char *changesfilename, const struct strlist *searchpath, bool file_exists, bool create_file, bool fakefields, struct changes *changesdata) {
	const char *command = argv[0];
	retvalue r;

	assert (argc > 0);

	if (strcasecmp(command, "verify") == 0) {
		if (argc > 1) {
			fprintf(stderr, "Too many arguments!\n");
			r = RET_ERROR;
		} else if (file_exists)
			r = verify(changesfilename, changesdata);
		else {
			fprintf(stderr, "No such file '%s'!\n",
					changesfilename);
			r = RET_ERROR;
		}
	} else if (strcasecmp(command, "updatechecksums") == 0) {
		if (file_exists)
			r = updatechecksums(changesfilename, changesdata,
					argc-1, argv+1);
		else {
			fprintf(stderr, "No such file '%s'!\n",
					changesfilename);
			r = RET_ERROR;
		}
	} else if (strcasecmp(command, "includeallsources") == 0) {
		if (file_exists)
			r = includeallsources(changesfilename, changesdata,
					argc-1, argv+1);
		else {
			fprintf(stderr, "No such file '%s'!\n",
					changesfilename);
			r = RET_ERROR;
		}
	} else if (strcasecmp(command, "addrawfile") == 0) {
		if (file_exists || create_file)
			r = addrawfiles(changesfilename, changesdata,
					argc-1, argv+1, searchpath, fakefields);
		else {
			fprintf(stderr, "No such file '%s'!\n",
					changesfilename);
			r = RET_ERROR;
		}
	} else if (strcasecmp(command, "adddsc") == 0) {
		if (file_exists || create_file)
			r = adddscs(changesfilename, changesdata,
					argc-1, argv+1, searchpath, fakefields);
		else {
			fprintf(stderr, "No such file '%s'!\n",
					changesfilename);
			r = RET_ERROR;
		}
	} else if (strcasecmp(command, "adddeb") == 0) {
		if (file_exists || create_file)
			r = adddebs(changesfilename, changesdata,
					argc-1, argv+1, searchpath, fakefields);
		else {
			fprintf(stderr, "No such file '%s'!\n",
					changesfilename);
			r = RET_ERROR;
		}
	} else if (strcasecmp(command, "add") == 0) {
		if (file_exists || create_file)
			r = addfiles(changesfilename, changesdata,
					argc-1, argv+1, searchpath, fakefields);
		else {
			fprintf(stderr, "No such file '%s'!\n",
					changesfilename);
			r = RET_ERROR;
		}
	} else if (strcasecmp(command, "setdistribution") == 0) {
		if (file_exists)
			r = setdistribution(changesfilename, changesdata,
					argc-1, argv+1);
		else {
			fprintf(stderr, "No such file '%s'!\n",
					changesfilename);
			r = RET_ERROR;
		}
	} else if (strcasecmp(command, "dumbremove") == 0) {
		if (file_exists)
			r = dumbremovefiles(changesfilename, changesdata,
					argc-1, argv+1);
		else {
			fprintf(stderr, "No such file '%s'!\n",
					changesfilename);
			r = RET_ERROR;
		}
	} else {
		fprintf(stderr, "Unknown command '%s'\n", command);
		r = RET_ERROR;
	}
	return r;
}

static retvalue splitpath(struct strlist *list, const char *path) {
	retvalue r;
	const char *next;

	while ((next = index(path, ':')) != NULL) {
		if (next > path) {
			char *dir = strndup(path, next-path);
			if (FAILEDTOALLOC(dir)) {
				return RET_ERROR_OOM;
			}
			r = strlist_add(list, dir);
			if (RET_WAS_ERROR(r))
				return r;
		}
		path = next+1;
	}
	return strlist_add_dup(list, path);
}

int main(int argc, char *argv[]) {
	static int longoption = 0;
	static const struct option longopts[] = {
		{"help", no_argument, NULL, 'h'},
		{"create", no_argument, NULL, 'C'},
		{"create-with-all-fields", no_argument, &longoption, 6},
		{"searchpath", required_argument, NULL, 's'},
		{"gunzip", required_argument, &longoption, 1},
		{"bunzip2", required_argument, &longoption, 2},
		{"unlzma", required_argument, &longoption, 3},
		{"unxz", required_argument, &longoption, 4},
		{"lunzip", required_argument, &longoption, 5},
		{"unzstd", required_argument, &longoption, 7},
		{NULL, 0, NULL, 0},
	};
	int c;
	const char *changesfilename;
	bool file_exists;
	bool create_file = false;
	bool all_fields = false;
	struct strlist searchpath;
	struct changes *changesdata;
	char *gunzip = NULL, *bunzip2 = NULL, *unlzma = NULL,
	*unxz = NULL, *lunzip = NULL, *unzstd = NULL;
	retvalue r;

	strlist_init(&searchpath);

	while ((c = getopt_long(argc, argv, "+hi:s:", longopts, NULL)) != -1) {
		switch (c) {
			case '\0':
				switch (longoption) {
					case 1:
						gunzip = strdup(optarg);
						break;
					case 2:
						bunzip2 = strdup(optarg);
						break;
					case 3:
						unlzma = strdup(optarg);
						break;
					case 4:
						unxz = strdup(optarg);
						break;
					case 5:
						lunzip = strdup(optarg);
						break;
					case 7:
						unzstd = strdup(optarg);
						break;
					case 6:
						create_file = true;
						all_fields = true;
						break;
				}
				break;
			case 'h':
				about(true);
			case 'C':
				create_file = true;
				break;
			case 's':
				r = splitpath(&searchpath, optarg);
				if (RET_WAS_ERROR(r)) {
					if (r == RET_ERROR_OOM)
						fprintf(stderr,
"Out of memory!\n");
					exit(EXIT_FAILURE);
				}
				break;
		}
	}
	if (argc - optind < 2) {
		about(false);
	}
	signature_init(false);
	uncompressions_check(gunzip, bunzip2, unlzma, unxz, lunzip, unzstd);

	changesfilename = argv[optind];
	if (strcmp(changesfilename, "-") != 0 &&
			!endswith(changesfilename, ".changes")) {
		fprintf(stderr, "first argument not ending with '.changes'\n");
		exit(EXIT_FAILURE);
	}
	file_exists = isregularfile(changesfilename);
	if (file_exists) {
		char *changes;

		r = signature_readsignedchunk(changesfilename, changesfilename,
				&changes, NULL, NULL);
		if (!RET_IS_OK(r)) {
			signatures_done();
			if (r == RET_ERROR_OOM)
				fprintf(stderr, "Out of memory!\n");
			exit(EXIT_FAILURE);
		}
		r = parse_changes(changesfilename, changes,
				&changesdata, &searchpath);
		if (RET_IS_OK(r))
			changesdata->control = changes;
		else {
			free(changes);
			changesdata = NULL;
		}
	} else {
		changesdata = zNEW(struct changes);
		if (FAILEDTOALLOC(changesdata))
			r = RET_ERROR_OOM;
		else {
			changesdata->filename = strdup(changesfilename);
			if (FAILEDTOALLOC(changesdata->filename))
				r = RET_ERROR_OOM;
			else
				r = dirs_getdirectory(changesfilename,
						&changesdata->basedir);
		}
	}

	if (!RET_WAS_ERROR(r)) {
		argc -= (optind+1);
		argv += (optind+1);
		r = execute_command(argc, argv, changesfilename, &searchpath,
		                    file_exists, create_file, all_fields,
				    changesdata);
	}
	changes_free(changesdata);

	signatures_done();
	if (RET_IS_OK(r))
		exit(EXIT_SUCCESS);
	if (r == RET_ERROR_OOM)
		fprintf(stderr, "Out of memory!\n");
	exit(EXIT_FAILURE);
}
