/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2007,2009,2012,2016 Bernhard R. Link
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
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <time.h>
#include <zlib.h>
#ifdef HAVE_LIBBZ2
#include <bzlib.h>
#endif
#ifdef HAVE_LIBLZMA
#include <lzma.h>
#endif
#define CHECKSUMS_CONTEXT visible
#include "error.h"
#include "ignore.h"
#include "mprintf.h"
#include "strlist.h"
#include "filecntl.h"
#include "chunks.h"
#include "checksums.h"
#include "dirs.h"
#include "names.h"
#include "signature.h"
#include "distribution.h"
#include "outhook.h"
#include "release.h"

#define INPUT_BUFFER_SIZE 1024
#define GZBUFSIZE 40960
#define BZBUFSIZE 40960
// TODO: what is the correct value here:
#define XZBUFSIZE 40960

struct release {
	/* The base-directory of the distribution we are exporting */
	char *dirofdist;
	/* anything new yet added */
	bool new;
	/* NULL if no snapshot */
	/*@null@*/char *snapshotname;
	/* specific overrides for fakeprefixes or snapshots: */
	/*@null@*/char *fakesuite;
	/*@null@*/char *fakecodename;
	/*@null@*/const char *fakecomponentprefix;
	size_t fakecomponentprefixlen;
	/* the files yet for the list */
	struct release_entry {
		struct release_entry *next;
		char *relativefilename;
		struct checksums *checksums;
		char *fullfinalfilename;
		char *fulltemporaryfilename;
		char *symlinktarget;
		/* name chks NULL NULL NULL: add old filename or virtual file
		 * name chks file file NULL: rename new file and publish
		 * name NULL file file NULL: rename new file
		 * name NULL file NULL NULL: delete if done
		 * name NULL file NULL file: create symlink */
	} *files;
	/* the Release file in preperation
	 * (only valid between _prepare and _finish) */
	struct signedfile *signedfile;
	/* the cache database for old files */
	struct table *cachedb;
};

static void release_freeentry(struct release_entry *e) {
	free(e->relativefilename);
	checksums_free(e->checksums);
	free(e->fullfinalfilename);
	if (!global.keeptemporaries && e->fulltemporaryfilename != NULL)
		(void)unlink(e->fulltemporaryfilename);
	free(e->fulltemporaryfilename);
	free(e->symlinktarget);
	free(e);
}

void release_free(struct release *release) {
	struct release_entry *e;

	free(release->snapshotname);
	free(release->dirofdist);
	free(release->fakesuite);
	free(release->fakecodename);
	while ((e = release->files) != NULL) {
		release->files = e->next;
		release_freeentry(e);
	}
	if (release->signedfile != NULL)
		signedfile_free(release->signedfile);
	if (release->cachedb != NULL) {
		table_close(release->cachedb);
	}
	free(release);
}

const char *release_dirofdist(struct release *release) {
	return release->dirofdist;
}

static retvalue newreleaseentry(struct release *release, /*@only@*/ char *relativefilename,
		/*@only@*/ struct checksums *checksums,
		/*@only@*/ /*@null@*/ char *fullfinalfilename,
		/*@only@*/ /*@null@*/ char *fulltemporaryfilename,
		/*@only@*/ /*@null@*/ char *symlinktarget) {
	struct release_entry *n, *p;

	/* everything has a relative name */
	assert (relativefilename != NULL);
	/* it's either something to do or to publish */
	assert (fullfinalfilename != NULL || checksums != NULL);
	/* if there is something temporary, it has a final place */
	assert (fulltemporaryfilename == NULL || fullfinalfilename != NULL);
	/* a symlink cannot be published (Yet?) */
	assert (symlinktarget == NULL || checksums == NULL);
	/* cannot place a file and a symlink */
	assert (symlinktarget == NULL || fulltemporaryfilename == NULL);
	/* something to publish cannot be a file deletion */
	assert (checksums == NULL
			|| fullfinalfilename == NULL
			|| fulltemporaryfilename != NULL
			|| symlinktarget != NULL);
	n = NEW(struct release_entry);
	if (FAILEDTOALLOC(n)) {
		checksums_free(checksums);
		free(fullfinalfilename);
		free(fulltemporaryfilename);
		free(symlinktarget);
		return RET_ERROR_OOM;
	}
	n->next = NULL;
	n->relativefilename = relativefilename;
	n->checksums = checksums;
	n->fullfinalfilename = fullfinalfilename;
	n->fulltemporaryfilename = fulltemporaryfilename;
	n->symlinktarget = symlinktarget;
	if (release->files == NULL)
		release->files = n;
	else {
		p = release->files;
		while (p->next != NULL)
			p = p->next;
		p->next = n;
	}
	return RET_OK;
}

retvalue release_init(struct release **release, const char *codename, const char *suite, const char *fakecomponentprefix) {
	struct release *n;
	size_t len, suitelen, codenamelen;
	retvalue r;

	if (verbose >= 15)
		fprintf(stderr, "trace: release_init(codename=%s, suite=%s, fakecomponentprefix=%s) called.\n",
		        codename, suite, fakecomponentprefix);

	n = zNEW(struct release);
	if (FAILEDTOALLOC(n))
		return RET_ERROR_OOM;
	n->dirofdist = calc_dirconcat(global.distdir, codename);
	if (FAILEDTOALLOC(n->dirofdist)) {
		free(n);
		return RET_ERROR_OOM;
	}
	if (fakecomponentprefix != NULL) {
		len = strlen(fakecomponentprefix);
		codenamelen = strlen(codename);

		n->fakecomponentprefix = fakecomponentprefix;
		n->fakecomponentprefixlen = len;
		if (codenamelen > len &&
		    codename[codenamelen - len - 1] == '/' &&
		    memcmp(codename + (codenamelen - len),
		           fakecomponentprefix, len) == 0) {
			n->fakecodename = strndup(codename,
					codenamelen - len - 1);
			if (FAILEDTOALLOC(n->fakecodename)) {
				free(n->dirofdist);
				free(n);
				return RET_ERROR_OOM;
			}
		}
		if (suite != NULL && (suitelen = strlen(suite)) > len &&
		    suite[suitelen - len - 1] == '/' &&
		    memcmp(suite + (suitelen - len),
		           fakecomponentprefix, len) == 0) {
			n->fakesuite = strndup(suite,
					suitelen - len - 1);
			if (FAILEDTOALLOC(n->fakesuite)) {
				free(n->fakecodename);
				free(n->dirofdist);
				free(n);
				return RET_ERROR_OOM;
			}
		}
	}
	r = database_openreleasecache(codename, &n->cachedb);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r)) {
		n->cachedb = NULL;
		free(n->fakecodename);
		free(n->fakesuite);
		free(n->dirofdist);
		free(n);
		return r;
	}
	*release = n;
	return RET_OK;
}

retvalue release_initsnapshot(const char *codename, const char *name, struct release **release) {
	struct release *n;

	n = zNEW(struct release);
	if (FAILEDTOALLOC(n))
		return RET_ERROR_OOM;
	n->dirofdist = calc_snapshotbasedir(codename, name);
	if (FAILEDTOALLOC(n->dirofdist)) {
		free(n);
		return RET_ERROR_OOM;
	}
	/* apt only removes the last /... part but we create two,
	 * so stop it generating warnings by faking a suite */
	n->fakesuite = mprintf("%s/snapshots/%s", codename, name);
	if (FAILEDTOALLOC(n->fakesuite)) {
		free(n->dirofdist);
		free(n);
		return RET_ERROR_OOM;
	}
	n->fakecodename = NULL;
	n->fakecomponentprefix = NULL;
	n->fakecomponentprefixlen = 0;
	n->cachedb = NULL;
	n->snapshotname = strdup(name);
	if (n->snapshotname == NULL) {
		free(n->fakesuite);
		free(n->dirofdist);
		free(n);
		return RET_ERROR_OOM;
	}
	*release = n;
	return RET_OK;
}

retvalue release_adddel(struct release *release, /*@only@*/char *reltmpfile) {
	char *filename;

	filename = calc_dirconcat(release->dirofdist, reltmpfile);
	if (FAILEDTOALLOC(filename)) {
		free(reltmpfile);
		return RET_ERROR_OOM;
	}
	return newreleaseentry(release, reltmpfile, NULL, filename, NULL, NULL);
}

retvalue release_addnew(struct release *release, /*@only@*/char *reltmpfile, /*@only@*/char *relfilename) {
	retvalue r;
	char *filename, *finalfilename;
	struct checksums *checksums;

	filename = calc_dirconcat(release->dirofdist, reltmpfile);
	if (FAILEDTOALLOC(filename)) {
		free(reltmpfile);
		free(relfilename);
		return RET_ERROR_OOM;
	}
	free(reltmpfile);
	r = checksums_read(filename, &checksums);
	if (!RET_IS_OK(r)) {
		free(relfilename);
		free(filename);
		return r;
	}
	finalfilename = calc_dirconcat(release->dirofdist, relfilename);
	if (FAILEDTOALLOC(finalfilename)) {
		free(relfilename);
		free(filename);
		checksums_free(checksums);
		return RET_ERROR_OOM;
	}
	release->new = true;
	return newreleaseentry(release, relfilename,
			checksums, finalfilename, filename, NULL);
}

retvalue release_addsilentnew(struct release *release, /*@only@*/char *reltmpfile, /*@only@*/char *relfilename) {
	char *filename, *finalfilename;

	filename = calc_dirconcat(release->dirofdist, reltmpfile);
	if (FAILEDTOALLOC(filename)) {
		free(reltmpfile);
		free(relfilename);
		return RET_ERROR_OOM;
	}
	free(reltmpfile);
	finalfilename = calc_dirconcat(release->dirofdist, relfilename);
	if (FAILEDTOALLOC(finalfilename)) {
		free(relfilename);
		free(filename);
		return RET_ERROR_OOM;
	}
	release->new = true;
	return newreleaseentry(release, relfilename,
			NULL, finalfilename, filename, NULL);
}

retvalue release_addold(struct release *release, /*@only@*/char *relfilename) {
	retvalue r;
	char *filename;
	struct checksums *checksums;

	filename = calc_dirconcat(release->dirofdist, relfilename);
	if (FAILEDTOALLOC(filename)) {
		free(filename);
		return RET_ERROR_OOM;
	}
	r = checksums_read(filename, &checksums);
	free(filename);
	if (!RET_IS_OK(r)) {
		free(relfilename);
		return r;
	}
	return newreleaseentry(release, relfilename,
			checksums, NULL, NULL, NULL);
}

static retvalue release_addsymlink(struct release *release, /*@only@*/char *relfilename, /*@only@*/ char *symlinktarget) {
	char *fullfilename;

	fullfilename = calc_dirconcat(release->dirofdist, relfilename);
	if (FAILEDTOALLOC(fullfilename)) {
		free(symlinktarget);
		free(relfilename);
		return RET_ERROR_OOM;
	}
	release->new = true;
	return newreleaseentry(release, relfilename, NULL,
				fullfilename, NULL, symlinktarget);
}

static char *calc_compressedname(const char *name, enum indexcompression ic) {
	switch (ic) {
		case ic_uncompressed:
			return strdup(name);
		case ic_gzip:
			return calc_addsuffix(name, "gz");
#ifdef HAVE_LIBBZ2
		case ic_bzip2:
			return calc_addsuffix(name, "bz2");
#endif
#ifdef HAVE_LIBLZMA
		case ic_xz:
			return calc_addsuffix(name, "xz");
#endif
		default:
			assert ("Huh?" == NULL);
			return NULL;
	}
}

static retvalue release_usecached(struct release *release,
				const char *relfilename,
				compressionset compressions) {
	retvalue result, r;
	enum indexcompression ic;
	char *filename[ic_count];
	struct checksums *checksums[ic_count];

	memset(filename, 0, sizeof(filename));
	memset(checksums, 0, sizeof(checksums));
	result = RET_OK;

	for (ic = ic_uncompressed ; ic < ic_count ; ic++) {
		if (ic != ic_uncompressed &&
		    (compressions & IC_FLAG(ic)) == 0)
			continue;
		filename[ic] = calc_compressedname(relfilename, ic);
		if (FAILEDTOALLOC(filename[ic])) {
			result = RET_ERROR_OOM;
			break;
		}
	}
	if (RET_IS_OK(result)) {
		/* first look if the there are actual files, in case
		 * the cache still lists them but they got lost */

		for (ic = ic_uncompressed ; ic < ic_count ; ic++) {
			char *fullfilename;

			if ((compressions & IC_FLAG(ic)) == 0)
				continue;
			assert (filename[ic] != NULL);
			fullfilename = calc_dirconcat(release->dirofdist,
					filename[ic]);
			if (FAILEDTOALLOC(fullfilename)) {
				result = RET_ERROR_OOM;
				break;
			}
			if (!isregularfile(fullfilename)) {
				free(fullfilename);
				result = RET_NOTHING;
				break;
			}
			free(fullfilename);
		}
	}
	if (RET_IS_OK(result) && release->cachedb == NULL)
		result = RET_NOTHING;
	if (!RET_IS_OK(result)) {
		for (ic = ic_uncompressed ; ic < ic_count ; ic++)
			free(filename[ic]);
		return result;
	}

	/* now that the files are there look into the cache
	 * what checksums they have. */

	for (ic = ic_uncompressed ; ic < ic_count ; ic++) {
		char *combinedchecksum;

		if (filename[ic] == NULL)
			continue;
		r = table_getrecord(release->cachedb, false, filename[ic],
				&combinedchecksum, NULL);
		if (!RET_IS_OK(r)) {
			result = r;
			break;
		}
		r = checksums_parse(&checksums[ic], combinedchecksum);
		// TODO: handle malformed checksums better?
		free(combinedchecksum);
		if (!RET_IS_OK(r)) {
			result = r;
			break;
		}
	}
	/* some files might not yet have some type of checksum available,
	 * so calculate them (checking the other checksums match...): */
	if (RET_IS_OK(result)) {
		for (ic = ic_uncompressed ; ic < ic_count ; ic++) {
			char *fullfilename;
			if (filename[ic] == NULL)
				continue;
			fullfilename = calc_dirconcat(release->dirofdist,
					filename[ic]);
			if (FAILEDTOALLOC(fullfilename))
				r = RET_ERROR_OOM;
			else
				r = checksums_complete(&checksums[ic],
					fullfilename);
			if (r == RET_ERROR_WRONG_MD5) {
				fprintf(stderr,
"WARNING: '%s' is different from recorded checksums.\n"
"(This was only caught because some new checksum type was not yet available.)\n"
"Triggering recreation of that file.\n", fullfilename);
				r = RET_NOTHING;
			}
			free(fullfilename);
			if (!RET_IS_OK(r)) {
				result = r;
				break;
			}
		}
	}
	if (!RET_IS_OK(result)) {
		for (ic = ic_uncompressed ; ic < ic_count ; ic++) {
			if (filename[ic] == NULL)
				continue;
			free(filename[ic]);
			checksums_free(checksums[ic]);
		}
		return result;
	}
	/* everything found, commit it: */
	result = RET_OK;
	for (ic = ic_uncompressed ; ic < ic_count ; ic++) {
		if (filename[ic] == NULL)
			continue;
		r = newreleaseentry(release, filename[ic],
				checksums[ic],
				NULL, NULL, NULL);
		RET_UPDATE(result, r);
	}
	return result;
}


struct filetorelease {
	retvalue state;
	struct openfile {
		int fd;
		struct checksumscontext context;
		char *relativefilename;
		char *fullfinalfilename;
		char *fulltemporaryfilename;
		char *symlinkas;
	} f[ic_count];
	/* input buffer, to checksum/compress data at once */
	unsigned char *buffer; size_t waiting_bytes;
	/* output buffer for gzip compression */
	unsigned char *gzoutputbuffer; size_t gz_waiting_bytes;
	z_stream gzstream;
#ifdef HAVE_LIBBZ2
	/* output buffer for bzip2 compression */
	char *bzoutputbuffer; size_t bz_waiting_bytes;
	bz_stream bzstream;
#endif
#ifdef HAVE_LIBLZMA
	/* output buffer for bzip2 compression */
	unsigned char *xzoutputbuffer; size_t xz_waiting_bytes;
	lzma_stream xzstream;
#endif
};

void release_abortfile(struct filetorelease *file) {
	enum indexcompression i;

	for (i = ic_uncompressed ; i < ic_count ; i++) {
		if (file->f[i].fd >= 0) {
			(void)close(file->f[i].fd);
			if (file->f[i].fulltemporaryfilename != NULL)
				(void)unlink(file->f[i].fulltemporaryfilename);
		}
		free(file->f[i].relativefilename);
		free(file->f[i].fullfinalfilename);
		free(file->f[i].fulltemporaryfilename);
		free(file->f[i].symlinkas);
	}
	free(file->buffer);
	free(file->gzoutputbuffer);
	if (file->gzstream.next_out != NULL) {
		(void)deflateEnd(&file->gzstream);
	}
#ifdef HAVE_LIBBZ2
	free(file->bzoutputbuffer);
	if (file->bzstream.next_out != NULL) {
		(void)BZ2_bzCompressEnd(&file->bzstream);
	}
#endif
#ifdef HAVE_LIBLZMA
	if (file->xzoutputbuffer != NULL) {
		free(file->xzoutputbuffer);
		lzma_end(&file->xzstream);
	}
#endif
}

bool release_oldexists(struct filetorelease *file) {
	enum indexcompression ic;
	bool hadanything = false;

	for (ic = ic_uncompressed ; ic < ic_count ; ic++) {
		char *f = file->f[ic].fullfinalfilename;

		if (f != NULL) {
			if (isregularfile(f))
				hadanything = true;
			else
				return false;
		}
	}
	return hadanything;
}

static retvalue openfile(const char *dirofdist, struct openfile *f) {

	f->fullfinalfilename = calc_dirconcat(dirofdist, f->relativefilename);
	if (FAILEDTOALLOC(f->fullfinalfilename))
		return RET_ERROR_OOM;
	f->fulltemporaryfilename = calc_addsuffix(f->fullfinalfilename, "new");
	if (FAILEDTOALLOC(f->fulltemporaryfilename))
		return RET_ERROR_OOM;
	(void)unlink(f->fulltemporaryfilename);
	f->fd = open(f->fulltemporaryfilename,
			O_WRONLY|O_CREAT|O_EXCL|O_NOCTTY, 0666);
	if (f->fd < 0) {
		int e = errno;
		fprintf(stderr, "Error %d opening file %s for writing: %s\n",
				e, f->fulltemporaryfilename, strerror(e));
		return RET_ERRNO(e);
	}
	return RET_OK;
}

static retvalue writetofile(struct openfile *file, const unsigned char *data, size_t len) {

	checksumscontext_update(&file->context, data, len);

	if (file->fd < 0)
		return RET_NOTHING;

	while (len > 0) {
		ssize_t written = write(file->fd, data, len);
		if (written >= 0) {
			len -= written;
			data += written;
		} else {
			int e = errno;
			if (e == EAGAIN || e == EINTR)
				continue;
			fprintf(stderr, "Error %d writing to %s: %s\n",
					e, file->fullfinalfilename,
					strerror(e));
			return RET_ERRNO(e);
		}
	}
	return RET_OK;
}

static retvalue initgzcompression(struct filetorelease *f) {
	int zret;

	if ((zlibCompileFlags() & (1<<17)) !=0) {
		fprintf(stderr, "libz compiled without .gz supporting code\n");
		return RET_ERROR;
	}
	f->gzoutputbuffer = malloc(GZBUFSIZE);
	if (FAILEDTOALLOC(f->gzoutputbuffer))
		return RET_ERROR_OOM;
	f->gzstream.next_in = NULL;
	f->gzstream.avail_in = 0;
	f->gzstream.next_out = f->gzoutputbuffer;
	f->gzstream.avail_out = GZBUFSIZE;
	f->gzstream.zalloc = NULL;
	f->gzstream.zfree = NULL;
	f->gzstream.opaque = NULL;
	zret = deflateInit2(&f->gzstream,
			/* Level: 0-9 or Z_DEFAULT_COMPRESSION: */
			Z_DEFAULT_COMPRESSION,
			/* only possibility yet: */
			Z_DEFLATED,
			/* +16 to generate gzip header */
			16 + MAX_WBITS,
			/* how much memory to use 1-9 */
			8,
			/* default or Z_FILTERED or Z_HUFFMAN_ONLY or Z_RLE */
			Z_DEFAULT_STRATEGY
			);
	f->gz_waiting_bytes = GZBUFSIZE - f->gzstream.avail_out;
	if (zret == Z_MEM_ERROR)
		return RET_ERROR_OOM;
	if (zret != Z_OK) {
		if (f->gzstream.msg == NULL) {
			fprintf(stderr, "Error from zlib's deflateInit2: "
					"unknown(%d)\n", zret);
		} else {
			fprintf(stderr, "Error from zlib's deflateInit2: %s\n",
					f->gzstream.msg);
		}
		return RET_ERROR;
	}
	return RET_OK;
}

#ifdef HAVE_LIBBZ2

static retvalue initbzcompression(struct filetorelease *f) {
	int bzret;

	f->bzoutputbuffer = malloc(BZBUFSIZE);
	if (FAILEDTOALLOC(f->bzoutputbuffer))
		return RET_ERROR_OOM;
	f->bzstream.next_in = NULL;
	f->bzstream.avail_in = 0;
	f->bzstream.next_out = f->bzoutputbuffer;
	f->bzstream.avail_out = BZBUFSIZE;
	f->bzstream.bzalloc = NULL;
	f->bzstream.bzfree = NULL;
	f->bzstream.opaque = NULL;
	bzret = BZ2_bzCompressInit(&f->bzstream,
			/* blocksize (1-9) */
			9,
			/* verbosity */
			0,
			/* workFaktor (1-250, 0 = default(30)) */
			0
			);
	if (bzret == BZ_MEM_ERROR)
		return RET_ERROR_OOM;
	if (bzret != BZ_OK) {
		fprintf(stderr, "Error from libbz2's bzCompressInit: "
				"%d\n", bzret);
		return RET_ERROR;
	}
	return RET_OK;
}
#endif

#ifdef HAVE_LIBLZMA

static retvalue initxzcompression(struct filetorelease *f) {
	lzma_ret lret;

	f->xzoutputbuffer = malloc(XZBUFSIZE);
	if (FAILEDTOALLOC(f->xzoutputbuffer))
		return RET_ERROR_OOM;
	memset(&f->xzstream, 0, sizeof(f->xzstream));
	lret = lzma_easy_encoder(&f->xzstream, 9, LZMA_CHECK_CRC64);
	if (lret == LZMA_MEM_ERROR)
		return RET_ERROR_OOM;
	if (lret != LZMA_OK) {
		fprintf(stderr, "Error from liblzma's lzma_easy_encoder: "
				"%d\n", lret);
		return RET_ERROR;
	}
	return RET_OK;
}
#endif


static const char * const ics[ic_count] = { "", ".gz"
#ifdef HAVE_LIBBZ2
       	, ".bz2"
#endif
#ifdef HAVE_LIBLZMA
       	, ".xz"
#endif
};

static inline retvalue setfilename(struct filetorelease *n, const char *relfilename, /*@null@*/const char *symlinkas, enum indexcompression ic) {
	n->f[ic].relativefilename = mprintf("%s%s", relfilename, ics[ic]);
	if (FAILEDTOALLOC(n->f[ic].relativefilename))
		return RET_ERROR_OOM;
	if (symlinkas == NULL)
		return RET_OK;
	/* symlink creation fails horrible if the symlink is not in the base
	 * directory */
	assert (strchr(symlinkas, '/') == NULL);
	n->f[ic].symlinkas = mprintf("%s%s", symlinkas, ics[ic]);
	if (FAILEDTOALLOC(n->f[ic].symlinkas))
		return RET_ERROR_OOM;
	return RET_OK;
}

static inline void warnfilename(struct release *release, const char *relfilename, enum indexcompression ic) {
	char *fullfilename;

	if (IGNORABLE(oldfile))
		return;

	fullfilename = mprintf("%s/%s%s", release->dirofdist,
			relfilename, ics[ic]);
	if (FAILEDTOALLOC(fullfilename))
		return;
	if (isanyfile(fullfilename)) {
		fprintf(stderr, "Possibly left over file '%s'.\n",
				fullfilename);
		if (!ignored[IGN_oldfile]) {
	       		fputs("You might want to delete it or use --ignore=oldfile to no longer get this message.\n", stderr);
			ignored[IGN_oldfile] = true;
		}
	}
	free(fullfilename);
}

static retvalue startfile(struct release *release, const char *filename, /*@null@*/const char *symlinkas, compressionset compressions, bool usecache, struct filetorelease **file) {
	struct filetorelease *n;
	enum indexcompression i;

	if (usecache) {
		retvalue r = release_usecached(release, filename, compressions);
		if (r != RET_NOTHING) {
			if (RET_IS_OK(r))
				return RET_NOTHING;
			return r;
		}
	}

	n = zNEW(struct filetorelease);
	if (FAILEDTOALLOC(n))
		return RET_ERROR_OOM;
	n->buffer = malloc(INPUT_BUFFER_SIZE);
	if (FAILEDTOALLOC(n->buffer)) {
		release_abortfile(n);
		return RET_ERROR_OOM;
	}
	for (i = ic_uncompressed ; i < ic_count ; i ++) {
		n->f[i].fd = -1;
	}
	if ((compressions & IC_FLAG(ic_uncompressed)) != 0) {
		retvalue r;

		r = setfilename(n, filename, symlinkas, ic_uncompressed);
		if (!RET_WAS_ERROR(r))
			r = openfile(release->dirofdist, &n->f[ic_uncompressed]);
		if (RET_WAS_ERROR(r)) {
			release_abortfile(n);
			return r;
		}
	} else {
		/* the uncompressed file always shows up in Release */
		n->f[ic_uncompressed].relativefilename = strdup(filename);
		if (FAILEDTOALLOC(n->f[ic_uncompressed].relativefilename)) {
			release_abortfile(n);
			return RET_ERROR_OOM;
		}
	}

	if ((compressions & IC_FLAG(ic_gzip)) != 0) {
		retvalue r;

		r = setfilename(n, filename, symlinkas, ic_gzip);
		if (!RET_WAS_ERROR(r))
			r = openfile(release->dirofdist, &n->f[ic_gzip]);
		if (RET_WAS_ERROR(r)) {
			release_abortfile(n);
			return r;
		}
		checksumscontext_init(&n->f[ic_gzip].context);
		r = initgzcompression(n);
		if (RET_WAS_ERROR(r)) {
			release_abortfile(n);
			return r;
		}
	}
#ifdef HAVE_LIBBZ2
	if ((compressions & IC_FLAG(ic_bzip2)) != 0) {
		retvalue r;
		r = setfilename(n, filename, symlinkas, ic_bzip2);
		if (!RET_WAS_ERROR(r))
			r = openfile(release->dirofdist, &n->f[ic_bzip2]);
		if (RET_WAS_ERROR(r)) {
			release_abortfile(n);
			return r;
		}
		checksumscontext_init(&n->f[ic_bzip2].context);
		r = initbzcompression(n);
		if (RET_WAS_ERROR(r)) {
			release_abortfile(n);
			return r;
		}
	}
#endif
#ifdef HAVE_LIBLZMA
	if ((compressions & IC_FLAG(ic_xz)) != 0) {
		retvalue r;
		r = setfilename(n, filename, symlinkas, ic_xz);
		if (!RET_WAS_ERROR(r))
			r = openfile(release->dirofdist, &n->f[ic_xz]);
		if (RET_WAS_ERROR(r)) {
			release_abortfile(n);
			return r;
		}
		checksumscontext_init(&n->f[ic_xz].context);
		r = initxzcompression(n);
		if (RET_WAS_ERROR(r)) {
			release_abortfile(n);
			return r;
		}
	}
#endif
	checksumscontext_init(&n->f[ic_uncompressed].context);
	*file = n;
	return RET_OK;
}

retvalue release_startfile(struct release *release, const char *filename, compressionset compressions, bool usecache, struct filetorelease **file) {
	return startfile(release, filename, NULL, compressions, usecache, file);
}

retvalue release_startlinkedfile(struct release *release, const char *filename, const char *symlinkas, compressionset compressions, bool usecache, struct filetorelease **file) {
	return startfile(release, filename, symlinkas, compressions, usecache, file);
}

void release_warnoldfileorlink(struct release *release, const char *filename, compressionset compressions) {
	enum indexcompression i;

	for (i = ic_uncompressed ; i < ic_count ; i ++)
		if ((compressions & IC_FLAG(i)) != 0)
			warnfilename(release, filename, i);
}

static inline char *calc_relative_path(const char *target, const char *linkname) {
	size_t t_len, l_len, common_len, len;
	const char *t, *l;
	int depth;
	char *n, *p;

	t_len = strlen(target);
	l_len = strlen(linkname);

	t = target; l = linkname; common_len = 0;
	while (*t == *l && *t != '\0') {
		if (*t == '/')
			common_len = (t - target) + 1;
		t++;
		l++;
	}
	depth = 0;
	while (*l != '\0') {
		if (*l++ == '/')
			depth++;
	}
	assert (common_len <= t_len && common_len <= l_len &&
			memcmp(target, linkname, common_len) == 0);
	len = 3 * depth + t_len - common_len;

	n = malloc(len + 1);
	if (FAILEDTOALLOC(n))
		return NULL;
	p = n;
	while (depth > 0) {
		memcpy(p, "../", 3);
		p += 3;
	}
	memcpy(p, target + common_len, 1 + t_len - common_len);
	p += t_len - common_len;
	assert ((size_t)(p-n) == len);
	return n;
}

static retvalue releasefile(struct release *release, struct openfile *f) {
	struct checksums *checksums;
	retvalue r;

	if (f->relativefilename == NULL) {
		assert (f->fullfinalfilename == NULL);
		assert (f->fulltemporaryfilename == NULL);
		return RET_NOTHING;
	}
	assert((f->fullfinalfilename == NULL
		  && f->fulltemporaryfilename == NULL)
		|| (f->fullfinalfilename != NULL
		  && f->fulltemporaryfilename != NULL));

	r = checksums_from_context(&checksums, &f->context);
	if (RET_WAS_ERROR(r))
		return r;
	if (f->symlinkas) {
		char *symlinktarget = calc_relative_path(f->relativefilename,
				f->symlinkas);
		if (FAILEDTOALLOC(symlinktarget))
			return RET_ERROR_OOM;
		r = release_addsymlink(release, f->symlinkas,
				symlinktarget);
		f->symlinkas = NULL;
		if (RET_WAS_ERROR(r))
			return r;
	}

	r = newreleaseentry(release, f->relativefilename, checksums,
			f->fullfinalfilename,
			f->fulltemporaryfilename,
			NULL);
	f->relativefilename = NULL;
	f->fullfinalfilename = NULL;
	f->fulltemporaryfilename = NULL;
	return r;
}

static retvalue writegz(struct filetorelease *f) {
	int zret;

	assert (f->f[ic_gzip].fd >= 0);

	f->gzstream.next_in = f->buffer;
	f->gzstream.avail_in = INPUT_BUFFER_SIZE;

	do {
		f->gzstream.next_out = f->gzoutputbuffer + f->gz_waiting_bytes;
		f->gzstream.avail_out = GZBUFSIZE - f->gz_waiting_bytes;

		zret = deflate(&f->gzstream, Z_NO_FLUSH);
		f->gz_waiting_bytes = GZBUFSIZE - f->gzstream.avail_out;

		if ((zret == Z_OK && f->gz_waiting_bytes >= GZBUFSIZE / 2)
		     || zret == Z_BUF_ERROR) {
			retvalue r;
			/* there should be anything to write, otherwise
			 * better break to avoid an infinite loop */
			if (f->gz_waiting_bytes == 0)
				break;
			r = writetofile(&f->f[ic_gzip],
					f->gzoutputbuffer, f->gz_waiting_bytes);
			assert (r != RET_NOTHING);
			if (RET_WAS_ERROR(r))
				return r;
			f->gz_waiting_bytes = 0;
		}
		/* as we start with some data to process, Z_BUF_ERROR
		 * should only happen when no output is possible, as that
		 * gets possible again it should finally produce more output
		 * and return Z_OK and always terminate. Hopefully... */
	} while (zret == Z_BUF_ERROR
			|| (zret == Z_OK && f->gzstream.avail_in != 0));

	f->gzstream.next_in = NULL;
	f->gzstream.avail_in = 0;

	if (zret != Z_OK) {
		if (f->gzstream.msg == NULL) {
			fprintf(stderr, "Error from zlib's deflate: "
					"unknown(%d)\n", zret);
		} else {
			fprintf(stderr, "Error from zlib's deflate: %s\n",
					f->gzstream.msg);
		}
		return RET_ERROR;
	}
	return RET_OK;
}

static retvalue finishgz(struct filetorelease *f) {
	int zret;

	assert (f->f[ic_gzip].fd >= 0);

	f->gzstream.next_in = f->buffer;
	f->gzstream.avail_in = f->waiting_bytes;

	do {
		f->gzstream.next_out = f->gzoutputbuffer + f->gz_waiting_bytes;
		f->gzstream.avail_out = GZBUFSIZE - f->gz_waiting_bytes;

		zret = deflate(&f->gzstream, Z_FINISH);
		f->gz_waiting_bytes = GZBUFSIZE - f->gzstream.avail_out;

		if (zret == Z_OK || zret == Z_STREAM_END
		     || zret == Z_BUF_ERROR) {
			retvalue r;
			if (f->gz_waiting_bytes == 0) {
				if (zret != Z_STREAM_END)  {
					fprintf(stderr,
"Unexpected buffer error after deflate (%d)\n", zret);
					return RET_ERROR;
				}
				break;
			}
			r = writetofile(&f->f[ic_gzip],
					f->gzoutputbuffer, f->gz_waiting_bytes);
			assert (r != RET_NOTHING);
			if (RET_WAS_ERROR(r))
				return r;
			f->gz_waiting_bytes = 0;
		}
		/* see above */
	} while (zret == Z_BUF_ERROR || zret == Z_OK);

	if (zret != Z_STREAM_END) {
		if (f->gzstream.msg == NULL) {
			fprintf(stderr, "Error from zlib's deflate: "
					"unknown(%d)\n", zret);
		} else {
			fprintf(stderr, "Error from zlib's deflate: %s\n",
					f->gzstream.msg);
		}
		return RET_ERROR;
	}

	zret = deflateEnd(&f->gzstream);
	/* to avoid deflateEnd called again */
	f->gzstream.next_out = NULL;
	if (zret != Z_OK) {
		if (f->gzstream.msg == NULL) {
			fprintf(stderr, "Error from zlib's deflateEnd: "
					"unknown(%d)\n", zret);
		} else {
			fprintf(stderr, "Error from zlib's deflateEnd: %s\n",
					f->gzstream.msg);
		}
		return RET_ERROR;
	}


	return RET_OK;
}

#ifdef HAVE_LIBBZ2

static retvalue writebz(struct filetorelease *f) {
	int bzret;

	assert (f->f[ic_bzip2].fd >= 0);

	f->bzstream.next_in = (char*)f->buffer;
	f->bzstream.avail_in = INPUT_BUFFER_SIZE;

	do {
		f->bzstream.next_out = f->bzoutputbuffer + f->bz_waiting_bytes;
		f->bzstream.avail_out = BZBUFSIZE - f->bz_waiting_bytes;

		bzret = BZ2_bzCompress(&f->bzstream, BZ_RUN);
		f->bz_waiting_bytes = BZBUFSIZE - f->bzstream.avail_out;

		if (bzret == BZ_RUN_OK &&
				f->bz_waiting_bytes >= BZBUFSIZE / 2) {
			retvalue r;
			assert (f->bz_waiting_bytes > 0);
			r = writetofile(&f->f[ic_bzip2],
					(const unsigned char *)f->bzoutputbuffer,
					f->bz_waiting_bytes);
			assert (r != RET_NOTHING);
			if (RET_WAS_ERROR(r))
				return r;
			f->bz_waiting_bytes = 0;
		}
	} while (bzret == BZ_RUN_OK && f->bzstream.avail_in != 0);

	f->bzstream.next_in = NULL;
	f->bzstream.avail_in = 0;

	if (bzret != BZ_RUN_OK) {
		fprintf(stderr, "Error from libbz2's bzCompress: "
					"%d\n", bzret);
		return RET_ERROR;
	}
	return RET_OK;
}

static retvalue finishbz(struct filetorelease *f) {
	int bzret;

	assert (f->f[ic_bzip2].fd >= 0);

	f->bzstream.next_in = (char*)f->buffer;
	f->bzstream.avail_in = f->waiting_bytes;

	do {
		f->bzstream.next_out = f->bzoutputbuffer + f->bz_waiting_bytes;
		f->bzstream.avail_out = BZBUFSIZE - f->bz_waiting_bytes;

		bzret = BZ2_bzCompress(&f->bzstream, BZ_FINISH);
		f->bz_waiting_bytes = BZBUFSIZE - f->bzstream.avail_out;

		/* BZ_RUN_OK most likely is not possible here, but BZ_FINISH_OK
		 * is returned when it cannot be finished in one step.
		 * but better safe then sorry... */
		if ((bzret == BZ_RUN_OK || bzret == BZ_FINISH_OK
					|| bzret == BZ_STREAM_END)
		    && f->bz_waiting_bytes > 0) {
			retvalue r;
			r = writetofile(&f->f[ic_bzip2],
					(const unsigned char*)f->bzoutputbuffer,
					f->bz_waiting_bytes);
			assert (r != RET_NOTHING);
			if (RET_WAS_ERROR(r))
				return r;
			f->bz_waiting_bytes = 0;
		}
	} while (bzret == BZ_RUN_OK || bzret == BZ_FINISH_OK);

	if (bzret != BZ_STREAM_END) {
		fprintf(stderr, "Error from bzlib's bzCompress: "
				"%d\n", bzret);
		return RET_ERROR;
	}

	bzret = BZ2_bzCompressEnd(&f->bzstream);
	/* to avoid bzCompressEnd called again */
	f->bzstream.next_out = NULL;
	if (bzret != BZ_OK) {
		fprintf(stderr, "Error from libbz2's bzCompressEnd: "
				"%d\n", bzret);
		return RET_ERROR;
	}

	return RET_OK;
}
#endif

#ifdef HAVE_LIBLZMA

static retvalue writexz(struct filetorelease *f) {
	lzma_ret xzret;

	assert (f->f[ic_xz].fd >= 0);

	f->xzstream.next_in = f->buffer;
	f->xzstream.avail_in = INPUT_BUFFER_SIZE;

	do {
		f->xzstream.next_out = f->xzoutputbuffer + f->xz_waiting_bytes;
		f->xzstream.avail_out = XZBUFSIZE - f->xz_waiting_bytes;

		xzret = lzma_code(&f->xzstream, LZMA_RUN);
		f->xz_waiting_bytes = XZBUFSIZE - f->xzstream.avail_out;

		if (xzret == LZMA_OK &&
				f->xz_waiting_bytes >= XZBUFSIZE / 2) {
			retvalue r;
			assert (f->xz_waiting_bytes > 0);
			r = writetofile(&f->f[ic_xz],
					(const unsigned char *)f->xzoutputbuffer,
					f->xz_waiting_bytes);
			assert (r != RET_NOTHING);
			if (RET_WAS_ERROR(r))
				return r;
			f->xz_waiting_bytes = 0;
		}
	} while (xzret == LZMA_OK && f->xzstream.avail_in != 0);

	f->xzstream.next_in = NULL;
	f->xzstream.avail_in = 0;

	if (xzret != LZMA_OK) {
		fprintf(stderr, "Error from liblzma's lzma_code: "
					"%d\n", xzret);
		return RET_ERROR;
	}
	return RET_OK;
}

static retvalue finishxz(struct filetorelease *f) {
	lzma_ret xzret;

	assert (f->f[ic_xz].fd >= 0);

	f->xzstream.next_in = f->buffer;
	f->xzstream.avail_in = f->waiting_bytes;

	do {
		f->xzstream.next_out = f->xzoutputbuffer + f->xz_waiting_bytes;
		f->xzstream.avail_out = XZBUFSIZE - f->xz_waiting_bytes;

		xzret = lzma_code(&f->xzstream, LZMA_FINISH);
		f->xz_waiting_bytes = XZBUFSIZE - f->xzstream.avail_out;

		if ((xzret == LZMA_OK || xzret == LZMA_STREAM_END)
		    && f->xz_waiting_bytes > 0) {
			retvalue r;
			r = writetofile(&f->f[ic_xz],
					(const unsigned char*)f->xzoutputbuffer,
					f->xz_waiting_bytes);
			assert (r != RET_NOTHING);
			if (RET_WAS_ERROR(r))
				return r;
			f->xz_waiting_bytes = 0;
		}
	} while (xzret == LZMA_OK);

	if (xzret != LZMA_STREAM_END) {
		fprintf(stderr, "Error from liblzma's lzma_code: "
				"%d\n", xzret);
		return RET_ERROR;
	}
	assert (f->xz_waiting_bytes == 0);

	lzma_end(&f->xzstream);
	free(f->xzoutputbuffer);
	f->xzoutputbuffer = NULL;

	return RET_OK;
}
#endif

retvalue release_finishfile(struct release *release, struct filetorelease *file) {
	retvalue result, r;
	enum indexcompression i;

	if (RET_WAS_ERROR(file->state)) {
		r = file->state;
		release_abortfile(file);
		return r;
	}

	r = writetofile(&file->f[ic_uncompressed],
			file->buffer, file->waiting_bytes);
	if (RET_WAS_ERROR(r)) {
		release_abortfile(file);
		return r;
	}
	if (file->f[ic_uncompressed].fd >= 0) {
		if (close(file->f[ic_uncompressed].fd) != 0) {
			int e = errno;
			file->f[ic_uncompressed].fd = -1;
			release_abortfile(file);
			return RET_ERRNO(e);
		}
		file->f[ic_uncompressed].fd = -1;
	}
	if (file->f[ic_gzip].fd >= 0) {
		r = finishgz(file);
		if (RET_WAS_ERROR(r)) {
			release_abortfile(file);
			return r;
		}
		if (close(file->f[ic_gzip].fd) != 0) {
			int e = errno;
			file->f[ic_gzip].fd = -1;
			release_abortfile(file);
			return RET_ERRNO(e);
		}
		file->f[ic_gzip].fd = -1;
	}
#ifdef HAVE_LIBBZ2
	if (file->f[ic_bzip2].fd >= 0) {
		r = finishbz(file);
		if (RET_WAS_ERROR(r)) {
			release_abortfile(file);
			return r;
		}
		if (close(file->f[ic_bzip2].fd) != 0) {
			int e = errno;
			file->f[ic_bzip2].fd = -1;
			release_abortfile(file);
			return RET_ERRNO(e);
		}
		file->f[ic_bzip2].fd = -1;
	}
#endif
#ifdef HAVE_LIBLZMA
	if (file->f[ic_xz].fd >= 0) {
		r = finishxz(file);
		if (RET_WAS_ERROR(r)) {
			release_abortfile(file);
			return r;
		}
		if (close(file->f[ic_xz].fd) != 0) {
			int e = errno;
			file->f[ic_xz].fd = -1;
			release_abortfile(file);
			return RET_ERRNO(e);
		}
		file->f[ic_xz].fd = -1;
	}
#endif
	release->new = true;
	result = RET_OK;

	for (i = ic_uncompressed ; i < ic_count ; i++) {
		r = releasefile(release, &file->f[i]);
		if (RET_WAS_ERROR(r)) {
			release_abortfile(file);
			return r;
		}
		RET_UPDATE(result, r);
	}
	free(file->buffer);
	free(file->gzoutputbuffer);
#ifdef HAVE_LIBBZ2
	free(file->bzoutputbuffer);
#endif
#ifdef HAVE_LIBLZMA
	assert(file->xzoutputbuffer == NULL);
#endif
	free(file);
	return result;
}

static retvalue release_processbuffer(struct filetorelease *file) {
	retvalue result, r;

	result = RET_OK;
	assert (file->waiting_bytes == INPUT_BUFFER_SIZE);

	/* always call this - even if there is no uncompressed file
	 * to generate - so that checksums are calculated */
	r = writetofile(&file->f[ic_uncompressed],
			file->buffer, INPUT_BUFFER_SIZE);
	RET_UPDATE(result, r);

	if (file->f[ic_gzip].relativefilename != NULL) {
		r = writegz(file);
		RET_UPDATE(result, r);
	}
	RET_UPDATE(file->state, result);
#ifdef HAVE_LIBBZ2
	if (file->f[ic_bzip2].relativefilename != NULL) {
		r = writebz(file);
		RET_UPDATE(result, r);
	}
	RET_UPDATE(file->state, result);
#endif
#ifdef HAVE_LIBLZMA
	if (file->f[ic_xz].relativefilename != NULL) {
		r = writexz(file);
		RET_UPDATE(result, r);
	}
	RET_UPDATE(file->state, result);
#endif
	return result;
}

retvalue release_writedata(struct filetorelease *file, const char *data, size_t len) {
	retvalue result, r;
	size_t free_bytes;

	result = RET_OK;
	/* move stuff into buffer, so stuff is not processed byte by byte */
	free_bytes = INPUT_BUFFER_SIZE - file->waiting_bytes;
	if (len < free_bytes) {
		memcpy(file->buffer + file->waiting_bytes, data, len);
		file->waiting_bytes += len;
		assert (file->waiting_bytes < INPUT_BUFFER_SIZE);
		return RET_OK;
	}
	memcpy(file->buffer + file->waiting_bytes, data, free_bytes);
	len -= free_bytes;
	data += free_bytes;
	file->waiting_bytes += free_bytes;
	r = release_processbuffer(file);
	RET_UPDATE(result, r);
	while (len >= INPUT_BUFFER_SIZE) {
		/* should not hopefully not happen, as all this copying
		 * is quite slow... */
		memcpy(file->buffer, data, INPUT_BUFFER_SIZE);
		len -= INPUT_BUFFER_SIZE;
		data += INPUT_BUFFER_SIZE;
		r = release_processbuffer(file);
		RET_UPDATE(result, r);
	}
	memcpy(file->buffer, data, len);
	file->waiting_bytes = len;
	assert (file->waiting_bytes < INPUT_BUFFER_SIZE);
	return result;
}

/* Generate a "Release"-file for arbitrary directory */
retvalue release_directorydescription(struct release *release, const struct distribution *distribution, const struct target *target, const char *releasename, bool onlyifneeded) {
	retvalue r;
	struct filetorelease *f;
	char *relfilename;

	relfilename = calc_dirconcat(target->relativedirectory, releasename);
	if (FAILEDTOALLOC(relfilename))
		return RET_ERROR_OOM;
	r = startfile(release, relfilename, NULL,
			IC_FLAG(ic_uncompressed), onlyifneeded, &f);
	free(relfilename);
	if (RET_WAS_ERROR(r) || r == RET_NOTHING)
		return r;

#define release_writeheader(name, data) \
	        if (data != NULL) { \
			(void)release_writestring(f, name ": "); \
			(void)release_writestring(f, data); \
			(void)release_writestring(f, "\n"); \
		}

	release_writeheader("Archive", distribution->suite);
	release_writeheader("Version", distribution->version);
	release_writeheader("Component",
			atoms_components[target->component]);
	release_writeheader("Origin", distribution->origin);
	release_writeheader("Label", distribution->label);
	release_writeheader("Architecture",
			atoms_architectures[target->architecture]);
	release_writeheader("NotAutomatic", distribution->notautomatic);
	release_writeheader("ButAutomaticUpgrades",
			distribution->butautomaticupgrades);
	release_writeheader("Description", distribution->description);
#undef release_writeheader
	r = release_finishfile(release, f);
	return r;
}

static retvalue storechecksums(struct release *release) {
	struct release_entry *file;
	retvalue result, r;
	const char *combinedchecksum;
	/* size including trailing '\0' character: */
	size_t len;

	result = RET_OK;

	for (file = release->files ; file != NULL ; file = file->next) {

		assert (file->relativefilename != NULL);

		r = table_deleterecord(release->cachedb,
				file->relativefilename, true);
		if (RET_WAS_ERROR(r))
			return r;

		if (file->checksums == NULL)
			continue;

		r = checksums_getcombined(file->checksums, &combinedchecksum, &len);
		RET_UPDATE(result, r);
		if (!RET_IS_OK(r))
			continue;

		r = table_adduniqsizedrecord(release->cachedb,
				file->relativefilename, combinedchecksum, len+1,
				false, false);
		RET_UPDATE(result, r);
	}
	return result;
}

static inline bool componentneedsfake(const char *cn, const struct release *release) {
	if (release->fakecomponentprefix == NULL)
		return false;
	if (strncmp(cn, release->fakecomponentprefix,
			       release->fakecomponentprefixlen) != 0)
		return true;
	return cn[release->fakecomponentprefixlen] != '/';
}


static struct release_entry *newspecialreleaseentry(struct release *release, const char *relativefilename) {
	struct release_entry *n, *p;

	assert (relativefilename != NULL);
	n = zNEW(struct release_entry);
	if (FAILEDTOALLOC(n))
		return NULL;
	n->relativefilename = strdup(relativefilename);
	n->fullfinalfilename = calc_dirconcat(release->dirofdist,
			relativefilename);
	if (!FAILEDTOALLOC(n->fullfinalfilename))
		n->fulltemporaryfilename = mprintf("%s.new",
				n->fullfinalfilename);
	if (FAILEDTOALLOC(n->relativefilename)
			|| FAILEDTOALLOC(n->fullfinalfilename)
			|| FAILEDTOALLOC(n->fulltemporaryfilename)) {
		release_freeentry(n);
		return NULL;
	}
	if (release->files == NULL)
		release->files = n;
	else {
		p = release->files;
		while (p->next != NULL)
			p = p->next;
		p->next = n;
	}
	return n;
}
static void omitunusedspecialreleaseentry(struct release *release, struct release_entry *e) {
	struct release_entry **p;

	if (e->fulltemporaryfilename != NULL)
		/* new file available, nothing to omit */
		return;
	if (isregularfile(e->fullfinalfilename))
		/* this will be deleted, everything fine */
		return;
	p = &release->files;
	while (*p != NULL && *p != e)
		p = &(*p)->next;
	if (*p != e) {
		assert (*p == e);
		return;
	}
	*p = e->next;
	release_freeentry(e);
}

/* Generate a main "Release" file for a distribution */
retvalue release_prepare(struct release *release, struct distribution *distribution, bool onlyifneeded) {
	size_t s;
	retvalue r;
	char buffer[100], untilbuffer[100];
	time_t t;
	struct tm *gmt;
	struct release_entry *file;
	enum checksumtype cs;
	int i;
	static const char * const release_checksum_headers[cs_hashCOUNT] =
		{ "MD5Sum:\n", "SHA1:\n", "SHA256:\n" };
	struct release_entry *plainentry, *signedentry, *detachedentry;

	// TODO: check for existence of Release file here first?
	if (onlyifneeded && !release->new) {
		return RET_NOTHING;
	}

	(void)time(&t);
	gmt = gmtime(&t);
	if (FAILEDTOALLOC(gmt))
		return RET_ERROR_OOM;
	s=strftime(buffer, 99, "%a, %d %b %Y %H:%M:%S UTC", gmt);
	if (s == 0 || s >= 99) {
		fprintf(stderr, "strftime is doing strange things...\n");
		return RET_ERROR;
	}
	if (distribution->validfor > 0) {
		t += distribution->validfor;
		gmt = gmtime(&t);
		if (FAILEDTOALLOC(gmt))
			return RET_ERROR_OOM;
		s=strftime(untilbuffer, 99, "%a, %d %b %Y %H:%M:%S UTC", gmt);
		if (s == 0 || s >= 99) {
			fprintf(stderr,
"strftime is doing strange things...\n");
			return RET_ERROR;
		}
	}
	plainentry = newspecialreleaseentry(release, "Release");
	if (FAILEDTOALLOC(plainentry))
		return RET_ERROR_OOM;
	signedentry = newspecialreleaseentry(release, "InRelease");
	if (FAILEDTOALLOC(signedentry))
		return RET_ERROR_OOM;
	detachedentry = newspecialreleaseentry(release, "Release.gpg");
	if (FAILEDTOALLOC(signedentry))
		return RET_ERROR_OOM;
	r = signature_startsignedfile(&release->signedfile);
	if (RET_WAS_ERROR(r))
		return r;
#define writestring(s) signedfile_write(release->signedfile, s, strlen(s))
#define writechar(c) {char __c = c ; signedfile_write(release->signedfile, &__c, 1); }

	if (distribution->origin != NULL) {
		writestring("Origin: ");
		writestring(distribution->origin);
		writechar('\n');
	}
	if (distribution->label != NULL) {
		writestring("Label: ");
		writestring(distribution->label);
		writechar('\n');
	}
	if (release->fakesuite != NULL) {
		writestring("Suite: ");
		writestring(release->fakesuite);
		writechar('\n');
	} else if (distribution->suite != NULL) {
		writestring("Suite: ");
		writestring(distribution->suite);
		writechar('\n');
	}
	writestring("Codename: ");
	if (release->fakecodename != NULL)
		writestring(release->fakecodename);
	else
		writestring(distribution->codename);
	if (distribution->version != NULL) {
		writestring("\nVersion: ");
		writestring(distribution->version);
	}
	writestring("\nDate: ");
	writestring(buffer);
	if (distribution->validfor > 0) {
		writestring("\nValid-Until: ");
		writestring(untilbuffer);
	}
	writestring("\nArchitectures:");
	for (i = 0 ; i < distribution->architectures.count ; i++) {
		architecture_t a = distribution->architectures.atoms[i];

		/* Debian's topmost Release files do not list it,
		 * so we won't either */
		if (a == architecture_source)
			continue;
		writechar(' ');
		writestring(atoms_architectures[a]);
	}
	writestring("\nComponents:");
	for (i = 0 ; i < distribution->components.count ; i++) {
		component_t c = distribution->components.atoms[i];
		const char *cn = atoms_components[c];

		writechar(' ');
		if (componentneedsfake(cn, release)) {
			writestring(release->fakecomponentprefix);
			writechar('/');
		}
		writestring(cn);
	}
	if (distribution->description != NULL) {
		writestring("\nDescription: ");
		writestring(distribution->description);
	}
	if (distribution->signed_by != NULL) {
		writestring("\nSigned-By: ");
		writestring(distribution->signed_by);
	}
	if (distribution->notautomatic != NULL) {
		writestring("\nNotAutomatic: ");
		writestring(distribution->notautomatic);
	}
	if (distribution->butautomaticupgrades != NULL) {
		writestring("\nButAutomaticUpgrades: ");
		writestring(distribution->butautomaticupgrades);
	}
	writechar('\n');

	for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++) {
		assert (release_checksum_headers[cs] != NULL);
		writestring(release_checksum_headers[cs]);
		for (file = release->files ; file != NULL ; file = file->next) {
			const char *hash, *size;
			size_t hashlen, sizelen;
			if (file->checksums == NULL)
				continue;
			if (!checksums_gethashpart(file->checksums, cs,
					&hash, &hashlen, &size, &sizelen))
				continue;
			writechar(' ');
			signedfile_write(release->signedfile, hash, hashlen);
			writechar(' ');
			signedfile_write(release->signedfile, size, sizelen);
			writechar(' ');
			writestring(file->relativefilename);
			writechar('\n');
		}
	}
	r = signedfile_create(release->signedfile,
			plainentry->fulltemporaryfilename,
			&signedentry->fulltemporaryfilename,
			&detachedentry->fulltemporaryfilename,
			&distribution->signwith, !global.keeptemporaries);
	if (RET_WAS_ERROR(r)) {
		signedfile_free(release->signedfile);
		release->signedfile = NULL;
		return r;
	}
	omitunusedspecialreleaseentry(release, signedentry);
	omitunusedspecialreleaseentry(release, detachedentry);
	return RET_OK;
}

static inline void release_toouthook(struct release *release, struct distribution *distribution) {
	struct release_entry *file;
	char *reldir;

	if (release->snapshotname != NULL) {
		reldir = mprintf("dists/%s/snapshots/%s",
				distribution->codename, release->snapshotname);
		if (FAILEDTOALLOC(reldir))
			return;
		outhook_send("BEGIN-SNAPSHOT", distribution->codename,
				reldir, release->snapshotname);
	} else {
		reldir = mprintf("dists/%s", distribution->codename);
		if (FAILEDTOALLOC(reldir))
			return;
		outhook_send("BEGIN-DISTRIBUTION", distribution->codename,
				reldir, distribution->suite);
	}

	for (file = release->files ; file != NULL ; file = file->next) {
		/* relf chks ffn  ftfn symt
		 * name chks NULL NULL NULL: added old filename or virtual file
		 * name chks file NULL NULL: renamed new file and published
		 * name NULL file NULL NULL: renamed new file
		 * name NULL NULL NULL NULL: deleted file
		 * name NULL NULL NULL file: created symlink */

		/* should already be in place: */
		assert (file->fulltemporaryfilename == NULL);
		/* symlinks are special: */
		if (file->symlinktarget != NULL) {
			outhook_send("DISTSYMLINK",
					reldir,
					file->relativefilename,
					file->symlinktarget);
		} else if (file->fullfinalfilename != NULL) {
			outhook_send("DISTFILE", reldir,
					file->relativefilename,
					file->fullfinalfilename);
		} else if (file->checksums == NULL){
			outhook_send("DISTDELETE", reldir,
					file->relativefilename, NULL);
		}
		/* would be nice to distinguish kept and virtual files... */
	}

	if (release->snapshotname != NULL) {
		outhook_send("END-SNAPSHOT", distribution->codename,
				reldir, release->snapshotname);
	} else {
		outhook_send("END-DISTRIBUTION", distribution->codename,
				reldir, distribution->suite);
	}
	free(reldir);
}

/* Generate a main "Release" file for a distribution */
retvalue release_finish(/*@only@*/struct release *release, struct distribution *distribution) {
	retvalue result, r;
	int e;
	struct release_entry *file;
	bool somethingwasdone;

	somethingwasdone = false;
	result = RET_OK;

	for (file = release->files ; file != NULL ; file = file->next) {
		assert (file->relativefilename != NULL);
		if (file->checksums == NULL
				&& file->fullfinalfilename != NULL
				&& file->fulltemporaryfilename == NULL
				&& file->symlinktarget == NULL) {
			e = unlink(file->fullfinalfilename);
			if (e < 0) {
				e = errno;
				fprintf(stderr,
"Error %d deleting %s: %s. (Will be ignored)\n",
					e, file->fullfinalfilename,
					strerror(e));
			}
			free(file->fullfinalfilename);
			file->fullfinalfilename = NULL;
		} else if (file->fulltemporaryfilename != NULL) {
			assert (file->fullfinalfilename != NULL);
			assert (file->symlinktarget == NULL);

			e = rename(file->fulltemporaryfilename,
					file->fullfinalfilename);
			if (e < 0) {
				e = errno;
				fprintf(stderr,
"Error %d moving %s to %s: %s!\n",
						e, file->fulltemporaryfilename,
						file->fullfinalfilename,
						strerror(e));
				r = RET_ERRNO(e);
				/* after something was done, do not stop
				 * but try to do as much as possible */
				if (!somethingwasdone) {
					release_free(release);
					return r;
				}
				RET_UPDATE(result, r);
			} else {
				somethingwasdone = true;
				free(file->fulltemporaryfilename);
				file->fulltemporaryfilename = NULL;
			}
		} else if (file->symlinktarget != NULL) {
			assert (file->fullfinalfilename != NULL);

			(void)unlink(file->fullfinalfilename);
			e = symlink(file->symlinktarget, file->fullfinalfilename);
			if (e != 0) {
				e = errno;
				fprintf(stderr,
"Error %d creating symlink '%s' -> '%s': %s.\n",
					e, file->fullfinalfilename,
					file->symlinktarget,
					strerror(e));
				r = RET_ERRNO(e);
				/* after something was done, do not stop
				 * but try to do as much as possible */
				if (!somethingwasdone) {
					release_free(release);
					return r;
				}
				RET_UPDATE(result, r);
			}
		}
	}
	if (RET_WAS_ERROR(result) && somethingwasdone) {
		fprintf(stderr,
"ATTENTION: some files were already moved to place, some could not be.\n"
"The generated index files for %s might be in a inconsistent state\n"
"and currently not useable! You should remove the reason for the failure\n"
"(most likely bad access permissions) and export the affected distributions\n"
"manually (via reprepro export codenames) as soon as possible!\n",
			distribution->codename);
	}
	if (release->cachedb != NULL) {
		// TODO: split this in removing before and adding later?
		// remember which file were changed in case of error, so
		// only those are changed...
		/* now update the cache database,
		 * so we find those the next time */
		r = storechecksums(release);
		RET_UPDATE(result, r);

		r = table_close(release->cachedb);
		release->cachedb = NULL;
		RET_ENDUPDATE(result, r);
	}
	release_toouthook(release, distribution);
	/* free everything */
	release_free(release);
	return result;
}

retvalue release_mkdir(struct release *release, const char *relativedirectory) {
	char *dirname;
	retvalue r;

	dirname = calc_dirconcat(release->dirofdist, relativedirectory);
	if (FAILEDTOALLOC(dirname))
		return RET_ERROR_OOM;
	// TODO: in some far future, remember which dirs were created so that
	r = dirs_make_recursive(dirname);
	free(dirname);
	return r;
}
