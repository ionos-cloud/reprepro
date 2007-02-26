/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005 Bernhard R. Link
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
#include <db.h>
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "chunks.h"
#include "md5.h"
#include "md5sum.h"
#include "copyfile.h"
#include "dirs.h"
#include "names.h"
#include "signature.h"
#include "distribution.h"
#include "release.h"

extern int verbose;


struct release {
	/* The base-directory of the distribution we are exporting */
	char *dirofdist;
	/* anything new yet added */
	bool_t new;
	/* snapshot */
	bool_t snapshot;
	/* the files yet for the list */
	struct release_entry {
		struct release_entry *next;
		char *relativefilename;
		char *md5sum;
		/* both == NULL if not new, only final==NULL means delete */
		char *fullfinalfilename;
		char *fulltemporaryfilename;
	} *files;
	/* the cache database for old files */
	DB *cachedb;
	/* some more things, only here to make
	 * free'ing them in case of error easier */
	char *newreleasefilename,*releasefilename;
	char *newsignfilename,*signfilename;
};

void release_free(struct release *release) {
	struct release_entry *e;

	free(release->dirofdist);
	while( (e = release->files) != NULL ) {
		release->files = e->next;
		free(e->relativefilename);
		free(e->md5sum);
		free(e->fullfinalfilename);
		free(e->fulltemporaryfilename);
		free(e);
	}
	free(release->newreleasefilename);
	free(release->releasefilename);
	free(release->newsignfilename);
	free(release->signfilename);
	if( release->cachedb != NULL ) {
		release->cachedb->close(release->cachedb,0);
	}
	free(release);
}

const char *release_dirofdist(struct release *release) {
	return release->dirofdist;
}

static retvalue newreleaseentry(struct release *release, /*@only@*/ char *relativefilename,
		/*@only@*/ char *md5sum,
		/*@only@*/ /*@null@*/ char *fullfinalfilename,
		/*@only@*/ /*@null@*/ char *fulltemporaryfilename ) {
	struct release_entry *n,*p;
	n = malloc(sizeof(struct release_entry));
	if( n == NULL )
		return RET_ERROR_OOM;
	n->next = NULL;
	n->relativefilename = relativefilename;
	n->md5sum = md5sum;
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

retvalue release_init(const char *dbdir, const char *distdir, const char *codename, struct release **release) {
	struct release *n;
	int dbret;
	char *filename;

	n = calloc(1,sizeof(struct release));
	n->dirofdist = calc_dirconcat(distdir,codename);
	if( n->dirofdist == NULL ) {
		free(n);
		return RET_ERROR_OOM;
	}
	dbret = db_create(&n->cachedb,NULL,0);
	if( dbret < 0 ) {
		fprintf(stderr, "db_create: %s\n", db_strerror(dbret));
		n->cachedb = NULL;
		free(n->dirofdist);
		free(n);
		return RET_DBERR(dbret);
	}
	filename = calc_dirconcat(dbdir,"release.cache.db");
	if( filename == NULL ) {
		(void)n->cachedb->close(n->cachedb,0);
		return RET_ERROR_OOM;
	}
	dbret = DB_OPEN(n->cachedb,filename,codename, DB_HASH, DB_CREATE);
	if( dbret < 0 ) {
		n->cachedb->err(n->cachedb, dbret, "%s(%s)",
				filename,codename);
		(void)n->cachedb->close(n->cachedb,0);
		free(filename);
		free(n->dirofdist);
		free(n);
		return RET_DBERR(dbret);
	}
	free(filename);
	*release = n;
	return RET_OK;
}

retvalue release_initsnapshot(const char *distdir, const char *codename, const char *name, struct release **release) {
	struct release *n;

	n = calloc(1,sizeof(struct release));
	n->dirofdist = mprintf("%s/%s/snapshots/%s",distdir,codename,name);
	if( n->dirofdist == NULL ) {
		free(n);
		return RET_ERROR_OOM;
	}
	n->cachedb = NULL;
	n->snapshot = TRUE;
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
	char *filename,*finalfilename,*md5sum;

	filename = calc_dirconcat(release->dirofdist,reltmpfile);
	if( filename == NULL ) {
		free(reltmpfile);
		free(relfilename);
		return RET_ERROR_OOM;
	}
	free(reltmpfile);
	r = md5sum_read(filename,&md5sum);
	if( !RET_IS_OK(r) ) {
		free(relfilename);
		free(filename);
		return r;
	}
	finalfilename = calc_dirconcat(release->dirofdist,relfilename);
	if( finalfilename == NULL ) {
		free(relfilename);
		free(filename);
		free(md5sum);
		return RET_ERROR_OOM;
	}
	release->new = TRUE;
	return newreleaseentry(release,relfilename,md5sum,finalfilename,filename);
}

retvalue release_addold(struct release *release,/*@only@*/char *relfilename) {
	retvalue r;
	char *filename,*md5sum;

	filename = calc_dirconcat(release->dirofdist,relfilename);
	if( filename == NULL ) {
		free(filename);
		return RET_ERROR_OOM;
	}
	r = md5sum_read(filename,&md5sum);
	free(filename);
	if( !RET_IS_OK(r) ) {
		free(relfilename);
		return r;
	}
	return newreleaseentry(release,relfilename,md5sum,NULL,NULL);
}

static retvalue getcachevalue(struct release *,const char *, char **);
static retvalue release_usecached(struct release *release,
				const char *relfilename,
				compressionset compressions) {
	retvalue result,r;
	char *filename,*md5sum;
	char *gzrelfilename,*gzmd5sum;
#ifdef HAVE_LIBBZ2
	char *bzrelfilename,*bzmd5sum;
#endif

	/* first look if the there are actual files, in case
	 * the cache still lists them but they got lost */
	filename = calc_dirconcat(release->dirofdist,relfilename);
	if( filename == NULL ) {
		return RET_ERROR_OOM;
	}
	if( (compressions & IC_FLAG(ic_uncompressed)) != 0 ) {
		if( !isregularfile(filename) ) {
			free(filename);
			return RET_NOTHING;
		}
	}
	if( (compressions & IC_FLAG(ic_gzip)) != 0 ) {
		char *gzfilename;
		gzfilename = calc_addsuffix(filename,"gz");
		if( gzfilename == NULL ) {
			free(filename);
			return RET_ERROR_OOM;
		}
		if( !isregularfile(gzfilename) ) {
			free(filename);
			free(gzfilename);
			return RET_NOTHING;
		}
		free(gzfilename);
	}
#ifdef HAVE_LIBBZ2
	if( (compressions & IC_FLAG(ic_bzip2)) != 0 ) {
		char *bzfilename;
		bzfilename = calc_addsuffix(filename,"bz");
		if( bzfilename == NULL ) {
			free(filename);
			return RET_ERROR_OOM;
		}
		if( !isregularfile(bzfilename) ) {
			free(filename);
			free(bzfilename);
			return RET_NOTHING;
		}
		free(bzfilename);
	}
#endif
	free(filename);

	/* now that the files are there look into the cache
	 * what md5sum they have. */

	filename = strdup(relfilename);
	if( filename == NULL )
		return RET_ERROR_OOM;

	r = getcachevalue(release,relfilename,&md5sum);
	if( !RET_IS_OK(r) ) {
		free(filename);
		return r;
	}
	if( (compressions & IC_FLAG(ic_gzip)) != 0 ) {
		gzrelfilename = calc_addsuffix(relfilename,"gz");
		if( gzrelfilename == NULL ) {
			free(filename);
			free(md5sum);
			return RET_ERROR_OOM;
		}
		r = getcachevalue(release,gzrelfilename,&gzmd5sum);
		if( !RET_IS_OK(r) ) {
			free(filename);
			free(md5sum);
			return r;
		}
	} else {
		gzrelfilename = NULL;
		gzmd5sum = NULL;
	}
#ifdef HAVE_LIBBZ2
	if( (compressions & IC_FLAG(ic_bzip2)) != 0 ) {
		bzrelfilename = calc_addsuffix(relfilename,"bz2");
		if( bzrelfilename == NULL ) {
			free(filename);
			free(md5sum);
			free(gzmd5sum);
			free(gzrelfilename);
			return RET_ERROR_OOM;
		}
		r = getcachevalue(release,bzrelfilename,&bzmd5sum);
		if( !RET_IS_OK(r) ) {
			free(filename);
			free(md5sum);
			free(gzmd5sum);
			free(gzrelfilename);
			return r;
		}
	} else {
		bzrelfilename = NULL;
		bzmd5sum = NULL;
	}
#endif

	result = newreleaseentry(release,filename,md5sum,NULL,NULL);
	if( gzrelfilename != NULL ) {
		r = newreleaseentry(release,gzrelfilename,gzmd5sum,NULL,NULL);
		RET_UPDATE(result,r);
	}
#ifdef HAVE_LIBBZ2
	if( bzrelfilename != NULL ) {
		r = newreleaseentry(release,bzrelfilename,bzmd5sum,NULL,NULL);
		RET_UPDATE(result,r);
	}
#endif
	return result;
}

struct filetorelease {
	retvalue state;
	struct openfile {
		int fd;
		struct MD5Context context;
		off_t filesize;
		char *relativefilename;
		char *fullfinalfilename;
		char *fulltemporaryfilename;
	} f[ic_count];
	/* output buffer for gzip compression */
	unsigned char *gzoutputbuffer; size_t gz_waiting_bytes;
	z_stream gzstream;
#ifdef OLDZLIB
	uLong crc;
#endif
#ifdef HAVE_LIBBZ2
	/* output buffer for bzip2 compression */
	unsigned char *bzoutputbuffer; size_t bz_waiting_bytes;
	bz_stream bzstream;
#endif
};

void release_abortfile(struct filetorelease *file) {
	enum indexcompression i;

	for( i = ic_uncompressed ; i < ic_count ; i++ ) {
		if( file->f[i].fd >= 0 ) {
			close(file->f[i].fd);
			if( file->f[i].fulltemporaryfilename != NULL )
				(void)unlink(file->f[i].fulltemporaryfilename);
		}
		free(file->f[i].relativefilename);
		free(file->f[i].fullfinalfilename);
		free(file->f[i].fulltemporaryfilename);
	}
	free(file->gzoutputbuffer);
	if( file->gzstream.next_out != NULL ) {
		deflateEnd(&file->gzstream);
	}
#ifdef HAVE_LIBBZ2
	free(file->bzoutputbuffer);
	if( file->bzstream.next_out != NULL ) {
		BZ2_bzCompressEnd(&file->bzstream);
	}
#endif
}

bool_t release_oldexists(struct filetorelease *file) {
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
		fprintf(stderr,"Error opening file %s for writing: %m\n",
				f->fulltemporaryfilename);
		return RET_ERRNO(e);
	}
	return RET_OK;
}

static retvalue writetofile(struct openfile *file, const char *data, size_t len) {

	file->filesize += len;
	MD5Update(&file->context,data,len);

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
			fprintf(stderr,"Error writing to %s: %d=%m\n",
					file->fullfinalfilename,e);
			return RET_ERRNO(e);
		}
	}
	return RET_OK;
}

#define GZBUFSIZE 40960

static retvalue	initgzcompression(struct filetorelease *f) {
	int zret;

#ifndef OLDZLIB
	if( (zlibCompileFlags() & (1<<17)) !=0 ) {
		fprintf(stderr,"libz compiled without .gz supporting code\n");
		return RET_ERROR;
	}
#else
	unsigned char header[10] = {
		31,139, Z_DEFLATED,
		/* flags */
		0,
		/* time */
		0, 0, 0, 0,
		/* xtra-flags */
		0,
		/* os (3 = unix, 255 = unknown)
		 * using unknown to generate the same file
		 * with or without OLDZLIB */
		255};
	retvalue r = writetofile(&f->f[ic_gzip],header,10);
	if( RET_WAS_ERROR(r) )
		return r;
	/* we have to calculate the crc ourself */
	f->crc = crc32(0L, NULL, 0);
#endif
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
#ifndef OLDZLIB
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
#else
	zret = deflateInit2(&f->gzstream,
			/* Level: 0-9 or Z_DEFAULT_COMPRESSION: */
			Z_DEFAULT_COMPRESSION,
			/* only possibility yet: */
			Z_DEFLATED,
			/* negative to no generate zlib header */
			-MAX_WBITS,
			/* how much memory to use 1-9 */
			8,
			/* default or Z_FILTERED or Z_HUFFMAN_ONLY or Z_RLE */
			Z_DEFAULT_STRATEGY
			);
#endif
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

#define BZBUFSIZE 40960

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
		bool_t usecache,
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
		MD5Init(&n->f[ic_gzip].context);
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
		MD5Init(&n->f[ic_bzip2].context);
		r = initbzcompression(n);
		if( RET_WAS_ERROR(r) ) {
			release_abortfile(n);
			return r;
		}
	}
#endif
	MD5Init(&n->f[ic_uncompressed].context);
	*file = n;
	return RET_OK;
}

retvalue release_startfile2(struct release *release,
		const char *relative_dir, const char *filename, compressionset compressions,
		bool_t usecache,
		struct filetorelease **file) {
	char *relfilename;

	relfilename = calc_dirconcat(relative_dir,filename);
	if( relfilename == NULL )
		return RET_ERROR_OOM;
	return startfile(release,relfilename,compressions,usecache,file);
}
retvalue release_startfile(struct release *release,
		const char *filename, compressionset compressions,
		bool_t usecache,
		struct filetorelease **file) {
	char *relfilename;

	relfilename = strdup(filename);
	if( relfilename == NULL )
		return RET_ERROR_OOM;
	return startfile(release,relfilename,compressions,usecache,file);
}

static retvalue releasefile(struct release *release, struct openfile *f) {
	char *md5sum;
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

	r = md5sum_genstring(&md5sum,&f->context,f->filesize);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) )
		return r;
	r = newreleaseentry(release,f->relativefilename,md5sum,
			f->fullfinalfilename,
			f->fulltemporaryfilename);
	f->relativefilename = NULL;
	f->fullfinalfilename = NULL;
	f->fulltemporaryfilename = NULL;
	return RET_OK;
}

static retvalue writegz(struct filetorelease *f, const char *data, size_t len) {
	int zret;

	assert( f->f[ic_gzip].fd >= 0  );

	if( len == 0 )
		return RET_NOTHING;

#ifdef OLDZLIB
	f->crc = crc32(f->crc,data,len);
#endif

	f->gzstream.next_in = (char*)data;
	f->gzstream.avail_in = len;

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
	char dummy;
#ifdef OLDZLIB
	unsigned char buffer[4];
	retvalue r;
#endif

	assert( f->f[ic_gzip].fd >= 0  );

	f->gzstream.next_in = &dummy;
	f->gzstream.avail_in = 0;

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

#ifdef OLDZLIB
	buffer[0] = f->crc & 0xFF;
	buffer[1] = (f->crc >> (8*1)) & 0xFF;
	buffer[2] = (f->crc >> (8*2)) & 0xFF;
	buffer[3] = (f->crc >> (8*3)) & 0xFF;
	r = writetofile(&f->f[ic_gzip],buffer,4);
	if( RET_WAS_ERROR(r) )
		return r;
	buffer[0] = f->f[ic_uncompressed].filesize & 0xFF;
	buffer[1] = (f->f[ic_uncompressed].filesize >> (8*1)) & 0xFF;
	buffer[2] = (f->f[ic_uncompressed].filesize >> (8*2)) & 0xFF;
	buffer[3] = (f->f[ic_uncompressed].filesize >> (8*3)) & 0xFF;
	r = writetofile(&f->f[ic_gzip],buffer,4);
	if( RET_WAS_ERROR(r) )
		return r;
#endif

	return RET_OK;
}

#ifdef HAVE_LIBBZ2

static retvalue writebz(struct filetorelease *f, const char *data, size_t len) {
	int bzret;

	assert( f->f[ic_bzip2].fd >= 0  );

	if( len == 0 )
		return RET_NOTHING;

	f->bzstream.next_in = (char*)data;
	f->bzstream.avail_in = len;

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
					f->bzoutputbuffer, f->bz_waiting_bytes);
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
	char dummy;

	assert( f->f[ic_bzip2].fd >= 0  );

	f->bzstream.next_in = &dummy;
	f->bzstream.avail_in = 0;

	do {
		f->bzstream.next_out = f->bzoutputbuffer + f->bz_waiting_bytes;
		f->bzstream.avail_out = BZBUFSIZE - f->bz_waiting_bytes;

		bzret = BZ2_bzCompress(&f->bzstream,BZ_FINISH);
		f->bz_waiting_bytes = BZBUFSIZE - f->bzstream.avail_out;

		if( bzret == BZ_RUN_OK || bzret == BZ_STREAM_END ) {
			retvalue r;
			assert( f->bz_waiting_bytes != 0 );
			r = writetofile(&f->f[ic_bzip2],
					f->bzoutputbuffer, f->bz_waiting_bytes);
			assert( r != RET_NOTHING );
			if( RET_WAS_ERROR(r) )
				return r;
			f->bz_waiting_bytes = 0;
		}
	} while( bzret == BZ_RUN_OK );

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
	release->new = TRUE;
	result = RET_OK;

	for( i = ic_uncompressed ; i < ic_count ; i++ ) {
		r = releasefile(release,&file->f[i]);
		if( RET_WAS_ERROR(r) ) {
			release_abortfile(file);
			return r;
		}
		RET_UPDATE(result,r);
	}
	free(file->gzoutputbuffer);
#ifdef HAVE_LIBBZ2
	free(file->bzoutputbuffer);
#endif
	free(file);
	return result;
}


retvalue release_writedata(struct filetorelease *file, const char *data, size_t len) {
	retvalue result,r;

	result = RET_OK;
	/* always call this so that md5sum is calculated */
	r = writetofile(&file->f[ic_uncompressed],data,len);
	RET_UPDATE(result,r);
	if( file->f[ic_gzip].relativefilename != NULL ) {
		r = writegz(file,data,len);
		RET_UPDATE(result,r);
	}
	RET_UPDATE(file->state,result);
#ifdef HAVE_LIBBZ2
	if( file->f[ic_bzip2].relativefilename != NULL ) {
		r = writebz(file,data,len);
		RET_UPDATE(result,r);
	}
	RET_UPDATE(file->state,result);
#endif
	return result;
}

/* Generate a "Release"-file for arbitrary directory */
retvalue release_directorydescription(struct release *release, const struct distribution *distribution,const struct target *target,const char *releasename,bool_t onlyifneeded) {
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
	release_writeheader("Component",target->component);
	release_writeheader("Origin",distribution->origin);
	release_writeheader("Label",distribution->label);
	release_writeheader("Architecture",target->architecture);
	release_writeheader("NotAutomatic",distribution->notautomatic);
	release_writeheader("Description",distribution->description);
	r = release_finishfile(release, f);
	return r;
}

#define CLEARDBT(dbt) {memset(&dbt,0,sizeof(dbt));}
#define SETDBT(dbt,datastr) {const char *my = datastr;memset(&dbt,0,sizeof(dbt));dbt.data=(void *)my;dbt.size=strlen(my)+1;}

static retvalue getcachevalue(struct release *r,const char *relfilename, char **md5sum) {
	int dbret;
	DBT key,data;

	if( r->cachedb == NULL )
		return RET_NOTHING;

	SETDBT(key,relfilename);
	CLEARDBT(data);
	dbret = r->cachedb->get(r->cachedb, NULL, &key, &data, 0);
	if( dbret == 0 ) {
		char *c;
		c = strdup(data.data);
		if( c == NULL )
			return RET_ERROR_OOM;
		else {
			*md5sum = c;
			return RET_OK;
		}
	} else if( dbret == DB_NOTFOUND ) {
		return RET_NOTHING;
	} else {
		r->cachedb->err(r->cachedb, dbret, "release.cache.db:");
		return RET_DBERR(dbret);
	}
}

static retvalue storechecksums(struct release *r) {

	struct release_entry *file;

	for( file = r->files ; file != NULL ; file = file->next ) {
		int dbret;
		DBT key,data;

		if( file->md5sum == NULL || file->relativefilename == NULL )
			continue;

		SETDBT(key,file->relativefilename);

		dbret = r->cachedb->del(r->cachedb, NULL, &key, 0);
		if( dbret < 0 && dbret != DB_NOTFOUND ) {
			r->cachedb->err(r->cachedb, dbret, "release.cache.db:");
			return RET_DBERR(dbret);
		}
		SETDBT(key,file->relativefilename);
		SETDBT(data,file->md5sum);

		dbret = r->cachedb->put(r->cachedb,NULL,&key,&data,DB_NOOVERWRITE);
		if( dbret < 0 ) {
			r->cachedb->err(r->cachedb, dbret, "release.cache.db:");
			return RET_DBERR(dbret);
		}
	}
	return RET_OK;
}
#undef CLEARDBT
#undef SETDBT


/* Generate a main "Release" file for a distribution */
retvalue release_write(/*@only@*/struct release *release, struct distribution *distribution, bool_t onlyifneeded) {
	FILE *f;
	size_t s;
	int e;
	retvalue result,r;
	char buffer[100];
	time_t t;
	struct tm *gmt;
	int i;
	struct release_entry *file;
	bool_t somethingwasdone;

	if( onlyifneeded && !release->new ) {
		release_free(release);
		return RET_NOTHING;
	}

	(void)time(&t);
	gmt = gmtime(&t);
	if( gmt == NULL ) {
		release_free(release);
		return RET_ERROR_OOM;
	}
	s=strftime(buffer,99,"%a, %d %b %Y %H:%M:%S +0000",gmt);
	if( s == 0 || s >= 99) {
		fprintf(stderr,"strftime is doing strange things...\n");
		release_free(release);
		return RET_ERROR;
	}

	release->newreleasefilename = calc_dirconcat(release->dirofdist,"Release.new");
	if( release->newreleasefilename == NULL ) {
		release_free(release);
		return RET_ERROR_OOM;
	}
	release->releasefilename = calc_dirconcat(release->dirofdist,"Release");
	if( release->releasefilename == NULL ) {
		release_free(release);
		return RET_ERROR_OOM;
	}
	(void)dirs_make_parent(release->newreleasefilename);
	(void)unlink(release->newreleasefilename);
	f = fopen(release->newreleasefilename,"w");
	if( f == NULL ) {
		e = errno;
		fprintf(stderr,"Error writing file %s: %m\n",release->newreleasefilename);
		release_free(release);
		return RET_ERRNO(e);
	}
#define checkwritten if( e < 0 ) { \
		e = errno; \
		fprintf(stderr,"Error writing to %s: %d=$m!\n",release->newreleasefilename,e); \
		release_free(release); \
		(void)fclose(f); \
		return RET_ERRNO(e); \
	}

	if( distribution->origin != NULL ) {
		e = fputs("Origin: ",f);
		checkwritten;
		e = fputs(distribution->origin,f);
		checkwritten;
		e = fputc('\n',f) - 1;
		checkwritten;
	}
	if( distribution->label != NULL ) {
		e = fputs("Label: ",f);
		checkwritten;
		e = fputs(distribution->label,f);
		checkwritten;
		e = fputc('\n',f) - 1;
		checkwritten;
	}
	if( distribution->suite != NULL ) {
		e = fputs("Suite: ",f);
		checkwritten;
		e = fputs(distribution->suite,f);
		checkwritten;
		e = fputc('\n',f) - 1;
		checkwritten;
	}
	e = fputs("Codename: ",f);
	checkwritten;
	e = fputs(distribution->codename,f);
	checkwritten;
	if( distribution->version != NULL ) {
		e = fputs("\nVersion: ",f);
		checkwritten;
		e = fputs(distribution->version,f);
		checkwritten;
	}
	e = fputs("\nDate: ",f);
	checkwritten;
	e = fputs(buffer,f);
	checkwritten;
	e = fputs("\nArchitectures:",f);
	checkwritten;
	for( i = 0 ; i < distribution->architectures.count ; i++ ) {
		/* Debian's topmost Release files do not list it, so we won't either */
		if( strcmp(distribution->architectures.values[i],"source") == 0 )
			continue;
		e = fputc(' ',f) - 1;
		checkwritten;
		e = fputs(distribution->architectures.values[i],f);
		checkwritten;
	}
	e = fputs("\nComponents: ",f);
	checkwritten;
	r = strlist_fprint(f,&distribution->components);
	if( RET_WAS_ERROR(r) ) {
		fprintf(stderr,"Error writing to %s!\n",release->newreleasefilename);
		release_free(release);
		(void)fclose(f);
		return r;
	}
	if( distribution->description != NULL ) {
		e = fputs("\nDescription: ",f);
		checkwritten;
		e = fputs(distribution->description,f);
		checkwritten;
	}
	if( distribution->notautomatic != NULL ) {
		e = fputs("\nNotAutomatic: ",f);
		checkwritten;
		e = fputs(distribution->notautomatic,f);
		checkwritten;
	}

	e = fputs("\nMD5Sum:\n",f);
	checkwritten;

	for( file = release->files ; file != NULL ; file = file->next ) {
		if( file->md5sum != NULL && file->relativefilename != NULL ) {
			e = fputc(' ',f) - 1;
			checkwritten;
			e = fputs(file->md5sum,f);
			checkwritten;
			e = fputc(' ',f) - 1;
			checkwritten;
			e = fputs(file->relativefilename,f);
			checkwritten;
			e = fputc('\n',f) - 1;
			checkwritten;
		}
	}

#undef checkwritten
	if( ferror(f) != 0 ) {
		e = errno;
		fprintf(stderr,"Error writing to %s: %d=$m!\n",
				release->newreleasefilename,e);
		release_free(release);
		(void)fclose(f);
		return RET_ERRNO(e);
	}
	if( fclose(f) < 0 ) {
		e = errno;
		fprintf(stderr,"Error writing to %s: %d=$m!\n",
				release->newreleasefilename,e);
		release_free(release);
		return RET_ERRNO(e);
	}

	if( distribution->signwith != NULL ) {

		release->signfilename = calc_dirconcat(release->dirofdist,"Release.gpg");
		if( release->signfilename == NULL ) {
			release_free(release);
			return RET_ERROR_OOM;
		}
		release->newsignfilename = calc_dirconcat(release->dirofdist,"Release.gpg.new");
		if( release->newsignfilename == NULL ) {
			release_free(release);
			return RET_ERROR_OOM;
		}

		r = signature_sign(distribution->signwith,
				release->newreleasefilename,
				release->newsignfilename);
		if( RET_WAS_ERROR(r) ) {
			release_free(release);
			return r;
		}
	}

	somethingwasdone = FALSE;
	result = RET_OK;

	for( file = release->files ; file != NULL ; file = file->next ) {
		if( file->relativefilename == NULL ) {
			assert(file->fulltemporaryfilename != NULL );
			e = unlink(file->fulltemporaryfilename);
			if( e < 0 ) {
				e = errno;
				fprintf(stderr,"Error deleting %s: %m. (Will be ignored)\n",file->fulltemporaryfilename);
			}
		} else if( file->fulltemporaryfilename != NULL ) {
			e = rename(file->fulltemporaryfilename,
					file->fullfinalfilename);
			if( e < 0 ) {
				e = errno;
				fprintf(stderr,"Error moving %s to %s: %d=%m!\n",file->fulltemporaryfilename,file->fullfinalfilename,e);
				r = RET_ERRNO(e);
				/* after something was done, do not stop
				 * but try to do as much as possible */
				if( !somethingwasdone ) {
					release_free(release);
					return r;
				}
				RET_UPDATE(result,r);
			} else
				somethingwasdone = TRUE;
		}
	}
	if( release->newsignfilename != NULL && release->signfilename != NULL ) {
		e = rename(release->newsignfilename,release->signfilename);
		if( e < 0 ) {
			e = errno;
			fprintf(stderr,"Error moving %s to %s: %d=%m!\n",
					release->newsignfilename,
					release->signfilename,e);
			r = RET_ERRNO(e);
			/* after something was done, do not stop
			 * but try to do as much as possible */
			if( !somethingwasdone ) {
				release_free(release);
				return r;
			}
			RET_UPDATE(result,r);
		}
	}
	e = rename(release->newreleasefilename,release->releasefilename);
	if( e < 0 ) {
		e = errno;
		fprintf(stderr,"Error moving %s to %s: %d=%m!\n",
				release->newreleasefilename,
				release->releasefilename,e);
		r = RET_ERRNO(e);
		RET_UPDATE(result,r);
	}

	if( release->cachedb != NULL ) {
	 	int dbret;

		/* now update the cache database, so we find those the next time */
		r = storechecksums(release);
		RET_UPDATE(result,r);

		/* check for an possible error in the cachedb,
		 * release_free return no error */
		dbret = release->cachedb->close(release->cachedb,0);
		if( dbret < 0 ) {
			release_free(release);
			return RET_DBERR(dbret);
		}
		release->cachedb = NULL;
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
