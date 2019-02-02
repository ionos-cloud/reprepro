/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006,2007,2009,2010 Bernhard R. Link
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
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"
#include "chunks.h"
#include "sources.h"
#include "binaries.h"
#include "names.h"
#include "dpkgversions.h"
#include "log.h"
#include "override.h"
#include "tracking.h"
#include "debfile.h"
#include "package.h"

static const char * const deb_checksum_headers[cs_COUNT] = {
	"MD5sum", "SHA1", "SHA256", "Size"};

static char *calc_binary_basename(const char *name, const char *version, architecture_t arch, packagetype_t packagetype) {
	const char *v;
	assert (name != NULL);
	assert (version != NULL);
	assert (atom_defined(arch));
	assert (atom_defined(packagetype));
	v = strchr(version, ':');
	if (v != NULL)
		v++;
	else
		v = version;
	return mprintf("%s_%s_%s.%s", name, v, atoms_architectures[arch],
			atoms_packagetypes[packagetype]);
}


/* get checksums out of a "Packages"-chunk. */
static retvalue binaries_parse_checksums(const char *chunk, /*@out@*/struct checksums **checksums_p) {
	retvalue result, r;
	char *checksums[cs_COUNT];
	enum checksumtype type;
	bool gothash = false;

	result = RET_NOTHING;

	for (type = 0 ; type < cs_COUNT ; type++) {
		checksums[type] = NULL;
		r = chunk_getvalue(chunk, deb_checksum_headers[type],
				&checksums[type]);
		if (type != cs_length && RET_IS_OK(r))
			gothash = true;
		RET_UPDATE(result, r);
	}
	if (!gothash) {
		fprintf(stderr,
"No checksums found in binary control chunk:\n '%s'\n",
				chunk);
		RET_UPDATE(result, RET_ERROR_MISSING);
	}
	if (checksums[cs_length] == NULL) {
		fprintf(stderr,
"Missing 'Size' line in binary control chunk:\n '%s'\n",
				chunk);
		RET_UPDATE(result, RET_ERROR_MISSING);
	}
	if (RET_WAS_ERROR(result)) {
		for (type = 0 ; type < cs_COUNT ; type++)
			free(checksums[type]);
		return result;
	}
	return checksums_init(checksums_p, checksums);
}

retvalue binaries_getarchitecture(const char *chunk, architecture_t *architecture_p) {
	char *parch;
	retvalue r;

	r = chunk_getvalue(chunk, "Architecture", &parch);
	if (r == RET_NOTHING) {
		fprintf(stderr,
"Internal Error: Missing Architecture: header in '%s'!\n",
				chunk);
		return RET_ERROR;
	}
	if (RET_WAS_ERROR(r))
		return r;
	*architecture_p = architecture_find(parch);
	free(parch);

	if (!atom_defined(*architecture_p)) {
		fprintf(stderr,
"Internal Error: Unexpected Architecture: header in '%s'!\n",
				chunk);
		return RET_ERROR;
	}
	return RET_OK;
}

/* get somefields out of a "Packages.gz"-chunk.
 * returns RET_OK on success, RET_NOTHING if incomplete, error otherwise */
static retvalue binaries_calc_basename(struct package *package, /*@out@*/char **basename_p) {
	retvalue r;
	char *mybasename;

	assert (package->name != NULL);
	assert (package->version != NULL);

	r = properpackagename(package->name);
	if (!RET_WAS_ERROR(r))
		r = properversion(package->version);
	if (RET_WAS_ERROR(r)) {
		return r;
	}
	mybasename = calc_binary_basename(package->name,
			package->version,
			package->architecture,
			package->target->packagetype);
	if (FAILEDTOALLOC(mybasename)) {
		return RET_ERROR_OOM;
	}

	*basename_p = mybasename;
	return RET_OK;
}

/* get files out of a "Packages.gz"-chunk. */
retvalue binaries_getfilekeys(const char *chunk, struct strlist *files) {
	retvalue r;
	char *filename;

	/* Read the filename given there */
	r = chunk_getvalue(chunk, "Filename", &filename);
	if (!RET_IS_OK(r)) {
		if (r == RET_NOTHING) {
			fprintf(stderr,
"Data does not look like binary control: '%s'\n",
					chunk);
			r = RET_ERROR;
		}
		return r;
	}
	r = strlist_init_singleton(filename, files);
	return r;
}

static retvalue calcfilekeys(component_t component, const char *sourcename, const char *basefilename, struct strlist *filekeys) {
	char *filekey;
	retvalue r;

	r = propersourcename(sourcename);
	if (RET_WAS_ERROR(r)) {
		return r;
	}
	filekey = calc_filekey(component, sourcename, basefilename);
	if (FAILEDTOALLOC(filekey))
		return RET_ERROR_OOM;
	r = strlist_init_singleton(filekey, filekeys);
	return r;
}

static inline retvalue calcnewcontrol(const char *chunk, const char *packagename, const char *sourcename, const char *basefilename, component_t component, struct strlist *filekeys, char **newchunk) {
	retvalue r;
	char *n;


	n = chunk_normalize(chunk, "Package", packagename);
	if (FAILEDTOALLOC(n))
		return RET_ERROR_OOM;
	r = calcfilekeys(component, sourcename, basefilename, filekeys);
	if (RET_WAS_ERROR(r)) {
		free(n);
		return r;
	}

	assert (filekeys->count == 1);
	*newchunk = chunk_replacefield(n, "Filename",
			filekeys->values[0], false);
	free(n);
	if (FAILEDTOALLOC(*newchunk)) {
		strlist_done(filekeys);
		return RET_ERROR_OOM;
	}
	return RET_OK;
}

retvalue binaries_getversion(const char *control, char **version) {
	retvalue r;

	r = chunk_getvalue(control, "Version", version);
	if (RET_WAS_ERROR(r))
		return r;
	if (r == RET_NOTHING) {
		fprintf(stderr,
"Missing 'Version' field in chunk:'%s'\n",
				control);
		return RET_ERROR;
	}
	return r;
}

retvalue binaries_getinstalldata(const struct target *t, struct package *package, char **control, struct strlist *filekeys, struct checksumsarray *origfiles) {
	char *basefilename;
	struct checksumsarray origfilekeys;
	retvalue r;
	const char *chunk = package->control;

	assert (t->packagetype == package->target->packagetype);

	r = package_getsource(package);
	if (RET_WAS_ERROR(r))
		return r;

	r = binaries_calc_basename(package, &basefilename);
	if (RET_WAS_ERROR(r))
		return RET_ERROR;
	r = binaries_getchecksums(chunk, &origfilekeys);
	if (RET_WAS_ERROR(r)) {
		free(basefilename);
		return r;
	}

	r = calcnewcontrol(chunk, package->name, package->source,
			basefilename,
			t->component, filekeys, control);
	if (RET_WAS_ERROR(r)) {
		checksumsarray_done(&origfilekeys);
	} else {
		assert (r != RET_NOTHING);
		checksumsarray_move(origfiles, &origfilekeys);
	}
	free(basefilename);
	return r;
}

retvalue binaries_getchecksums(const char *chunk, struct checksumsarray *filekeys) {
	retvalue r;
	struct checksumsarray a;

	r = binaries_getfilekeys(chunk, &a.names);
	if (RET_WAS_ERROR(r))
		return r;
	assert (a.names.count == 1);
	a.checksums = NEW(struct checksums *);
	if (FAILEDTOALLOC(a.checksums)) {
		strlist_done(&a.names);
		return RET_ERROR_OOM;
	}
	r = binaries_parse_checksums(chunk, a.checksums);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r)) {
		free(a.checksums);
		strlist_done(&a.names);
		return r;
	}
	checksumsarray_move(filekeys, &a);
	return RET_OK;
}

retvalue binaries_doreoverride(const struct target *target, const char *packagename, const char *controlchunk, /*@out@*/char **newcontrolchunk) {
	const struct overridedata *o;
	struct fieldtoadd *fields;
	char *newchunk;
	retvalue r;

	if (interrupted())
		return RET_ERROR_INTERRUPTED;

	o = override_search(target->distribution->overrides.deb, packagename);
	if (o == NULL)
		return RET_NOTHING;

	r = override_allreplacefields(o, &fields);
	if (!RET_IS_OK(r))
		return r;
	newchunk = chunk_replacefields(controlchunk, fields, "Filename", false);
	addfield_free(fields);
	if (FAILEDTOALLOC(newchunk))
		return RET_ERROR_OOM;
	*newcontrolchunk = newchunk;
	return RET_OK;
}

retvalue ubinaries_doreoverride(const struct target *target, const char *packagename, const char *controlchunk, /*@out@*/char **newcontrolchunk) {
	const struct overridedata *o;
	struct fieldtoadd *fields;
	char *newchunk;
	retvalue r;

	if (interrupted())
		return RET_ERROR_INTERRUPTED;

	o = override_search(target->distribution->overrides.udeb, packagename);
	if (o == NULL)
		return RET_NOTHING;

	r = override_allreplacefields(o, &fields);
	if (!RET_IS_OK(r))
		return r;
	newchunk = chunk_replacefields(controlchunk, fields, "Filename", true);
	addfield_free(fields);
	if (FAILEDTOALLOC(newchunk))
		return RET_ERROR_OOM;
	*newcontrolchunk = newchunk;
	return RET_OK;
}

retvalue binaries_retrack(const char *packagename, const char *chunk, trackingdb tracks) {
	retvalue r;
	const char *sourcename;
	char *fsourcename, *sourceversion, *arch, *filekey;
	enum filetype filetype;
	struct trackedpackage *pkg;

	//TODO: eliminate duplicate code!
	assert(packagename!=NULL);

	if (interrupted())
		return RET_ERROR_INTERRUPTED;

	/* is there a sourcename */
	r = chunk_getnameandversion(chunk, "Source",
			&fsourcename, &sourceversion);
	if (RET_WAS_ERROR(r))
		return r;
	if (r == RET_NOTHING) {
		sourceversion = NULL;
		sourcename = packagename;
		fsourcename = NULL;
	} else {
		sourcename = fsourcename;
	}
	if (sourceversion == NULL) {
		// Think about binNMUs, can something be done here?
		r = chunk_getvalue(chunk, "Version", &sourceversion);
		if (RET_WAS_ERROR(r)) {
			free(fsourcename);
			return r;
		}
		if (r == RET_NOTHING) {
			free(fsourcename);
			fprintf(stderr,
"Missing 'Version' field in chunk:'%s'\n",
					chunk);
			return RET_ERROR;
		}
	}

	r = chunk_getvalue(chunk, "Architecture", &arch);
	if (r == RET_NOTHING) {
		fprintf(stderr, "No Architecture field in chunk:'%s'\n",
				chunk);
		r = RET_ERROR;
	}
	if (RET_WAS_ERROR(r)) {
		free(sourceversion);
		free(fsourcename);
		return r;
	}
	if (strcmp(arch, "all") == 0) {
		filetype = ft_ALL_BINARY;
	} else {
		filetype = ft_ARCH_BINARY;
	}
	free(arch);

	r = chunk_getvalue(chunk, "Filename", &filekey);
	if (!RET_IS_OK(r)) {
		if (r == RET_NOTHING) {
			fprintf(stderr, "No Filename field in chunk: '%s'\n",
					chunk);
			r = RET_ERROR;
		}
		free(sourceversion);
		free(fsourcename);
		return r;
	}
	r = tracking_getornew(tracks, sourcename, sourceversion, &pkg);
	free(fsourcename);
	free(sourceversion);
	if (RET_WAS_ERROR(r)) {
		free(filekey);
		return r;
	}
	assert (r != RET_NOTHING);
	r = trackedpackage_addfilekey(tracks, pkg, filetype, filekey, true);
	if (RET_WAS_ERROR(r)) {
		trackedpackage_free(pkg);
		return r;
	}
	return tracking_save(tracks, pkg);
}

retvalue binaries_getsourceandversion(const char *chunk, const char *packagename, char **source, char **version) {
	retvalue r;
	char *sourcename, *sourceversion;

	//TODO: eliminate duplicate code!
	assert(packagename!=NULL);

	/* is there a sourcename */
	r = chunk_getnameandversion(chunk, "Source",
			&sourcename, &sourceversion);
	if (RET_WAS_ERROR(r))
		return r;
	if (r == RET_NOTHING) {
		sourceversion = NULL;
		sourcename = strdup(packagename);
		if (FAILEDTOALLOC(sourcename))
			return RET_ERROR_OOM;
	}
	if (sourceversion == NULL) {
		r = chunk_getvalue(chunk, "Version", &sourceversion);
		if (RET_WAS_ERROR(r)) {
			free(sourcename);
			return r;
		}
		if (r == RET_NOTHING) {
			free(sourcename);
			fprintf(stderr, "No Version field in chunk:'%s'\n",
					chunk);
			return RET_ERROR;
		}
	}
	*source = sourcename;
	*version = sourceversion;
	return RET_OK;
}

static inline retvalue getvalue(const char *filename, const char *chunk, const char *field, char **value) {
	retvalue r;

	r = chunk_getvalue(chunk, field, value);
	if (r == RET_NOTHING) {
		fprintf(stderr, "No %s field in %s's control file!\n",
				field, filename);
		r = RET_ERROR;
	}
	return r;
}

static inline retvalue checkvalue(const char *filename, const char *chunk, const char *field) {
	retvalue r;

	r = chunk_checkfield(chunk, field);
	if (r == RET_NOTHING) {
		fprintf(stderr, "No %s field in %s's control file!\n",
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

void binaries_debdone(struct deb_headers *pkg) {
	free(pkg->name);free(pkg->version);
	free(pkg->source);free(pkg->sourceversion);
	free(pkg->control);
	free(pkg->section);
	free(pkg->priority);
}

retvalue binaries_readdeb(struct deb_headers *deb, const char *filename) {
	retvalue r;
	char *architecture;

	r = extractcontrol(&deb->control, filename);
	if (RET_WAS_ERROR(r))
		return r;
	/* first look for fields that should be there */

	r = chunk_getname(deb->control, "Package", &deb->name, false);
	if (r == RET_NOTHING) {
		fprintf(stderr, "Missing 'Package' field in %s!\n", filename);
		r = RET_ERROR;
	}
	if (RET_WAS_ERROR(r))
		return r;
	r = checkvalue(filename, deb->control, "Maintainer");
	if (RET_WAS_ERROR(r))
		return r;
	r = checkvalue(filename, deb->control, "Description");
	if (RET_WAS_ERROR(r))
		return r;
	r = getvalue(filename, deb->control, "Version", &deb->version);
	if (RET_WAS_ERROR(r))
		return r;
	r = getvalue(filename, deb->control, "Architecture", &architecture);
	if (RET_WAS_ERROR(r))
		return r;
	r = properfilenamepart(architecture);
	if (RET_WAS_ERROR(r)) {
		free(architecture);
		return r;
	}
	r = architecture_intern(architecture, &deb->architecture);
	free(architecture);
	if (RET_WAS_ERROR(r))
		return r;
	/* can be there, otherwise we also know what it is */
	r = chunk_getnameandversion(deb->control, "Source",
			&deb->source, &deb->sourceversion);
	if (r == RET_NOTHING) {
		deb->source = strdup(deb->name);
		if (FAILEDTOALLOC(deb->source))
			r = RET_ERROR_OOM;
	}
	if (RET_WAS_ERROR(r))
		return r;
	if (deb->sourceversion == NULL) {
		deb->sourceversion = strdup(deb->version);
		if (FAILEDTOALLOC(deb->sourceversion))
			return RET_ERROR_OOM;
	}

	/* normally there, but optional: */

	r = getvalue_n(deb->control, PRIORITY_FIELDNAME, &deb->priority);
	if (RET_WAS_ERROR(r))
		return r;
	r = getvalue_n(deb->control, SECTION_FIELDNAME, &deb->section);
	if (RET_WAS_ERROR(r))
		return r;
	return RET_OK;
}

/* do overwrites, add Filename and Checksums to the control-item */
retvalue binaries_complete(const struct deb_headers *pkg, const char *filekey, const struct checksums *checksums, const struct overridedata *override, const char *section, const char *priority, char **newcontrol) {
	struct fieldtoadd *replace;
	char *normalchunk, *newchunk;
	enum checksumtype type;

	assert (section != NULL && priority != NULL);
	assert (filekey != NULL && checksums != NULL);

	replace = NULL;
	for (type = 0 ; type < cs_COUNT ; type++) {
		const char *start;
		size_t len;
		if (checksums_getpart(checksums, type, &start, &len)) {
			replace = addfield_newn(deb_checksum_headers[type],
					start, len, replace);
			if (FAILEDTOALLOC(replace))
				return RET_ERROR_OOM;
		}
	}
	replace = addfield_new("Filename", filekey, replace);
	if (FAILEDTOALLOC(replace))
		return RET_ERROR_OOM;
	replace = addfield_new(SECTION_FIELDNAME, section, replace);
	if (FAILEDTOALLOC(replace))
		return RET_ERROR_OOM;
	replace = addfield_new(PRIORITY_FIELDNAME, priority, replace);
	if (FAILEDTOALLOC(replace))
		return RET_ERROR_OOM;

	replace = override_addreplacefields(override, replace);
	if (FAILEDTOALLOC(replace))
		return RET_ERROR_OOM;

	normalchunk = chunk_normalize(pkg->control,
			"Package", pkg->name);
	if (FAILEDTOALLOC(normalchunk))
		newchunk = NULL;
	else
		newchunk = chunk_replacefields(normalchunk, replace,
				"Description", true);
	free(normalchunk);
	addfield_free(replace);
	if (FAILEDTOALLOC(newchunk)) {
		return RET_ERROR_OOM;
	}

	*newcontrol = newchunk;

	return RET_OK;
}

/* update Checksums */
retvalue binaries_complete_checksums(const char *chunk, const struct strlist *filekeys, struct checksums **c, char **out) {
	struct fieldtoadd *replace;
	char *newchunk;
	enum checksumtype type;
	const struct checksums *checksums;

	assert (filekeys->count == 1);
	checksums = c[0];

	replace = NULL;
	for (type = 0 ; type < cs_COUNT ; type++) {
		const char *start;
		size_t len;
		if (checksums_getpart(checksums, type, &start, &len)) {
			replace = addfield_newn(deb_checksum_headers[type],
					start, len, replace);
			if (FAILEDTOALLOC(replace))
				return RET_ERROR_OOM;
		}
	}
	newchunk = chunk_replacefields(chunk, replace,
			"Description", true);
	addfield_free(replace);
	if (FAILEDTOALLOC(newchunk))
		return RET_ERROR_OOM;
	*out = newchunk;
	return RET_OK;
}

retvalue binaries_adddeb(const struct deb_headers *deb, const struct atomlist *forcearchitectures, packagetype_t packagetype, struct distribution *distribution, struct trackingdata *trackingdata, component_t component, const struct strlist *filekeys, const char *control) {
	retvalue r, result;
	int i;

	assert (logger_isprepared(distribution->logger));

	/* finally put it into one or more architectures of the distribution */

	result = RET_NOTHING;

	if (deb->architecture != architecture_all) {
		struct target *t = distribution_getpart(distribution,
				component, deb->architecture,
				packagetype);
		r = target_initpackagesdb(t, READWRITE);
		if (!RET_WAS_ERROR(r)) {
			retvalue r2;
			if (interrupted())
				r = RET_ERROR_INTERRUPTED;
			else
				r = target_addpackage(t, distribution->logger,
						deb->name, deb->version,
						control,
						filekeys,
						false,
						trackingdata,
						deb->architecture,
						NULL, NULL);
			r2 = target_closepackagesdb(t);
			RET_ENDUPDATE(r, r2);
		}
		RET_UPDATE(result, r);
		RET_UPDATE(distribution->status, result);
		return result;
	}
	/* It's an architecture all package */

	/* if -A includes all, it is added everywhere, as if no -A was
	 * given. (as it behaved this way when there was only one -A possible,
	 * and to allow incoming to force it into architecture 'all' )
	 * */
	if (forcearchitectures != NULL &&
			atomlist_in(forcearchitectures, architecture_all))
		forcearchitectures = NULL;

	for (i = 0 ; i < distribution->architectures.count ; i++) {
		/*@dependent@*/struct target *t;
		architecture_t a = distribution->architectures.atoms[i];

		if (a == architecture_source)
			continue;
		if (forcearchitectures != NULL &&
				!atomlist_in(forcearchitectures, a))
			continue;
		t = distribution_getpart(distribution,
				component, a, packagetype);
		r = target_initpackagesdb(t, READWRITE);
		if (!RET_WAS_ERROR(r)) {
			retvalue r2;
			if (interrupted())
				r = RET_ERROR_INTERRUPTED;
			else
				r = target_addpackage(t, distribution->logger,
						deb->name, deb->version,
						control,
						filekeys,
						false,
						trackingdata,
						deb->architecture,
						NULL, NULL);
			r2 = target_closepackagesdb(t);
			RET_ENDUPDATE(r, r2);
		}
		RET_UPDATE(result, r);
	}
	RET_UPDATE(distribution->status, result);
	return result;
}

static inline retvalue checkadddeb(struct distribution *distribution, component_t component, architecture_t architecture, packagetype_t packagetype, bool tracking, const struct deb_headers *deb, bool permitnewerold) {
	retvalue r;
	struct target *t;

	t = distribution_getpart(distribution,
			component, architecture, packagetype);
	assert (t != NULL);
	r = target_initpackagesdb(t, READONLY);
	if (!RET_WAS_ERROR(r)) {
		retvalue r2;
		if (interrupted())
			r = RET_ERROR_INTERRUPTED;
		else
			r = target_checkaddpackage(t,
					deb->name, deb->version,
					tracking,
					permitnewerold);
		r2 = target_closepackagesdb(t);
		RET_ENDUPDATE(r, r2);
	}
	return r;
}

retvalue binaries_checkadddeb(const struct deb_headers *deb, architecture_t forcearchitecture, packagetype_t packagetype, struct distribution *distribution, bool tracking, component_t component, bool permitnewerold) {
	retvalue r, result;
	int i;

	/* finally put it into one or more architectures of the distribution */
	result = RET_NOTHING;

	if (deb->architecture != architecture_all) {
		r = checkadddeb(distribution,
				component, deb->architecture, packagetype,
				tracking, deb, permitnewerold);
		RET_UPDATE(result, r);
	} else if (atom_defined(forcearchitecture)
			&& forcearchitecture != architecture_all) {
		r = checkadddeb(distribution,
				component, forcearchitecture, packagetype,
				tracking, deb, permitnewerold);
		RET_UPDATE(result, r);
	} else for (i = 0 ; i < distribution->architectures.count ; i++) {
		architecture_t a = distribution->architectures.atoms[i];
		if (a == architecture_source)
			continue;
		r = checkadddeb(distribution,
				component, a, packagetype,
				tracking, deb, permitnewerold);
		RET_UPDATE(result, r);
	}
	return result;
}

retvalue binaries_calcfilekeys(component_t component, const struct deb_headers *deb, packagetype_t packagetype, struct strlist *filekeys) {
	retvalue r;
	char *basefilename;

	basefilename = calc_binary_basename(deb->name, deb->version,
			deb->architecture, packagetype);
	if (FAILEDTOALLOC(basefilename))
		return RET_ERROR_OOM;

	r = calcfilekeys(component, deb->source, basefilename, filekeys);
	free(basefilename);
	return r;
}
