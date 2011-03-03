/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2007,2009 Bernhard R. Link
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
#include <malloc.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <time.h>
#include <zlib.h>
#ifdef HAVE_LIBBZ2
#include <bzlib.h>
#endif
#define CHECKSUMS_CONTEXT visible
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "filecntl.h"
#include "chunks.h"
#include "checksums.h"
#include "dirs.h"
#include "names.h"
#include "signature.h"
#include "distribution.h"
#include "release.h"

#define INPUT_BUFFER_SIZE 1024
#define GZBUFSIZE 40960
#define BZBUFSIZE 40960

struct release {
	/* The base-directory of the distribution we are exporting */
	char *dirofdist;
	/* anything new yet added */
	bool new;
	/* snapshot */
	bool snapshot;
	/*@null@*/char *fakesuite;
	/*@null@*/char *fakecodename;
	/*@null@*/const char *fakecomponentprefix;
	size_t fakecomponentprefixlen;
	/* the files yet for the list */
	struct release_entry {
		struct release_entry *next;
		char *relativefilename;
		struct checksums *checksums;
		/* both == NULL if not new, only final==NULL means delete */
		char *fullfinalfilename;
		char *fulltemporaryfilename;
	} *files;
	/* the Release file in preperation (only valid between _prepare and _finish) */
	struct signedfile *signedfile;
	/* the cache database for old files */
	struct table *cachedb;
};

void release_free(struct release *release) {
	struct release_entry *e;

	free(release->dirofdist);
	free(release->fakesuite);
	free(release->fakecodename);
	while( (e = release->files) != NULL ) {
		release->files = e->next;
		free(e->relativefilename);
		checksums_free(e->checksums);
		free(e->fullfinalfilename);
		if( !global.keeptemporaries && e->fulltemporaryfilename != NULL)
			unlink(e->fulltemporaryfilename);
		free(e->fulltemporaryfilename);
		free(e);
	}
	if( release->signedfile != NULL )
		signedfile_free(release->signedfile, !global.keeptemporaries);
	if( release->cachedb != NULL ) {
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
		/*@only@*/ /*@null@*/ char *fulltemporaryfilename ) {
	struct release_entry *n,*p;
	n = malloc(sizeof(struct release_entry));
	if( n == NULL )
		return RET_ERROR_OOM;
	n->next = NULL;
	n->relativefilename = relativefilename;
	n->checksums = checksums;
	n->fullfinalfilename = fullfinalfilename;
	n->fulltemporaryfilename = fulltemporaryfilename;
	if( release->files == NULL )
		release->files = n;
	else {
		p = release->files;
		while( p->next != NULL )
			p = p->next;
		p->next = n;
	}
	return RET_OK;
}

retvalue release_init(struct release **release, struct database *database, const char *codename, const char *suite, const char *fakecomponentprefix) {
	struct release *n;
	size_t len, suitelen, codenamelen;
	retvalue r;

	n = calloc(1,sizeof(struct release));
	n->dirofdist = calc_dirconcat(global.distdir, codename);
	if( n->dirofdist == NULL ) {
		free(n);
		return RET_ERROR_OOM;
	}
	if( fakecomponentprefix != NULL ) {
		len = strlen(fakecomponentprefix);
		codenamelen = strlen(codename);

		n->fakecomponentprefix = fakecomponentprefix;
		n->fakecomponentprefixlen = len;
		if( codenamelen > len &&
		    codename[codenamelen - len - 1] == '/' &&
		    memcmp(codename + (codenamelen - len),
		           fakecomponentprefix, len) == 0) {
			n->fakecodename = strndup(codename,
					codenamelen - len - 1);
			if( FAILEDTOALLOC(n->fakecodename) ) {
				free(n->dirofdist);
				free(n);
				return RET_ERROR_OOM;
			}
		}
		if( suite != NULL && (suitelen = strlen(suite)) > len &&
		    suite[suitelen - len - 1] == '/' &&
		    memcmp(suite + (suitelen - len),
		           fakecomponentprefix, len) == 0) {
			n->fakesuite = strndup(suite,
					suitelen - len - 1);
			if( FAILEDTOALLOC(n->fakesuite) ) {
				free(n->fakecodename);
				free(n->dirofdist);
				free(n);
				return RET_ERROR_OOM;
			}
		}
	}
	r = database_openreleasecache(database, codename, &n->cachedb);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) ) {
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

	n = calloc(1,sizeof(struct release));
	n->dirofdist = calc_snapshotbasedir(codename, name);
	if( n->dirofdist == NULL ) {
		free(n);
		return RET_ERROR_OOM;
	}
	/* apt only removes the last /... part but we create two,
	 * so stop it generating warnings by faking a suite */
	n->fakesuite = mprintf("%s/snapshots/%s", codename, name);
	if( n->fakesuite == NULL ) {
		free(n->dirofdist);
		free(n);
		return RET_ERROR_OOM;
	}
	n->fakecodename = NULL;
	n->fakecomponentprefix = NULL;
	n->fakecomponentprefixlen = 0;
	n->cachedb = NULL;
	n->snapshot = true;
	*release = n;
	return RET_OK;
}

retvalue release_adddel(struct release *release,/*@only@*/char *reltmpfile) {
	char *filename;

	filename = calc_dirconcat(release->dirofdist,reltmpfile);
	if( filename == NULL ) {
		free(reltmpfile);
		return RET_ERROR_OOM;
	}
	free(reltmpfile);
	return newreleaseentry(release,NULL,NULL,NULL,filename);
}

retvalue release_addnew(struct release *release,/*@only@*/char *reltmpfile,/*@only@*/char *relfilename) {
	retvalue r;
	char *filename, *finalfilename;
	struct checksums *checksums;

	filename = calc_dirconcat(release->dirofdist,reltmpfile);
	if( filename == NULL ) {
		free(reltmpfile);
		free(relfilename);
		return RET_ERROR_OOM;
	}
	free(reltmpfile);
	r = checksums_read(filename, &checksums);
	if( !RET_IS_OK(r) ) {
		free(relfilename);
		free(filename);
		return r;
	}
	finalfilename = calc_dirconcat(release->dirofdist,relfilename);
	if( finalfilename == NULL ) {
		free(relfilename);
		free(filename);
		checksums_free(checksums);
		return RET_ERROR_OOM;
	}
	release->new = true;
	return newreleaseentry(release, relfilename,
			checksums, finalfilename, filename);
}

retvalue release_addsilentnew(struct release *release,/*@only@*/char *reltmpfile,/*@only@*/char *relfilename) {
	char *filename, *finalfilename;

	filename = calc_dirconcat(release->dirofdist,reltmpfile);
	if( filename == NULL ) {
		free(reltmpfile);
		free(relfilename);
		return RET_ERROR_OOM;
	}
	free(reltmpfile);
	finalfilename = calc_dirconcat(release->dirofdist,relfilename);
	if( finalfilename == NULL ) {
		free(relfilename);
		free(filename);
		return RET_ERROR_OOM;
	}
	free(relfilename);
	release->new = true;
	return newreleaseentry(release, NULL,
			NULL, finalfilename, filename);
}

retvalue release_addold(struct release *release,/*@only@*/char *relfilename) {
	retvalue r;
	char *filename;
	struct checksums *checksums;

	filename = calc_dirconcat(release->dirofdist,relfilename);
	if( filename == NULL ) {
		free(filename);
		return RET_ERROR_OOM;
	}
	r = checksums_read(filename, &checksums);
	free(filename);
	if( !RET_IS_OK(r) ) {
		free(relfilename);
		return r;
	}
	return newreleaseentry(release, relfilename, checksums, NULL, NULL);
}

static char *calc_compressedname(const char *name, enum indexcompression ic) {
	switch( ic ) {
		case ic_uncompressed:
			return strdup(name);
		case ic_gzip:
			return calc_addsuffix(name, "gz");
#ifdef HAVE_LIBBZ2
		case ic_bzip2:
			return calc_addsuffix(name, "bz2");
#endif
		default:
			assert( "Huh?" == NULL );
			return NULL;
	}
}

static retvalue release_usecached(struct release *release,
				const char *relfilename,
				compressionset compressions) {
	retvalue result,r;
	enum indexcompression ic;
	char *filename[ic_count];
	struct checksums *checksums[ic_count];

	memset(filename, 0, sizeof(filename));
	memset(checksums, 0, sizeof(checksums));
	result = RET_OK;

	for( ic = ic_uncompressed ; ic < ic_count ; ic++ ) {
		if( ic != ic_uncompressed &&
		    (compressions & IC_FLAG(ic)) == 0 )
			continue;
		filename[ic] = calc_compressedname(relfilename, ic);
		if( filename[ic] == NULL ) {
			result = RET_ERROR_OOM;
			break;
		}
	}
	if( RET_IS_OK(result) ) {
		/* first look if the there are actual files, in case
		 * the cache still lists them but they got lost */

		for( ic = ic_uncompressed ; ic < ic_count ; ic++ ) {
			char *fullfilename;

			if( (compressions & IC_FLAG(ic)) == 0 )
				continue;
			assert( filename[ic] != NULL );
			fullfilename = calc_dirconcat(release->dirofdist,
					filename[ic]);
			if( fullfilename == NULL ) {
				result = RET_ERROR_OOM;
				break;
			}
			if( !isregularfile(fullfilename) ) {
				free(fullfilename);
				result = RET_NOTHING;
				break;
			}
			free(fullfilename);
		}
	}
	if( RET_IS_OK(result) && release->cachedb == NULL )
		result = RET_NOTHING;
	if( !RET_IS_OK(result) ) {
		for( ic = ic_uncompressed ; ic < ic_count ; ic++ )
			free(filename[ic]);
		return result;
	}

	/* now that the files are there look into the cache
	 * what checksums they have. */

	for( ic = ic_uncompressed ; ic < ic_count ; ic++ ) {
		char *combinedchecksum;

		if( filename[ic] == NULL )
			continue;
		r = table_getrecord(release->cachedb, filename[ic],
				&combinedchecksum);
		if( !RET_IS_OK(r) ) {
			result = r;
			break;
		}
		r = checksums_parse(&checksums[ic], combinedchecksum);
		// TODO: handle malformed checksums better?
		free(combinedchecksum);
		if( !RET_IS_OK(r) ) {
			result = r;
			break;
		}
	}
	/* some files might not yet have some type of checksum available,
	 * so calculate them (checking the other checksums match...): */
	if( RET_IS_OK(result) ) {
		for( ic = ic_uncompressed ; ic < ic_count ; ic++ ) {
			char *fullfilename;
			if( filename[ic] == NULL )
				continue;
			fullfilename = calc_dirconcat(release->dirofdist,
					filename[ic]);
			if( fullfilename == NULL )
				r = RET_ERROR_OOM;
			else
				r = checksums_complete(&checksums[ic],
					fullfilename);
			if( r == RET_ERROR_WRONG_MD5 ) {
				fprintf(stderr,
"WARNING: '%s' is different from recorded checksums.\n"
"(This was only catched because some new checksum type was not yet available.)\n"
"Triggering recreation of that file.\n", fullfilename);
				r = RET_NOTHING;
			}
			free(fullfilename);
			if( !RET_IS_OK(r) ) {
				result = r;
				break;
			}
		}
	}
	if( !RET_IS_OK(result) ) {
		for( ic = ic_uncompressed ; ic < ic_count ; ic++ ) {
			if( filename[ic] == NULL )
				continue;
			free(filename[ic]);
			checksums_free(checksums[ic]);
		}
		return result;
	}
	/* everything found, commit it: */
	result = RET_OK;
	for( ic = ic_uncompressed ; ic < ic_count ; ic++ ) {
		if( filename[ic] == NULL )
			continue;
		r = newreleaseentry(release, filename[ic],
				checksums[ic],
				NULL, NULL);
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
};

void release_abortfile(struct filetorelease *file) {
	enum indexcompression i;

	for( i = ic_uncompressed ; i < ic_count ; i++ ) {
		if( file->f[i].fd >= 0 ) {
			(void)close(file->f[i].fd);
			if( file->f[i].fulltemporaryfilename != NULL )
				(void)unlink(file->f[i].fulltemporaryfilename);
		}
		free(file->f[i].relativefilename);
		free(file->f[i].fullfinalfilename);
		free(file->f[i].fulltemporaryfilename);
	}
	free(file->buffer);
	free(file->gzoutputbuffer);
	if( file->gzstream.next_out != NULL ) {
		(void)deflateEnd(&file->gzstream);
	}
#ifdef HAVE_LIBBZ2
	free(file->bzoutputbuffer);
	if( file->bzstream.next_out != NULL ) {
		(void)BZ2_bzCompressEnd(&file->bzstream);
	}
#endif
}

bool release_oldexists(struct filetorelease *file) {
	if( file->f[ic_uncompressed].fullfinalfilename != NULL ) {
		if( file->f[ic_gzip].fullfinalfilename != NULL ) {
			return isregularfile(file->f[ic_gzip].fullfinalfilename) &&
			       isregularfile(file->f[ic_uncompressed].fullfinalfilename);
		} else {
			return isregularfile(file->f[ic_uncompressed].fullfinalfilename);
		}
	} else {
		assert( file->f[ic_gzip].fullfinalfilename != NULL );
		return isregularfile(file->f[ic_gzip].fullfinalfilename);
	}
}

static retvalue openfile(const char *dirofdist, struct openfile *f) {

	f->fullfinalfilename = calc_dirconcat(dirofdist,f->relativefilename);
	if( f->fullfinalfilename == NULL )
		return RET_ERROR_OOM;
	f->fulltemporaryfilename = calc_addsuffix(f->fullfinalfilename,"new");
	if( f->fulltemporaryfilename == NULL )
		return RET_ERROR_OOM;
	(void)unlink(f->fulltemporaryfilename);
	f->fd = open(f->fulltemporaryfilename,O_WRONLY|O_CREAT|O_EXCL|O_NOCTTY,0666);
	if( f->fd < 0 ) {
		int e = errno;
		fprintf(stderr, "Error %d opening file %s for writing: %s\n",
				e, f->fulltemporaryfilename, strerror(e));
		return RET_ERRNO(e);
	}
	return RET_OK;
}

static retvalue writetofile(struct openfile *file, const unsigned char *data, size_t len) {

	checksumscontext_update(&file->context, data, len);

	if( file->fd < 0 )
		return RET_NOTHING;

	while( len > 0 ) {
		ssize_t written = write(file->fd,data,len);
		if( written >= 0 ) {
			len -= written;
			data += written;
		} else {
			int e = errno;
			if( e == EAGAIN || e == EINTR )
				continue;
			fprintf(stderr, "Error %d writing to %s: %s\n",
					e, file->fullfinalfilename,
					strerror(e));
			return RET_ERRNO(e);
		}
	}
	return RET_OK;
}

static retvalue	initgzcompression(struct filetorelease *f) {
	int zret;

	if( (zlibCompileFlags() & (1<<17)) !=0 ) {
		fprintf(stderr,"libz compiled without .gz supporting code\n");
		return RET_ERROR;
	}
	f->gzoutputbuffer = malloc(GZBUFSIZE);
	if( f->gzoutputbuffer == NULL )
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
	if( zret == Z_MEM_ERROR )
		return RET_ERROR_OOM;
	if( zret != Z_OK ) {
		if( f->gzstream.msg == NULL ) {
			fprintf(stderr,"Error from zlib's deflateInit2: "
					"unknown(%d)\n", zret);
		} else {
			fprintf(stderr,"Error from zlib's deflateInit2: %s\n",
					f->gzstream.msg);
		}
		return RET_ERROR;
	}
	return RET_OK;
}

#ifdef HAVE_LIBBZ2

static retvalue	initbzcompression(struct filetorelease *f) {
	int bzret;

	f->bzoutputbuffer = malloc(BZBUFSIZE);
	if( f->bzoutputbuffer == NULL )
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
	if( bzret == BZ_MEM_ERROR )
		return RET_ERROR_OOM;
	if( bzret != BZ_OK ) {
		fprintf(stderr,"Error from libbz2's bzCompressInit: "
				"%d\n", bzret);
		return RET_ERROR;
	}
	return RET_OK;
}
#endif

static retvalue startfile(struct release *release,
		char *filename, compressionset compressions,
		bool usecache,
		struct filetorelease **file) {
	struct filetorelease *n;
	enum indexcompression i;

	if( usecache ) {
		retvalue r = release_usecached(release,filename,compressions);
		if( r != RET_NOTHING ) {
			free(filename);
			if( RET_IS_OK(r) )
				return RET_NOTHING;
			return r;
		}
	}

	n = calloc(1,sizeof(struct filetorelease));
	if( n == NULL ) {
		free(filename);
		return RET_ERROR_OOM;
	}
	n->buffer = malloc(INPUT_BUFFER_SIZE);
	if( n->buffer == NULL ) {
		release_abortfile(n);
		return RET_ERROR_OOM;
	}
	for( i = ic_uncompressed ; i < ic_count ; i ++ ) {
		n->f[i].fd = -1;
	}
	n->f[ic_uncompressed].relativefilename = filename;
	if( n->f[ic_uncompressed].relativefilename == NULL ) {
		release_abortfile(n);
		return RET_ERROR_OOM;
	}
	if( (compressions & IC_FLAG(ic_uncompressed)) != 0 ) {
		retvalue r;
		r = openfile(release->dirofdist,&n->f[ic_uncompressed]);
		if( RET_WAS_ERROR(r) ) {
			release_abortfile(n);
			return r;
		}
	}
	if( (compressions & IC_FLAG(ic_gzip)) != 0 ) {
		retvalue r;
		n->f[ic_gzip].relativefilename = calc_addsuffix(filename,"gz");
		if( n->f[ic_gzip].relativefilename == NULL ) {
			release_abortfile(n);
			return RET_ERROR_OOM;
		}
		r = openfile(release->dirofdist,&n->f[ic_gzip]);
		if( RET_WAS_ERROR(r) ) {
			release_abortfile(n);
			return r;
		}
		checksumscontext_init(&n->f[ic_gzip].context);
		r = initgzcompression(n);
		if( RET_WAS_ERROR(r) ) {
			release_abortfile(n);
			return r;
		}
	}
#ifdef HAVE_LIBBZ2
	if( (compressions & IC_FLAG(ic_bzip2)) != 0 ) {
		retvalue r;
		n->f[ic_bzip2].relativefilename = calc_addsuffix(filename,"bz2");
		if( n->f[ic_bzip2].relativefilename == NULL ) {
			release_abortfile(n);
			return RET_ERROR_OOM;
		}
		r = openfile(release->dirofdist,&n->f[ic_bzip2]);
		if( RET_WAS_ERROR(r) ) {
			release_abortfile(n);
			return r;
		}
		checksumscontext_init(&n->f[ic_bzip2].context);
		r = initbzcompression(n);
		if( RET_WAS_ERROR(r) ) {
			release_abortfile(n);
			return r;
		}
	}
#endif
	checksumscontext_init(&n->f[ic_uncompressed].context);
	*file = n;
	return RET_OK;
}

retvalue release_startfile2(struct release *release,
		const char *relative_dir, const char *filename, compressionset compressions,
		bool usecache,
		struct filetorelease **file) {
	char *relfilename;

	relfilename = calc_dirconcat(relative_dir,filename);
	if( relfilename == NULL )
		return RET_ERROR_OOM;
	return startfile(release,relfilename,compressions,usecache,file);
}
retvalue release_startfile(struct release *release,
		const char *filename, compressionset compressions,
		bool usecache,
		struct filetorelease **file) {
	char *relfilename;

	relfilename = strdup(filename);
	if( relfilename == NULL )
		return RET_ERROR_OOM;
	return startfile(release,relfilename,compressions,usecache,file);
}

static retvalue releasefile(struct release *release, struct openfile *f) {
	struct checksums *checksums;
	retvalue r;

	if( f->relativefilename == NULL ) {
		assert( f->fullfinalfilename == NULL);
		assert( f->fulltemporaryfilename == NULL);
		return RET_NOTHING;
	}
	assert((f->fullfinalfilename == NULL
		  && f->fulltemporaryfilename == NULL)
	  	|| (f->fullfinalfilename != NULL
		  && f->fulltemporaryfilename != NULL));

	r = checksums_from_context(&checksums, &f->context);
	if( RET_WAS_ERROR(r) )
		return r;
	r = newreleaseentry(release, f->relativefilename, checksums,
			f->fullfinalfilename,
			f->fulltemporaryfilename);
	f->relativefilename = NULL;
	f->fullfinalfilename = NULL;
	f->fulltemporaryfilename = NULL;
	return r;
}

static retvalue writegz(struct filetorelease *f) {
	int zret;

	assert( f->f[ic_gzip].fd >= 0  );

	f->gzstream.next_in = f->buffer;
	f->gzstream.avail_in = INPUT_BUFFER_SIZE;

	do {
		f->gzstream.next_out = f->gzoutputbuffer + f->gz_waiting_bytes;
		f->gzstream.avail_out = GZBUFSIZE - f->gz_waiting_bytes;

		zret = deflate(&f->gzstream,Z_NO_FLUSH);
		f->gz_waiting_bytes = GZBUFSIZE - f->gzstream.avail_out;

		if( (zret == Z_OK && f->gz_waiting_bytes >= GZBUFSIZE / 2 )
		     || zret == Z_BUF_ERROR ) {
			retvalue r;
			/* there should be anything to write, otherwise
			 * better break to avoid an infinite loop */
			if( f->gz_waiting_bytes == 0 )
				break;
			r = writetofile(&f->f[ic_gzip],
					f->gzoutputbuffer, f->gz_waiting_bytes);
			assert( r != RET_NOTHING );
			if( RET_WAS_ERROR(r) )
				return r;
			f->gz_waiting_bytes = 0;
		}
		/* as we start with some data to process, Z_BUF_ERROR
		 * should only happend when no output is possible, as that
		 * gets possible again it should finally produce more output
		 * and return Z_OK and always terminate. Hopefully... */
	} while( zret == Z_BUF_ERROR
			|| ( zret == Z_OK && f->gzstream.avail_in != 0));

	f->gzstream.next_in = NULL;
	f->gzstream.avail_in = 0;

	if( zret != Z_OK ) {
		if( f->gzstream.msg == NULL ) {
			fprintf(stderr,"Error from zlib's deflate: "
					"unknown(%d)\n", zret);
		} else {
			fprintf(stderr,"Error from zlib's deflate: %s\n",
					f->gzstream.msg);
		}
		return RET_ERROR;
	}
	return RET_OK;
}

static retvalue finishgz(struct filetorelease *f) {
	int zret;

	assert( f->f[ic_gzip].fd >= 0  );

	f->gzstream.next_in = f->buffer;
	f->gzstream.avail_in = f->waiting_bytes;

	do {
		f->gzstream.next_out = f->gzoutputbuffer + f->gz_waiting_bytes;
		f->gzstream.avail_out = GZBUFSIZE - f->gz_waiting_bytes;

		zret = deflate(&f->gzstream,Z_FINISH);
		f->gz_waiting_bytes = GZBUFSIZE - f->gzstream.avail_out;

		if( zret == Z_OK || zret == Z_STREAM_END
		     || zret == Z_BUF_ERROR ) {
			retvalue r;
			if( f->gz_waiting_bytes == 0 ) {
				if( zret != Z_STREAM_END )  {
					fprintf(stderr,
"Unexpected buffer error after deflate (%d)\n",zret);
					return RET_ERROR;
				}
				break;
			}
			r = writetofile(&f->f[ic_gzip],
					f->gzoutputbuffer, f->gz_waiting_bytes);
			assert( r != RET_NOTHING );
			if( RET_WAS_ERROR(r) )
				return r;
			f->gz_waiting_bytes = 0;
		}
		/* see above */
	} while( zret == Z_BUF_ERROR || zret == Z_OK );

	if( zret != Z_STREAM_END ) {
		if( f->gzstream.msg == NULL ) {
			fprintf(stderr,"Error from zlib's deflate: "
					"unknown(%d)\n", zret);
		} else {
			fprintf(stderr,"Error from zlib's deflate: %s\n",
					f->gzstream.msg);
		}
		return RET_ERROR;
	}

	zret = deflateEnd(&f->gzstream);
	/* to avoid deflateEnd called again */
	f->gzstream.next_out = NULL;
	if( zret != Z_OK ) {
		if( f->gzstream.msg == NULL ) {
			fprintf(stderr,"Error from zlib's deflateEnd: "
					"unknown(%d)\n", zret);
		} else {
			fprintf(stderr,"Error from zlib's deflateEnd: %s\n",
					f->gzstream.msg);
		}
		return RET_ERROR;
	}


	return RET_OK;
}

#ifdef HAVE_LIBBZ2

static retvalue writebz(struct filetorelease *f) {
	int bzret;

	assert( f->f[ic_bzip2].fd >= 0  );

	f->bzstream.next_in = (char*)f->buffer;
	f->bzstream.avail_in = INPUT_BUFFER_SIZE;

	do {
		f->bzstream.next_out = f->bzoutputbuffer + f->bz_waiting_bytes;
		f->bzstream.avail_out = BZBUFSIZE - f->bz_waiting_bytes;

		bzret = BZ2_bzCompress(&f->bzstream,BZ_RUN);
		f->bz_waiting_bytes = BZBUFSIZE - f->bzstream.avail_out;

		if( bzret == BZ_RUN_OK &&
				f->bz_waiting_bytes >= BZBUFSIZE / 2 ) {
			retvalue r;
			assert( f->bz_waiting_bytes > 0 );
			r = writetofile(&f->f[ic_bzip2],
					(const unsigned char *)f->bzoutputbuffer,
					f->bz_waiting_bytes);
			assert( r != RET_NOTHING );
			if( RET_WAS_ERROR(r) )
				return r;
			f->bz_waiting_bytes = 0;
		}
	} while( bzret == BZ_RUN_OK && f->bzstream.avail_in != 0 );

	f->bzstream.next_in = NULL;
	f->bzstream.avail_in = 0;

	if( bzret != BZ_RUN_OK ) {
		fprintf(stderr,"Error from libbz2's bzCompress: "
					"%d\n", bzret);
		return RET_ERROR;
	}
	return RET_OK;
}

static retvalue finishbz(struct filetorelease *f) {
	int bzret;

	assert( f->f[ic_bzip2].fd >= 0  );

	f->bzstream.next_in = (char*)f->buffer;
	f->bzstream.avail_in = f->waiting_bytes;

	do {
		f->bzstream.next_out = f->bzoutputbuffer + f->bz_waiting_bytes;
		f->bzstream.avail_out = BZBUFSIZE - f->bz_waiting_bytes;

		bzret = BZ2_bzCompress(&f->bzstream,BZ_FINISH);
		f->bz_waiting_bytes = BZBUFSIZE - f->bzstream.avail_out;

		/* BZ_RUN_OK most likely is not possible here, but BZ_FINISH_OK
		 * is returned when it cannot be finished in one step.
		 * but better safe then sorry... */
		if( (bzret == BZ_RUN_OK || bzret == BZ_FINISH_OK || bzret == BZ_STREAM_END)
		    && f->bz_waiting_bytes > 0 ) {
			retvalue r;
			r = writetofile(&f->f[ic_bzip2],
					(const unsigned char*)f->bzoutputbuffer,
					f->bz_waiting_bytes);
			assert( r != RET_NOTHING );
			if( RET_WAS_ERROR(r) )
				return r;
			f->bz_waiting_bytes = 0;
		}
	} while( bzret == BZ_RUN_OK || bzret == BZ_FINISH_OK );

	if( bzret != BZ_STREAM_END ) {
		fprintf(stderr,"Error from bzlib's bzCompress: "
				"%d\n", bzret);
		return RET_ERROR;
	}

	bzret = BZ2_bzCompressEnd(&f->bzstream);
	/* to avoid bzCompressEnd called again */
	f->bzstream.next_out = NULL;
	if( bzret != BZ_OK ) {
		fprintf(stderr,"Error from libbz2's bzCompressEnd: "
				"%d\n", bzret);
		return RET_ERROR;
	}

	return RET_OK;
}
#endif

retvalue release_finishfile(struct release *release, struct filetorelease *file) {
	retvalue result,r;
	enum indexcompression i;

	if( RET_WAS_ERROR( file->state ) ) {
		r = file->state;
		release_abortfile(file);
		return r;
	}

	r = writetofile(&file->f[ic_uncompressed],
			file->buffer, file->waiting_bytes);
	if( RET_WAS_ERROR(r) ) {
		release_abortfile(file);
		return r;
	}
	if( file->f[ic_uncompressed].fd >= 0 ) {
		if( close(file->f[ic_uncompressed].fd) != 0 ) {
			int e = errno;
			file->f[ic_uncompressed].fd = -1;
			release_abortfile(file);
			return RET_ERRNO(e);
		}
		file->f[ic_uncompressed].fd = -1;
	}
	if( file->f[ic_gzip].fd >= 0 ) {
		r = finishgz(file);
		if( RET_WAS_ERROR(r) ) {
			release_abortfile(file);
			return r;
		}
		if( close(file->f[ic_gzip].fd) != 0 ) {
			int e = errno;
			file->f[ic_gzip].fd = -1;
			release_abortfile(file);
			return RET_ERRNO(e);
		}
		file->f[ic_gzip].fd = -1;
	}
#ifdef HAVE_LIBBZ2
	if( file->f[ic_bzip2].fd >= 0 ) {
		r = finishbz(file);
		if( RET_WAS_ERROR(r) ) {
			release_abortfile(file);
			return r;
		}
		if( close(file->f[ic_bzip2].fd) != 0 ) {
			int e = errno;
			file->f[ic_bzip2].fd = -1;
			release_abortfile(file);
			return RET_ERRNO(e);
		}
		file->f[ic_bzip2].fd = -1;
	}
#endif
	release->new = true;
	result = RET_OK;

	for( i = ic_uncompressed ; i < ic_count ; i++ ) {
		r = releasefile(release,&file->f[i]);
		if( RET_WAS_ERROR(r) ) {
			release_abortfile(file);
			return r;
		}
		RET_UPDATE(result,r);
	}
	free(file->buffer);
	free(file->gzoutputbuffer);
#ifdef HAVE_LIBBZ2
	free(file->bzoutputbuffer);
#endif
	free(file);
	return result;
}

static retvalue release_processbuffer(struct filetorelease *file) {
	retvalue result,r;

	result = RET_OK;
	assert( file->waiting_bytes == INPUT_BUFFER_SIZE );

	/* always call this - even if there is no uncompressed file
	 * to generate - so that checksums are calculated */
	r = writetofile(&file->f[ic_uncompressed],
			file->buffer, INPUT_BUFFER_SIZE);
	RET_UPDATE(result, r);

	if( file->f[ic_gzip].relativefilename != NULL ) {
		r = writegz(file);
		RET_UPDATE(result, r);
	}
	RET_UPDATE(file->state, result);
#ifdef HAVE_LIBBZ2
	if( file->f[ic_bzip2].relativefilename != NULL ) {
		r = writebz(file);
		RET_UPDATE(result,r);
	}
	RET_UPDATE(file->state, result);
#endif
	return result;
}

retvalue release_writedata(struct filetorelease *file, const char *data, size_t len) {
	retvalue result,r;
	size_t free_bytes;

	result = RET_OK;
	/* move stuff into buffer, so stuff is not processed byte by byte */
	free_bytes = INPUT_BUFFER_SIZE - file->waiting_bytes;
	if( len < free_bytes ) {
		memcpy(file->buffer + file->waiting_bytes, data, len);
		file->waiting_bytes += len;
		assert( file->waiting_bytes < INPUT_BUFFER_SIZE );
		return RET_OK;
	}
	memcpy(file->buffer + file->waiting_bytes, data, free_bytes);
	len -= free_bytes;
	data += free_bytes;
	file->waiting_bytes += free_bytes;
	r = release_processbuffer(file);
	RET_UPDATE(result, r);
	while( len >= INPUT_BUFFER_SIZE ) {
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
	assert( file->waiting_bytes < INPUT_BUFFER_SIZE );
	return result;
}

/* Generate a "Release"-file for arbitrary directory */
retvalue release_directorydescription(struct release *release, const struct distribution *distribution, const struct target *target, const char *releasename, bool onlyifneeded) {
	retvalue r;
	struct filetorelease *f;

	r = release_startfile2(release,
		target->relativedirectory, releasename,
		IC_FLAG(ic_uncompressed), onlyifneeded,
		&f);
	if( RET_WAS_ERROR(r) || r == RET_NOTHING )
		return r;

#define release_writeheader(name, data) \
	        if( data != NULL ) { \
			(void)release_writestring(f,name ": "); \
			(void)release_writestring(f,data); \
			(void)release_writestring(f,"\n"); \
		}

	release_writeheader("Archive",distribution->suite);
	release_writeheader("Version",distribution->version);
	release_writeheader("Component",
			atoms_components[target->component_atom]);
	release_writeheader("Origin",distribution->origin);
	release_writeheader("Label",distribution->label);
	release_writeheader("Architecture",
			atoms_architectures[target->architecture_atom]);
	release_writeheader("NotAutomatic",distribution->notautomatic);
	release_writeheader("ButAutomaticUpgrades",
			distribution->butautomaticupgrades);
	release_writeheader("Description",distribution->description);
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

	for( file = release->files ; file != NULL ; file = file->next ) {

		if( file->relativefilename == NULL )
			continue;

		r = table_deleterecord(release->cachedb,
				file->relativefilename, true);
		if( RET_WAS_ERROR(r) )
			return r;

		r = checksums_getcombined(file->checksums, &combinedchecksum, &len);
		RET_UPDATE(result, r);
		if( !RET_IS_OK(r) )
			continue;

		r = table_adduniqsizedrecord(release->cachedb,
				file->relativefilename, combinedchecksum, len+1,
				false, false);
		RET_UPDATE(result, r);
	}
	return result;
}

static inline bool componentneedsfake(const char *cn, const struct release *release) {
	if( release->fakecomponentprefix == NULL )
		return false;
	if( strncmp(cn, release->fakecomponentprefix,
			       release->fakecomponentprefixlen) != 0 )
		return true;
	return cn[release->fakecomponentprefixlen] != '/';
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

	// TODO: check for existance of Release file here first?
	if( onlyifneeded && !release->new ) {
		return RET_NOTHING;
	}

	(void)time(&t);
	gmt = gmtime(&t);
	if( gmt == NULL ) {
		return RET_ERROR_OOM;
	}
	s=strftime(buffer,99,"%a, %d %b %Y %H:%M:%S UTC",gmt);
	if( s == 0 || s >= 99) {
		fprintf(stderr,"strftime is doing strange things...\n");
		return RET_ERROR;
	}
	if( distribution->validfor > 0 ) {
		t += distribution->validfor;
		gmt = gmtime(&t);
		if( gmt == NULL ) {
			return RET_ERROR_OOM;
		}
		s=strftime(untilbuffer,99,"%a, %d %b %Y %H:%M:%S UTC",gmt);
		if( s == 0 || s >= 99) {
			fprintf(stderr,"strftime is doing strange things...\n");
			return RET_ERROR;
		}
	}
	r = signature_startsignedfile(release->dirofdist,
			"Release", "InRelease",
			&release->signedfile);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
#define writestring(s) signedfile_write(release->signedfile, s, strlen(s))
#define writechar(c) {char __c = c ; signedfile_write(release->signedfile, &__c, 1); }

	if( distribution->origin != NULL ) {
		writestring("Origin: ");
		writestring(distribution->origin);
		writechar('\n');
	}
	if( distribution->label != NULL ) {
		writestring("Label: ");
		writestring(distribution->label);
		writechar('\n');
	}
	if( release->fakesuite != NULL ) {
		writestring("Suite: ");
		writestring(release->fakesuite);
		writechar('\n');
	} else if( distribution->suite != NULL ) {
		writestring("Suite: ");
		writestring(distribution->suite);
		writechar('\n');
	}
	writestring("Codename: ");
	if( release->fakecodename != NULL )
		writestring(release->fakecodename);
	else
		writestring(distribution->codename);
	if( distribution->version != NULL ) {
		writestring("\nVersion: ");
		writestring(distribution->version);
	}
	writestring("\nDate: ");
	writestring(buffer);
	if( distribution->validfor > 0 ) {
		writestring("\nValid-Until: ");
		writestring(untilbuffer);
	}
	writestring("\nArchitectures:");
	for( i = 0 ; i < distribution->architectures.count ; i++ ) {
		architecture_t a = distribution->architectures.atoms[i];

		/* Debian's topmost Release files do not list it, so we won't either */
		if( a == architecture_source )
			continue;
		writechar(' ');
		writestring(atoms_architectures[a]);
	}
	writestring("\nComponents:");
	for( i = 0 ; i < distribution->components.count ; i++ ) {
		component_t c = distribution->components.atoms[i];
		const char *cn = atoms_components[c];

		writechar(' ');
		if( componentneedsfake(cn, release) ) {
			writestring(release->fakecomponentprefix);
			writechar('/');
		}
		writestring(cn);
	}
	if( distribution->description != NULL ) {
		writestring("\nDescription: ");
		writestring(distribution->description);
	}
	if( distribution->notautomatic != NULL ) {
		writestring("\nNotAutomatic: ");
		writestring(distribution->notautomatic);
	}
	if( distribution->butautomaticupgrades != NULL ) {
		writestring("\nButAutomaticUpgrades: ");
		writestring(distribution->butautomaticupgrades);
	}
	writechar('\n');

	for( cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++ ) {
		assert( release_checksum_headers[cs] != NULL );
		writestring(release_checksum_headers[cs]);
		for( file = release->files ; file != NULL ; file = file->next ) {
			const char *hash, *size;
			size_t hashlen, sizelen;
			if( file->relativefilename == NULL )
				continue;
			if( !checksums_gethashpart(file->checksums, cs,
					&hash, &hashlen, &size, &sizelen) )
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
	r = signedfile_prepare(release->signedfile, distribution->signwith,
			!global.keeptemporaries);
	if( RET_WAS_ERROR(r) ) {
		signedfile_free(release->signedfile, !global.keeptemporaries);
		release->signedfile = NULL;
		return r;
	}
	return RET_OK;
}

/* Generate a main "Release" file for a distribution */
retvalue release_finish(/*@only@*/struct release *release, struct distribution *distribution) {
	retvalue result,r;
	int e;
	struct release_entry *file;
	bool somethingwasdone;

	somethingwasdone = false;
	result = RET_OK;

	for( file = release->files ; file != NULL ; file = file->next ) {
		if( file->relativefilename == NULL
				&& file->fullfinalfilename == NULL ) {
			assert(file->fulltemporaryfilename != NULL );
			e = unlink(file->fulltemporaryfilename);
			if( e < 0 ) {
				e = errno;
				fprintf(stderr,
"Error %d deleting %s: %s. (Will be ignored)\n",
					e, file->fulltemporaryfilename,
					strerror(e));
			}
			free(file->fulltemporaryfilename);
			file->fulltemporaryfilename = NULL;
		} else if( file->fulltemporaryfilename != NULL ) {
			e = rename(file->fulltemporaryfilename,
					file->fullfinalfilename);
			if( e < 0 ) {
				e = errno;
				fprintf(stderr, "Error %d moving %s to %s: %s!\n",
						e, file->fulltemporaryfilename,
						file->fullfinalfilename,
						strerror(e));
				r = RET_ERRNO(e);
				/* after something was done, do not stop
				 * but try to do as much as possible */
				if( !somethingwasdone ) {
					release_free(release);
					return r;
				}
				RET_UPDATE(result,r);
			} else {
				somethingwasdone = true;
				free(file->fulltemporaryfilename);
				file->fulltemporaryfilename = NULL;
			}
		}
	}
	r = signedfile_finalize(release->signedfile, &somethingwasdone);
	if( RET_WAS_ERROR(r) && !somethingwasdone ) {
		release_free(release);
		return r;
	}
	RET_UPDATE(result,r);
	if( RET_WAS_ERROR(result) && somethingwasdone ) {
		fprintf(stderr,
"ATTENTION: some files were already moved to place, some could not be.\n"
"The generated index files for %s might be in a inconsistent state\n"
"and currently not useable! You should remove the reason for the failure\n"
"(most likely bad access permissions) and export the affected distributions\n"
"manually (via reprepro export codenames) as soon as possible!\n",
			distribution->codename);
	}
	if( release->cachedb != NULL ) {
		// TODO: split this in removing before and adding later?
		// remember which file were changed in case of error, so
		// only those are changed...
		/* now update the cache database, so we find those the next time */
		r = storechecksums(release);
		RET_UPDATE(result,r);

		r = table_close(release->cachedb);
		release->cachedb = NULL;
		RET_ENDUPDATE(result, r);
	}
	/* free everything */
	release_free(release);
	return result;
}

retvalue release_mkdir(struct release *release, const char *relativedirectory) {
	char *dirname;
	retvalue r;

	dirname = calc_dirconcat(release->dirofdist,relativedirectory);
	if( dirname == NULL )
		return RET_ERROR_OOM;
	// TODO: in some far future, remember which dirs were created so that
	r = dirs_make_recursive(dirname);
	free(dirname);
	return r;
}
