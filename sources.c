/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006,2007,2008,2009,2010 Bernhard R. Link
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

#include <assert.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "chunks.h"
#include "sources.h"
#include "names.h"
#include "dirs.h"
#include "dpkgversions.h"
#include "override.h"
#include "tracking.h"
#include "signature.h"
#include "package.h"

/* split a "<md5> <size> <filename>" into md5sum and filename */
static retvalue calc_parsefileline(const char *fileline, /*@out@*/char **filename) {
	const char *p, *fn, *fnend;
	char *filen;

	assert (fileline != NULL);
	if (*fileline == '\0')
		return RET_NOTHING;

	/* the md5sums begins after the (perhaps) heading spaces ...  */
	p = fileline;
	while (*p != '\0' && (*p == ' ' || *p == '\t'))
		p++;
	if (*p == '\0')
		return RET_NOTHING;
	/* ... and ends with the following spaces. */
	while (*p != '\0' && !(*p == ' ' || *p == '\t'))
		p++;
	if (*p == '\0') {
		fprintf(stderr, "Expecting more data after md5sum!\n");
		return RET_ERROR;
	}
	/* Then the size of the file is expected: */
	while ((*p == ' ' || *p == '\t'))
		p++;
	while (*p !='\0' && !(*p == ' ' || *p == '\t'))
		p++;
	if (*p == '\0') {
		fprintf(stderr, "Expecting more data after size!\n");
		return RET_ERROR;
	}
	/* Then the filename */
	fn = p;
	while ((*fn == ' ' || *fn == '\t'))
		fn++;
	fnend = fn;
	while (*fnend != '\0' && !(*fnend == ' ' || *fnend == '\t'))
		fnend++;

	filen = strndup(fn, fnend-fn);
	if (FAILEDTOALLOC(filen))
		return RET_ERROR_OOM;
	*filename = filen;
	return RET_OK;
}

static retvalue getBasenames(const struct strlist *filelines, /*@out@*/struct strlist *basenames) {
	int i;
	retvalue r;

	assert (filelines != NULL && basenames != NULL);

	r = strlist_init_n(filelines->count, basenames);
	if (RET_WAS_ERROR(r))
		return r;
	r = RET_NOTHING;
	for (i = 0 ; i < filelines->count ; i++) {
		char *basefilename;
		const char *fileline = filelines->values[i];

		r = calc_parsefileline(fileline, &basefilename);
		if (r == RET_NOTHING) {
			fprintf(stderr, "Malformed Files: line '%s'!\n",
					fileline);
			r = RET_ERROR;
		}
		if (RET_WAS_ERROR(r))
			break;

		r = strlist_add(basenames, basefilename);
		if (RET_WAS_ERROR(r)) {
			break;
		}
		r = RET_OK;
	}
	if (RET_WAS_ERROR(r)) {
		strlist_done(basenames);
	} else {
		assert (filelines->count == basenames->count);
	}
	return r;
}

retvalue sources_getversion(const char *control, char **version) {
	retvalue r;

	r = chunk_getvalue(control, "Version", version);
	if (RET_WAS_ERROR(r))
		return r;
	if (r == RET_NOTHING) {
		fprintf(stderr, "Missing 'Version' field in chunk:'%s'\n",
				control);
		return RET_ERROR;
	}
	return r;
}

retvalue sources_getarchitecture(UNUSED(const char *chunk), architecture_t *architecture_p) {
	*architecture_p = architecture_source;
	return RET_OK;
}

retvalue sources_getinstalldata(const struct target *t, struct package *package, char **control, struct strlist *filekeys, struct checksumsarray *origfiles) {
	retvalue r;
	char *origdirectory, *directory, *mychunk;
	struct strlist myfilekeys;
	struct strlist filelines[cs_hashCOUNT];
	struct checksumsarray files;
	enum checksumtype cs;
	bool gothash = false;
	const char *chunk = package->control;

	assert (package->architecture == architecture_source);

	for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++) {
		assert (source_checksum_names[cs] != NULL);
		r = chunk_getextralinelist(chunk, source_checksum_names[cs],
				&filelines[cs]);
		if (r == RET_NOTHING)
			strlist_init(&filelines[cs]);
		else if (RET_WAS_ERROR(r)) {
			while (cs-- > cs_md5sum) {
				strlist_done(&filelines[cs]);
			}
			return r;
		} else
			gothash = true;
	}
	if (!gothash) {
		fprintf(stderr,
"Missing 'Files' (or 'SHA1' or ...)  entry in '%s'!\n",
				chunk);
		for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++)
			strlist_done(&filelines[cs]);
		return RET_ERROR;
	}
	r = checksumsarray_parse(&files, filelines, package->name);
	for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++) {
		strlist_done(&filelines[cs]);
	}
	if (RET_WAS_ERROR(r))
		return r;

	r = chunk_getvalue(chunk, "Directory", &origdirectory);
	if (r == RET_NOTHING) {
/* Flat repositories can come without this, TODO: add warnings in other cases
		fprintf(stderr, "Missing 'Directory' entry in '%s'!\n", chunk);
		r = RET_ERROR;
*/
		origdirectory = strdup(".");
		if (FAILEDTOALLOC(origdirectory))
			r = RET_ERROR_OOM;
	}
	if (RET_WAS_ERROR(r)) {
		checksumsarray_done(&files);
		return r;
	}

	r = propersourcename(package->name);
	assert (r != RET_NOTHING);
	if (RET_IS_OK(r))
		r = properfilenames(&files.names);
	if (RET_WAS_ERROR(r)) {
		fprintf(stderr,
"Forbidden characters in source package '%s'!\n", package->name);
		free(origdirectory);
		checksumsarray_done(&files);
		return r;
	}

	directory = calc_sourcedir(t->component, package->name);
	if (FAILEDTOALLOC(directory))
		r = RET_ERROR_OOM;
	else
		r = calc_dirconcats(directory, &files.names, &myfilekeys);
	if (RET_WAS_ERROR(r)) {
		free(directory);
		free(origdirectory);
		checksumsarray_done(&files);
		return r;
	}
	r = calc_inplacedirconcats(origdirectory, &files.names);
	free(origdirectory);
	if (!RET_WAS_ERROR(r)) {
		char *n;

		n = chunk_normalize(chunk, "Package", package->name);
		if (FAILEDTOALLOC(n))
			mychunk = NULL;
		else
			mychunk = chunk_replacefield(n,
					"Directory", directory, true);
		free(n);
		if (FAILEDTOALLOC(mychunk))
			r = RET_ERROR_OOM;
	}
	free(directory);
	if (RET_WAS_ERROR(r)) {
		strlist_done(&myfilekeys);
		checksumsarray_done(&files);
		return r;
	}
	*control = mychunk;
	strlist_move(filekeys, &myfilekeys);
	checksumsarray_move(origfiles, &files);
	return RET_OK;
}

retvalue sources_getfilekeys(const char *chunk, struct strlist *filekeys) {
	char *origdirectory;
	struct strlist basenames;
	retvalue r;
	struct strlist filelines;


	/* Read the directory given there */
	r = chunk_getvalue(chunk, "Directory", &origdirectory);
	if (r == RET_NOTHING) {
		//TODO: check if it is even text and do not print
		//of looking binary??
		fprintf(stderr, "Does not look like source control: '%s'\n",
				chunk);
		return RET_ERROR;
	}
	if (RET_WAS_ERROR(r))
		return r;

	r = chunk_getextralinelist(chunk, "Files", &filelines);
	if (r == RET_NOTHING) {
		//TODO: check if it is even text and do not print
		//of looking binary??
		fprintf(stderr, "Does not look like source control: '%s'\n",
				chunk);
		r = RET_ERROR;
	}
	if (RET_WAS_ERROR(r)) {
		free(origdirectory);
		return r;
	}
	r = getBasenames(&filelines, &basenames);
	strlist_done(&filelines);
	if (RET_WAS_ERROR(r)) {
		free(origdirectory);
		return r;
	}

	r = calc_dirconcats(origdirectory, &basenames, filekeys);
	free(origdirectory);
	strlist_done(&basenames);
	return r;
}

retvalue sources_getchecksums(const char *chunk, struct checksumsarray *out) {
	char *origdirectory;
	struct checksumsarray a;
	retvalue r;
	struct strlist filelines[cs_hashCOUNT];
	enum checksumtype cs;

	/* Read the directory given there */
	r = chunk_getvalue(chunk, "Directory", &origdirectory);
	if (!RET_IS_OK(r))
		return r;

	for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++) {
		assert (source_checksum_names[cs] != NULL);
		r = chunk_getextralinelist(chunk, source_checksum_names[cs],
				&filelines[cs]);
		if (r == RET_NOTHING) {
			if (cs == cs_md5sum) {
				fprintf(stderr,
"Missing 'Files' entry in '%s'!\n",
						chunk);
				r = RET_ERROR;
			} else
				strlist_init(&filelines[cs]);
		}
		if (RET_WAS_ERROR(r)) {
			while (cs-- > cs_md5sum) {
				strlist_done(&filelines[cs]);
			}
			free(origdirectory);
			return r;
		}
	}
	r = checksumsarray_parse(&a, filelines, "source chunk");
	for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++) {
		strlist_done(&filelines[cs]);
	}
	if (RET_WAS_ERROR(r)) {
		free(origdirectory);
		return r;
	}

	r = calc_inplacedirconcats(origdirectory, &a.names);
	free(origdirectory);
	if (RET_WAS_ERROR(r)) {
		checksumsarray_done(&a);
		return r;
	}
	checksumsarray_move(out, &a);
	return RET_OK;
}

retvalue sources_doreoverride(const struct target *target, const char *packagename, const char *controlchunk, /*@out@*/char **newcontrolchunk) {
	const struct overridedata *o;
	struct fieldtoadd *fields;
	char *newchunk;
	retvalue r;

	if (interrupted())
		return RET_ERROR_INTERRUPTED;

	o = override_search(target->distribution->overrides.dsc, packagename);
	if (o == NULL)
		return RET_NOTHING;

	r = override_allreplacefields(o, &fields);
	if (!RET_IS_OK(r))
		return r;
	newchunk = chunk_replacefields(controlchunk, fields,
			"Directory", true);
	addfield_free(fields);
	if (FAILEDTOALLOC(newchunk))
		return RET_ERROR_OOM;
	*newcontrolchunk = newchunk;
	return RET_OK;
}

retvalue sources_retrack(const char *sourcename, const char *chunk, trackingdb tracks) {
	retvalue r;
	char *sourceversion;
	struct trackedpackage *pkg;
	struct strlist filekeys;
	int i;

	//TODO: eliminate duplicate code!
	assert(sourcename!=NULL);

	if (interrupted())
		return RET_ERROR_INTERRUPTED;

	r = chunk_getvalue(chunk, "Version", &sourceversion);
	if (r == RET_NOTHING) {
		fprintf(stderr, "Missing 'Version' field in chunk:'%s'\n",
				chunk);
		r = RET_ERROR;
	}
	if (RET_WAS_ERROR(r)) {
		return r;
	}

	r = sources_getfilekeys(chunk, &filekeys);
	if (r == RET_NOTHING) {
		fprintf(stderr, "Malformed source control:'%s'\n", chunk);
		r = RET_ERROR;
	}
	if (RET_WAS_ERROR(r)) {
		free(sourceversion);
		return r;
	}

	r = tracking_getornew(tracks, sourcename, sourceversion, &pkg);
	free(sourceversion);
	if (RET_WAS_ERROR(r)) {
		strlist_done(&filekeys);
		return r;
	}

	// TODO: error handling is suboptimal here.
	//  is there a way to again remove old additions (esp. references)
	//  where something fails?
	for (i = 0 ; i < filekeys.count ; i++) {
		r = trackedpackage_addfilekey(tracks, pkg,
				ft_SOURCE, filekeys.values[i], true);
		filekeys.values[i] = NULL;
		if (RET_WAS_ERROR(r)) {
			strlist_done(&filekeys);
			trackedpackage_free(pkg);
			return r;
		}
	}
	strlist_done(&filekeys);
	return tracking_save(tracks, pkg);
}

retvalue sources_getsourceandversion(const char *chunk, const char *packagename, char **source, char **version) {
	retvalue r;
	char *sourceversion;
	char *sourcename;

	//TODO: eliminate duplicate code!
	assert(packagename!=NULL);

	r = chunk_getvalue(chunk, "Version", &sourceversion);
	if (r == RET_NOTHING) {
		fprintf(stderr, "Missing 'Version' field in chunk:'%s'\n",
				chunk);
		r = RET_ERROR;
	}
	if (RET_WAS_ERROR(r)) {
		return r;
	}
	sourcename = strdup(packagename);
	if (FAILEDTOALLOC(sourcename)) {
		free(sourceversion);
		return RET_ERROR_OOM;
	}
	*source = sourcename;
	*version = sourceversion;
	return RET_OK;
}

/****************************************************************/

static inline retvalue getvalue(const char *filename, const char *chunk, const char *field, char **value) {
	retvalue r;

	r = chunk_getvalue(chunk, field, value);
	if (r == RET_NOTHING) {
		fprintf(stderr, "Missing '%s' field in %s!\n",
				field, filename);
		r = RET_ERROR;
	}
	return r;
}

static inline retvalue checkvalue(const char *filename, const char *chunk, const char *field) {
	retvalue r;

	r = chunk_checkfield(chunk, field);
	if (r == RET_NOTHING) {
		fprintf(stderr, "Cannot find '%s' field in %s!\n",
				field, filename);
		r = RET_ERROR;
	}
	return r;
}

static inline retvalue getvalue_n(const char *chunk, const char *field, char **value) {
	retvalue r;

	r = chunk_getvalue(chunk, field, value);
	if (r == RET_NOTHING) {
		*value = NULL;
	}
	return r;
}

retvalue sources_readdsc(struct dsc_headers *dsc, const char *filename, const char *filenametoshow, bool *broken) {
	retvalue r;
	struct strlist filelines[cs_hashCOUNT];
	enum checksumtype cs;

	r = signature_readsignedchunk(filename, filenametoshow,
			&dsc->control, NULL, broken);
	if (RET_WAS_ERROR(r)) {
		return r;
	}
	if (verbose > 100) {
		fprintf(stderr, "Extracted control chunk from '%s': '%s'\n",
				filenametoshow, dsc->control);
	}

	/* first look for fields that should be there */

	r = chunk_getname(dsc->control, "Source", &dsc->name, false);
	if (r == RET_NOTHING) {
		fprintf(stderr, "Missing 'Source' field in %s!\n",
				filenametoshow);
		return RET_ERROR;
	}
	if (RET_WAS_ERROR(r))
		return r;

	/* This is needed and cannot be ignored unless
	 * sources_complete is changed to not need it */
	r = checkvalue(filenametoshow, dsc->control, "Format");
	if (RET_WAS_ERROR(r))
		return r;

	r = checkvalue(filenametoshow, dsc->control, "Maintainer");
	if (RET_WAS_ERROR(r))
		return r;

	r = getvalue(filenametoshow, dsc->control, "Version", &dsc->version);
	if (RET_WAS_ERROR(r))
		return r;

	r = getvalue_n(dsc->control, SECTION_FIELDNAME, &dsc->section);
	if (RET_WAS_ERROR(r))
		return r;
	r = getvalue_n(dsc->control, PRIORITY_FIELDNAME, &dsc->priority);
	if (RET_WAS_ERROR(r))
		return r;

	for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++) {
		assert (source_checksum_names[cs] != NULL);
		r = chunk_getextralinelist(dsc->control,
				source_checksum_names[cs], &filelines[cs]);
		if (r == RET_NOTHING) {
			if (cs == cs_md5sum) {
				fprintf(stderr,
"Missing 'Files' field in '%s'!\n",
					filenametoshow);
				r = RET_ERROR;
			} else
				strlist_init(&filelines[cs]);
		}
		if (RET_WAS_ERROR(r)) {
			while (cs-- > cs_md5sum) {
				strlist_done(&filelines[cs]);
			}
			return r;
		}
	}
	r = checksumsarray_parse(&dsc->files, filelines, filenametoshow);
	for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++) {
		strlist_done(&filelines[cs]);
	}
	return r;
}

void sources_done(struct dsc_headers *dsc) {
	free(dsc->name);
	free(dsc->version);
	free(dsc->control);
	checksumsarray_done(&dsc->files);
	free(dsc->section);
	free(dsc->priority);
}

retvalue sources_complete(const struct dsc_headers *dsc, const char *directory, const struct overridedata *override, const char *section, const char *priority, char **newcontrol) {
	retvalue r;
	struct fieldtoadd *replace;
	char *newchunk, *newchunk2;
	char *newfilelines, *newsha1lines, *newsha256lines;

	assert(section != NULL && priority != NULL);

	newchunk2 = chunk_normalize(dsc->control, "Package", dsc->name);
	if (FAILEDTOALLOC(newchunk2))
		return RET_ERROR_OOM;

	r = checksumsarray_genfilelist(&dsc->files,
			&newfilelines, &newsha1lines, &newsha256lines);
	if (RET_WAS_ERROR(r)) {
		free(newchunk2);
		return r;
	}
	assert (newfilelines != NULL);
	replace = aodfield_new("Checksums-Sha256", newsha256lines, NULL);
	if (!FAILEDTOALLOC(replace))
		replace = aodfield_new("Checksums-Sha1", newsha1lines, replace);
	if (!FAILEDTOALLOC(replace))
		replace = deletefield_new("Source", replace);
	if (!FAILEDTOALLOC(replace))
		replace = addfield_new("Files", newfilelines, replace);
	if (!FAILEDTOALLOC(replace))
		replace = addfield_new("Directory", directory, replace);
	if (!FAILEDTOALLOC(replace))
		replace = deletefield_new("Status", replace);
	if (!FAILEDTOALLOC(replace))
		replace = addfield_new(SECTION_FIELDNAME, section, replace);
	if (!FAILEDTOALLOC(replace))
		replace = addfield_new(PRIORITY_FIELDNAME, priority, replace);
	if (!FAILEDTOALLOC(replace))
		replace = override_addreplacefields(override, replace);
	if (FAILEDTOALLOC(replace)) {
		free(newsha256lines);
		free(newsha1lines);
		free(newfilelines);
		free(newchunk2);
		return RET_ERROR_OOM;
	}

	newchunk  = chunk_replacefields(newchunk2, replace, "Files", true);
	free(newsha256lines);
	free(newsha1lines);
	free(newfilelines);
	free(newchunk2);
	addfield_free(replace);
	if (FAILEDTOALLOC(newchunk)) {
		return RET_ERROR_OOM;
	}

	*newcontrol = newchunk;

	return RET_OK;
}

/* update Checksums */
retvalue sources_complete_checksums(const char *chunk, const struct strlist *filekeys, struct checksums **c, char **out) {
	struct fieldtoadd *replace;
	char *newchunk;
	char *newfilelines, *newsha1lines, *newsha256lines;
	struct checksumsarray checksums;
	retvalue r;
	int i;

	/* fake a checksumarray... */
	checksums.checksums = c;
	checksums.names.count = filekeys->count;
	checksums.names.values = nzNEW(filekeys->count, char *);
	if (FAILEDTOALLOC(checksums.names.values))
		return RET_ERROR_OOM;
	for (i = 0 ; i < filekeys->count ; i++) {
		checksums.names.values[i] = (char*)
			dirs_basename(filekeys->values[i]);
	}

	r = checksumsarray_genfilelist(&checksums,
			&newfilelines, &newsha1lines, &newsha256lines);
	free(checksums.names.values);
	if (RET_WAS_ERROR(r))
		return r;
	assert (newfilelines != NULL);
	replace = aodfield_new("Checksums-Sha256", newsha256lines, NULL);
	if (!FAILEDTOALLOC(replace))
		replace = aodfield_new("Checksums-Sha1", newsha1lines, replace);
	if (!FAILEDTOALLOC(replace))
		replace = addfield_new("Files", newfilelines, replace);
	if (FAILEDTOALLOC(replace)) {
		free(newsha256lines);
		free(newsha1lines);
		free(newfilelines);
		return RET_ERROR_OOM;
	}
	newchunk = chunk_replacefields(chunk, replace, "Files", true);
	free(newsha256lines);
	free(newsha1lines);
	free(newfilelines);
	addfield_free(replace);
	if (FAILEDTOALLOC(newchunk))
		return RET_ERROR_OOM;

	*out = newchunk;
	return RET_OK;
}

char *calc_source_basename(const char *name, const char *version) {
	const char *v = strchr(version, ':');
	if (v != NULL)
		v++;
	else
		v = version;
	return mprintf("%s_%s.dsc", name, v);
}

char *calc_sourcedir(component_t component, const char *sourcename) {

	assert (*sourcename != '\0');

	if (sourcename[0] == 'l' && sourcename[1] == 'i' &&
			sourcename[2] == 'b' && sourcename[3] != '\0')
		return mprintf("pool/%s/lib%c/%s",
				atoms_components[component],
				sourcename[3], sourcename);
	else if (*sourcename != '\0')
		return mprintf("pool/%s/%c/%s",
				atoms_components[component],
				sourcename[0], sourcename);
	else
		return NULL;
}

char *calc_filekey(component_t component, const char *sourcename, const char *filename) {
	if (sourcename[0] == 'l' && sourcename[1] == 'i' &&
			sourcename[2] == 'b' && sourcename[3] != '\0')
		return mprintf("pool/%s/lib%c/%s/%s",
				atoms_components[component],
				sourcename[3], sourcename, filename);
	else if (*sourcename != '\0')
		return mprintf("pool/%s/%c/%s/%s",
				atoms_components[component],
				sourcename[0], sourcename, filename);
	else
		return NULL;
}

char *calc_byhanddir(component_t component, const char *sourcename, const char *version) {
	if (sourcename[0] == 'l' && sourcename[1] == 'i' &&
			sourcename[2] == 'b' && sourcename[3] != '\0')
		return mprintf("pool/%s/lib%c/%s/%s_%s_byhand",
				atoms_components[component],
				sourcename[3], sourcename,
				sourcename, version);
	else if (*sourcename != '\0')
		return mprintf("pool/%s/%c/%s/%s_%s_byhand",
				atoms_components[component],
				sourcename[0], sourcename,
				sourcename, version);
	else
		return NULL;
}
