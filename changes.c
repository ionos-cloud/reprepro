/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006,2008 Bernhard R. Link
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
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include "error.h"
#include "names.h"
#include "uncompression.h"
#include "checksums.h"
#include "changes.h"

retvalue changes_parsefileline(const char *fileline, /*@out@*/filetype *result_type, /*@out@*/char **result_basename, /*@out@*/struct hash_data *hash_p, /*@out@*/struct hash_data *size_p, /*@out@*/char **result_section, /*@out@*/char **result_priority, /*@out@*/architecture_t *result_architecture, /*@out@*/char **result_name) {

	const char *p, *md5start, *md5end;
	const char *sizestart, *sizeend;
	const char *sectionstart, *sectionend;
	const char *priostart, *prioend;
	const char *filestart, *nameend, *fileend;
	const char *archstart, *archend;
	const char *versionstart;
	filetype type;
	char *section, *priority, *basefilename, *name;
	architecture_t architecture;
	size_t l;
	bool checkfilename = false;

	p = fileline;
	while (*p !='\0' && xisspace(*p))
		p++;
	md5start = p;
	while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))
		p++;
	if (*p == '\0') {
		fprintf(stderr, "Missing md5sum in '%s'!\n", fileline);
		return RET_ERROR;
	}
	if (!xisspace(*p)) {
		fprintf(stderr, "Malformed md5 hash in '%s'!\n", fileline);
		return RET_ERROR;
	}
	md5end = p;
	while (*p !='\0' && xisspace(*p))
		p++;
	while (*p == '0' && p[1] >= '0' && p[1] <= '9')
		p++;
	sizestart = p;
	while (*p >= '0' && *p <= '9')
		p++;
	if (*p == '\0') {
		fprintf(stderr,
"Missing size (second argument) in '%s'!\n", fileline);
		return RET_ERROR;
	}
	if (!xisspace(*p)) {
		fprintf(stderr,
"Malformed size (second argument) in '%s'!\n", fileline);
		return RET_ERROR;
	}
	sizeend = p;
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
"Unexpected sixth argument in '%s'!\n", fileline);
		return RET_ERROR;
	}
	if (*md5start == '\0' || *sizestart == '\0' || *sectionstart == '\0'
			|| *priostart == '\0' || *filestart == '\0') {
		fprintf(stderr,
"Wrong number of arguments in '%s' (5 expected)!\n",
				fileline);
		return RET_ERROR;
	}
	if ((sectionend - sectionstart == 6 &&
				strncmp(sectionstart, "byhand", 6) == 0) ||
	    (sectionend - sectionstart > 4 &&
				strncmp(sectionstart, "raw-", 4) == 0)) {
		section = strndup(sectionstart, sectionend - sectionstart);
		priority = strndup(priostart, prioend - priostart);
		basefilename = strndup(filestart, fileend - filestart);
		if (FAILEDTOALLOC(section) || FAILEDTOALLOC(priority) ||
		    FAILEDTOALLOC(basefilename)) {
			free(section); free(priority);
			free(basefilename);
			return RET_ERROR_OOM;
		}
		hash_p->start = md5start;
		hash_p->len = md5end - md5start;
		size_p->start = sizestart;
		size_p->len = sizeend - sizestart;
		*result_section = section;
		*result_priority = priority;
		*result_basename = basefilename;
		*result_architecture = atom_unknown;
		*result_name = NULL;
		*result_type = fe_BYHAND;
		return RET_OK;
	}

	p = filestart;
	while (*p != '\0' && *p != '_' && !xisspace(*p))
		p++;
	if (*p != '_') {
		if (*p == '\0')
			fprintf(stderr,
"No underscore found in file name in '%s'!\n",
					fileline);
		else
			fprintf(stderr,
"Unexpected character '%c' in file name in '%s'!\n",
					*p, fileline);
		return RET_ERROR;
	}
	nameend = p;
	p++;
	versionstart = p;

	/* changing 3.0 format to now also allow _ in source files
	 * makes this parsing quite more ugly... */

	while (*p !='\0' && !xisspace(*p))
		p++;
	l = p - versionstart;

	/* identify the binary types (they have no compression
	 * and will need a _ */

	if (l >= 4 && memcmp(p-4, ".deb", 4) == 0)
		type = fe_DEB;
	else if (l >= 5 && memcmp(p-5, ".ddeb", 5) == 0)
		type = fe_DDEB;
	else if (l >= 5 && memcmp(p-5, ".udeb", 5) == 0)
		type = fe_UDEB;
	else
		type = fe_UNKNOWN;

	if (type != fe_UNKNOWN) {
		/* a _ should separate the version from the rest */
		p = versionstart;
		names_overversion(&p, true);
		if (*p != '\0' && *p != '_') {
			fprintf(stderr,
"Unexpected character '%c' in file name within '%s'!\n", *p, fileline);
			return RET_ERROR;
		}
		if (*p != '_') {
			fprintf(stderr,
"Cannot cope with .[u]deb filename not containing an underscore (in '%s')!",
					fileline);
			return RET_ERROR;
		}
		p++;
		archstart = p;
		if (type == fe_DEB)
			archend = versionstart + l - 4;
		else {
			assert (type == fe_DDEB || type == fe_UDEB);
			archend = versionstart + l - 5;
		}
		if (archend - archstart == 6 &&
				strncmp(archstart, "source", 6) == 0) {
			fprintf(stderr,
"Architecture 'source' not allowed for .[u]debs ('%s')!\n", filestart);
			return RET_ERROR;
		}
	} else {
		enum compression c;
		const char *eoi;
		bool issignature = false;

		/* without those, it gets more complicated.
		 * It's not .deb, .ddeb or .udeb, so most likely a
		 * source file (or perhaps a log (reprepro extension)) */

		/* if it uses a known compression, things are easy,
		 * so try this first: */

		if (l > 4 && memcmp(versionstart + l - 4, ".asc", 4) == 0 ) {
			issignature = true;
			l -= 4;
		}
		c = compression_by_suffix(versionstart, &l);
		p = versionstart + l;

		archstart = "source";
		archend = archstart + 6;
		if (l > 9 && strncmp(p-9, ".orig.tar", 9) == 0) {
			type = fe_ORIG;
			eoi = p - 9;
		} else if (l > 4 && strncmp(p-4, ".tar", 4) == 0) {
			type = fe_TAR;
			eoi = p - 4;
		} else if (issignature) {
			/* only .tar.* files are allowed to have .asc files: */
			issignature = false;
		} else if (l > 5 && strncmp(p-5, ".diff", 5) == 0) {
			type = fe_DIFF;
			eoi = p - 5;
		} else if (l > 4 && strncmp(p-4, ".dsc", 4) == 0
				&& c == c_none) {
			type = fe_DSC;
			eoi = p - 4;
		} else if (l > 4 && strncmp(p-4, ".git", 4) == 0
				&& c == c_none) {
			type = fe_ALTSRC;
			eoi = p - 4;
		} else if (l > 4 && strncmp(p-4, ".log", 4) == 0) {
			type = fe_LOG;
			eoi = p - 4;
		} else if (l > 6 && strncmp(p-6, ".build", 6) == 0) {
			type = fe_LOG;
			eoi = p - 6;
		} else if (l > 10 && strncmp(p-10, ".buildinfo", 10) == 0) {
			type = fe_BUILDINFO;
			eoi = p - 10;
		}
		if (type != fe_UNKNOWN) {
			/* check for a proper version */
			p = versionstart;
			names_overversion(&p, true);
			if (p >= eoi) {
				/* all well */
			} else if (type == fe_TAR) {
				/* a tar might be a component with ugly
				 * data between .orig- and the .tar.c */
				const char *o = strstr(versionstart, ".orig-");
				if (o == NULL || o > eoi) {
					fprintf(stderr,
"Unexpected character '%c' in file name within '%s'!\n",
							*p, fileline);
					return RET_ERROR;
				}
				checkfilename = true;
			} else if (type == fe_LOG || type == fe_BUILDINFO) {
				if (*p == '_') {
					archstart = p + 1;
					archend = eoi;
					checkfilename = true;
				} else {
					fprintf(stderr,
"Unexpected character '%c' in file name within '%s'!\n",
						*p, fileline);
				}
			} else {
				fprintf(stderr,
"Unexpected character '%c' in file name within '%s'!\n",
						*p, fileline);
				return RET_ERROR;

			}
			if (issignature)
				type = fe_SIG;
		} else {
			/* everything else is assumed to be source */
			checkfilename = true;
			fprintf(stderr,
"Unknown file type: '%s', assuming source format...\n", fileline);
		}
	}
	section = strndup(sectionstart, sectionend - sectionstart);
	priority = strndup(priostart, prioend - priostart);
	basefilename = strndup(filestart, fileend - filestart);
	// TODO: this does not make much sense for log files, as they might
	// list multiple..
	architecture = architecture_find_l(archstart, archend - archstart);
	name = strndup(filestart, nameend - filestart);
	if (FAILEDTOALLOC(section) || FAILEDTOALLOC(priority) ||
	    FAILEDTOALLOC(basefilename) || FAILEDTOALLOC(name)) {
		free(section); free(priority);
		free(basefilename); free(name);
		return RET_ERROR_OOM;
	}
	if (checkfilename || !atom_defined(architecture)) {
		retvalue r;

		/* as we no longer run properversion over the whole
		 * rest of the string, at least make sure nothing evil
		 * is in this name */
		r = properfilename(basefilename);
		if (!RET_IS_OK(r)) {
			assert (r != RET_NOTHING);
			free(section); free(priority);
			free(basefilename); free(name);
			return r;
		}
	}
	hash_p->start = md5start;
	hash_p->len = md5end - md5start;
	size_p->start = sizestart;
	size_p->len = sizeend - sizestart;
	*result_section = section;
	*result_priority = priority;
	*result_basename = basefilename;
	*result_architecture = architecture;
	*result_name = name;
	*result_type = type;
	return RET_OK;
}
