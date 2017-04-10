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
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include "error.h"
#include "ignore.h"
#include "mprintf.h"
#include "filecntl.h"
#include "strlist.h"
#include "dirs.h"
#include "names.h"
#include "checksums.h"
#include "chunks.h"
#include "target.h"
#include "signature.h"
#include "binaries.h"
#include "sources.h"
#include "dpkgversions.h"
#include "uploaderslist.h"
#include "guesscomponent.h"
#include "log.h"
#include "override.h"
#include "tracking.h"
#include "incoming.h"
#include "files.h"
#include "configparser.h"
#include "byhandhook.h"
#include "changes.h"

enum permitflags {
	/* do not error out on unused files */
	pmf_unused_files = 0,
	/* do not error out if there already is a newer package */
	pmf_oldpackagenewer,
	/* do not error out if there there are unadvertised binary files */
	pmf_unlistedbinaries,
	pmf_COUNT /* must be last */
};
enum cleanupflags {
	/* delete everything referenced by a .changes file
	 * when it is not accepted */
	cuf_on_deny = 0,
	/* check owner when deleting on_deny */
	cuf_on_deny_check_owner,
	/* delete everything referenced by a .changes on errors
	 * after accepting that .changes file*/
	cuf_on_error,
	/* delete unused files after successfully
	 * processing the used ones */
	cuf_unused_files,
	/* same but restricted to .buildinfo files */
	cuf_unused_buildinfo_files,
	cuf_COUNT /* must be last */
};
enum optionsflags {
	/* only put _all.deb comes with those of some architecture,
	 * only put in those architectures */
	iof_limit_arch_all = 0,
	/* allow .changes file to specify multiple distributions */
	iof_multiple_distributions,
	iof_COUNT /* must be last */
};

struct incoming {
	/* by incoming_parse: */
	char *name;
	char *directory;
	char *morguedir;
	char *tempdir;
	char *logdir;
	struct strlist allow;
	struct distribution **allow_into;
	struct distribution *default_into;
	/* by incoming_prepare: */
	struct strlist files;
	bool *processed;
	bool *delete;
	bool permit[pmf_COUNT];
	bool cleanup[cuf_COUNT];
	bool options[iof_COUNT];
	/* only to ease parsing: */
	const char *filename; /* only valid while parsing! */
	size_t lineno;
};
#define BASENAME(i, ofs) (i)->files.values[ofs]
/* the changes file is always the first one listed */
#define changesfile(c) (c->files)

static void incoming_free(/*@only@*/ struct incoming *i) {
	if (i == NULL)
		return;
	free(i->name);
	free(i->morguedir);
	free(i->tempdir);
	free(i->logdir);
	free(i->directory);
	strlist_done(&i->allow);
	free(i->allow_into);
	strlist_done(&i->files);
	free(i->processed);
	free(i->delete);
	free(i);
}

static retvalue incoming_prepare(struct incoming *i) {
	DIR *dir;
	struct dirent *ent;
	retvalue r;
	int ret;

	/* TODO: decide whether to clean this directory first ... */
	r = dirs_make_recursive(i->tempdir);
	if (RET_WAS_ERROR(r))
		return r;
	dir = opendir(i->directory);
	if (dir == NULL) {
		int e = errno;
		fprintf(stderr, "Cannot scan '%s': %s\n",
				i->directory, strerror(e));
		return RET_ERRNO(e);
	}
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;
		/* this should be impossible to hit.
		 * but given utf-8 encoding filesystems and
		 * overlong slashes, better check than be sorry */
		if (strchr(ent->d_name, '/') != NULL)
			continue;
		r = strlist_add_dup(&i->files, ent->d_name) ;
		if (RET_WAS_ERROR(r)) {
			(void)closedir(dir);
			return r;
		}
	}
	ret = closedir(dir);
	if (ret != 0) {
		int e = errno;
		fprintf(stderr, "Error scanning '%s': %s\n",
				i->directory, strerror(e));
		return RET_ERRNO(e);
	}
	i->processed = nzNEW(i->files.count, bool);
	if (FAILEDTOALLOC(i->processed))
		return RET_ERROR_OOM;
	i->delete = nzNEW(i->files.count, bool);
	if (FAILEDTOALLOC(i->delete))
		return RET_ERROR_OOM;
	return RET_OK;
}

struct read_incoming_data {
	/*@temp@*/const char *name;
	/*@temp@*/struct distribution *distributions;
	struct incoming *i;
};

static retvalue translate(struct distribution *distributions, struct strlist *names, struct distribution ***r) {
	struct distribution **d;
	int j;

	d = nzNEW(names->count, struct distribution *);
	if (FAILEDTOALLOC(d))
		return RET_ERROR_OOM;
	for (j = 0 ; j < names->count ; j++) {
		d[j] = distribution_find(distributions, names->values[j]);
		if (d[j] == NULL) {
			free(d);
			return RET_ERROR;
		}
	}
	*r = d;
	return RET_OK;
}

CFstartparse(incoming) {
	CFstartparseVAR(incoming, result_p);
	struct incoming *i;

	i = zNEW(struct incoming);
	if (FAILEDTOALLOC(i))
		return RET_ERROR_OOM;
	*result_p = i;
	return RET_OK;
}

CFfinishparse(incoming) {
	CFfinishparseVARS(incoming, i, last, d);

	if (!complete || strcmp(i->name, d->name) != 0) {
		incoming_free(i);
		return RET_NOTHING;
	}
	if (d->i != NULL) {
		fprintf(stderr,
"Multiple definitions of '%s': first started at line %u of %s, second at line %u of %s!\n",
				d->name,
				(unsigned int)d->i->lineno, d->i->filename,
				config_firstline(iter), config_filename(iter));
		incoming_free(i);
		incoming_free(d->i);
		d->i = NULL;
		return RET_ERROR;
	}
	if (i->logdir != NULL && i->logdir[0] != '/') {
		char *n = calc_dirconcat(global.basedir, i->logdir);
		if (FAILEDTOALLOC(n)) {
			incoming_free(i);
			return RET_ERROR_OOM;
		}
		free(i->logdir);
		i->logdir = n;
	}
	if (i->morguedir != NULL && i->morguedir[0] != '/') {
		char *n = calc_dirconcat(global.basedir, i->morguedir);
		if (FAILEDTOALLOC(n)) {
			incoming_free(i);
			return RET_ERROR_OOM;
		}
		free(i->morguedir);
		i->morguedir = n;
	}
	if (i->tempdir[0] != '/') {
		char *n = calc_dirconcat(global.basedir, i->tempdir);
		if (FAILEDTOALLOC(n)) {
			incoming_free(i);
			return RET_ERROR_OOM;
		}
		free(i->tempdir);
		i->tempdir = n;
	}
	if (i->directory[0] != '/') {
		char *n = calc_dirconcat(global.basedir, i->directory);
		if (FAILEDTOALLOC(n)) {
			incoming_free(i);
			return RET_ERROR_OOM;
		}
		free(i->directory);
		i->directory = n;
	}
	if (i->default_into == NULL && i->allow.count == 0) {
		fprintf(stderr,
"There is neither an 'Allow' nor a 'Default' definition in rule '%s'\n"
"(starting at line %u, ending at line %u of %s)!\n"
"Aborting as nothing would be let in.\n",
				d->name,
				config_firstline(iter), config_line(iter),
				config_filename(iter));
			incoming_free(i);
			return RET_ERROR;
	}
	if (i->morguedir != NULL && !i->cleanup[cuf_on_deny]
			&& !i->cleanup[cuf_on_error]
			&& !i->cleanup[cuf_unused_buildinfo_files]
			&& !i->cleanup[cuf_unused_files]) {
		fprintf(stderr,
"Warning: There is a 'MorgueDir' but no 'Cleanup' to act on in rule '%s'\n"
"(starting at line %u, ending at line %u of %s)!\n",
				d->name,
				config_firstline(iter), config_line(iter),
				config_filename(iter));
	}

	d->i = i;
	i->filename = config_filename(iter);
	i->lineno = config_firstline(iter);
	/* only suppreses the last unused warning: */
	*last = i;
	return RET_OK;
}

CFSETPROC(incoming, default) {
	CFSETPROCVARS(incoming, i, d);
	char *default_into;
	retvalue r;

	r = config_getonlyword(iter, headername, NULL, &default_into);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r))
		return r;
	i->default_into = distribution_find(d->distributions, default_into);
	free(default_into);
	return (i->default_into == NULL)?RET_ERROR:RET_OK;
}

CFSETPROC(incoming, allow) {
	CFSETPROCVARS(incoming, i, d);
	struct strlist allow_into;
	retvalue r;

	r = config_getsplitwords(iter, headername, &i->allow, &allow_into);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r))
		return r;
	assert (i->allow.count == allow_into.count);
	r = translate(d->distributions, &allow_into, &i->allow_into);
	strlist_done(&allow_into);
	if (RET_WAS_ERROR(r))
		return r;
	return RET_OK;
}

CFSETPROC(incoming, permit) {
	CFSETPROCVARS(incoming, i, d);
	static const struct constant permitconstants[] = {
		{ "unused_files",	pmf_unused_files},
		{ "older_version",	pmf_oldpackagenewer},
		{ "unlisted_binaries",	pmf_unlistedbinaries},
		/* not yet implemented:
		   { "downgrade",		pmf_downgrade},
		 */
		{ NULL, -1}
	};

	if (IGNORABLE(unknownfield))
		return config_getflags(iter, headername, permitconstants,
				i->permit, true, "");
	else if (i->name == NULL)
		return config_getflags(iter, headername, permitconstants,
				i->permit, false,
"\n(try put Name: before Permit: to ignore if it is from the wrong rule");
	else if (strcmp(i->name, d->name) != 0)
		return config_getflags(iter, headername, permitconstants,
				i->permit, true,
" (but not within the rule we are intrested in.)");
	else
		return config_getflags(iter, headername, permitconstants,
				i->permit, false,
" (use --ignore=unknownfield to ignore this)\n");

}

CFSETPROC(incoming, cleanup) {
	CFSETPROCVARS(incoming, i, d);
	static const struct constant cleanupconstants[] = {
		{ "unused_files", cuf_unused_files},
		{ "unused_buildinfo_files", cuf_unused_buildinfo_files},
		{ "on_deny", cuf_on_deny},
		/* not yet implemented
		{ "on_deny_check_owner", cuf_on_deny_check_owner},
		 */
		{ "on_error", cuf_on_error},
		{ NULL, -1}
	};

	if (IGNORABLE(unknownfield))
		return config_getflags(iter, headername, cleanupconstants,
				i->cleanup, true, "");
	else if (i->name == NULL)
		return config_getflags(iter, headername, cleanupconstants,
				i->cleanup, false,
"\n(try put Name: before Cleanup: to ignore if it is from the wrong rule");
	else if (strcmp(i->name, d->name) != 0)
		return config_getflags(iter, headername, cleanupconstants,
				i->cleanup, true,
" (but not within the rule we are intrested in.)");
	else
		return config_getflags(iter, headername, cleanupconstants,
				i->cleanup, false,
" (use --ignore=unknownfield to ignore this)\n");
}

CFSETPROC(incoming, options) {
	CFSETPROCVARS(incoming, i, d);
	static const struct constant optionsconstants[] = {
		{ "limit_arch_all", iof_limit_arch_all},
		{ "multiple_distributions", iof_multiple_distributions},
		{ NULL, -1}
	};

	if (IGNORABLE(unknownfield))
		return config_getflags(iter, headername, optionsconstants,
				i->options, true, "");
	else if (i->name == NULL)
		return config_getflags(iter, headername, optionsconstants,
				i->options, false,
"\n(try put Name: before Options: to ignore if it is from the wrong rule");
	else if (strcmp(i->name, d->name) != 0)
		return config_getflags(iter, headername, optionsconstants,
				i->options, true,
" (but not within the rule we are intrested in.)");
	else
		return config_getflags(iter, headername, optionsconstants,
				i->options, false,
" (use --ignore=unknownfield to ignore this)\n");
}

CFvalueSETPROC(incoming, name)
CFdirSETPROC(incoming, logdir)
CFdirSETPROC(incoming, tempdir)
CFdirSETPROC(incoming, morguedir)
CFdirSETPROC(incoming, directory)
CFtruthSETPROC2(incoming, multiple, options[iof_multiple_distributions])

static const struct configfield incomingconfigfields[] = {
	CFr("Name", incoming, name),
	CFr("TempDir", incoming, tempdir),
	CFr("IncomingDir", incoming, directory),
	CF("MorgueDir", incoming, morguedir),
	CF("Default", incoming, default),
	CF("Allow", incoming, allow),
	CF("Multiple", incoming, multiple),
	CF("Options", incoming, options),
	CF("Cleanup", incoming, cleanup),
	CF("Permit", incoming, permit),
	CF("Logdir", incoming, logdir)
};

static retvalue incoming_init(struct distribution *distributions, const char *name, /*@out@*/struct incoming **result) {
	retvalue r;
	struct read_incoming_data imports;

	imports.name = name;
	imports.distributions = distributions;
	imports.i = NULL;

	r = configfile_parse("incoming", IGNORABLE(unknownfield),
			startparseincoming, finishparseincoming,
			"incoming rule",
			incomingconfigfields, ARRAYCOUNT(incomingconfigfields),
			&imports);
	if (RET_WAS_ERROR(r))
		return r;
	if (imports.i == NULL) {
		fprintf(stderr,
"No definition for '%s' found in '%s/incoming'!\n",
				name, global.confdir);
		return RET_ERROR_MISSING;
	}

	r = incoming_prepare(imports.i);
	if (RET_WAS_ERROR(r)) {
		incoming_free(imports.i);
		return r;
	}
	*result = imports.i;
	return r;
}

struct candidate {
	/* from candidate_read */
	int ofs;
	char *control;
	struct signatures *signatures;
	/* from candidate_parse */
	char *source, *sourceversion, *changesversion;
	struct strlist distributions,
		       architectures,
		       binaries;
	bool isbinNMU;
	struct candidate_file {
		/* set by _addfileline */
		struct candidate_file *next;
		int ofs; /* to basename in struct incoming->files */
		filetype type;
		/* all NULL if it is the .changes itself,
		 * otherwise the data from the .changes for this file: */
		char *section;
		char *priority;
		architecture_t architecture;
		char *name;
		/* like above, but updated once files are copied */
		struct checksums *checksums;
		/* set later */
		bool used;
		char *tempfilename;
		/* distribution-unspecific contents of the packages */
		/* - only for FE_BINARY types: */
		struct deb_headers deb;
		/* - only for fe_DSC types */
		struct dsc_headers dsc;
		/* only valid while parsing */
		struct hashes h;
	} *files;
	struct candidate_perdistribution {
		struct candidate_perdistribution *next;
		struct distribution *into;
		bool skip;
		struct candidate_package {
			/* a package is something installing files, including
			 * the pseudo-package for the .changes file, if that is
			 * to be included */
			struct candidate_package *next;
			const struct candidate_file *master;
			component_t component;
			packagetype_t packagetype;
			struct strlist filekeys;
			/* a list of pointers to the files belonging to those
			 * filekeys, NULL if it does not need linking/copying */
			const struct candidate_file **files;
			/* only for FE_PACKAGE: */
			char *control;
			/* only for fe_DSC */
			char *directory;
			/* true if skipped because already there or newer */
			bool skip;
		} *packages;
		struct byhandfile {
			struct byhandfile *next;
			const struct candidate_file *file;
			const struct byhandhook *hook;
		} *byhandhookstocall;
	} *perdistribution;
	/* the logsubdir, and the list of files to put there,
	 * otherwise both NULL */
	char *logsubdir;
	int logcount;
	const struct candidate_file **logfiles;
};

static void candidate_file_free(/*@only@*/struct candidate_file *f) {
	checksums_free(f->checksums);
	free(f->section);
	free(f->priority);
	free(f->name);
	if (FE_BINARY(f->type))
		binaries_debdone(&f->deb);
	if (f->type == fe_DSC)
		sources_done(&f->dsc);
	if (f->tempfilename != NULL) {
		(void)unlink(f->tempfilename);
		free(f->tempfilename);
		f->tempfilename = NULL;
	}
	free(f);
}

static void candidate_package_free(/*@only@*/struct candidate_package *p) {
	free(p->control);
	free(p->directory);
	strlist_done(&p->filekeys);
	free(p->files);
	free(p);
}

static void candidate_free(/*@only@*/struct candidate *c) {
	if (c == NULL)
		return;
	free(c->control);
	signatures_free(c->signatures);
	free(c->source);
	free(c->sourceversion);
	free(c->changesversion);
	strlist_done(&c->distributions);
	strlist_done(&c->architectures);
	strlist_done(&c->binaries);
	while (c->perdistribution != NULL) {
		struct candidate_perdistribution *d = c->perdistribution;
		c->perdistribution = d->next;

		while (d->packages != NULL) {
			struct candidate_package *p = d->packages;
			d->packages = p->next;
			candidate_package_free(p);
		}
		while (d->byhandhookstocall != NULL) {
			struct byhandfile *h = d->byhandhookstocall;
			d->byhandhookstocall = h->next;
			free(h);
		}
		free(d);
	}
	while (c->files != NULL) {
		struct candidate_file *f = c->files;
		c->files = f->next;
		candidate_file_free(f);
	}
	free(c->logsubdir);
	free(c->logfiles);
	free(c);
}

static retvalue candidate_newdistribution(struct candidate *c, struct distribution *distribution) {
	struct candidate_perdistribution *n, **pp = &c->perdistribution;

	while (*pp != NULL) {
		if ((*pp)->into == distribution)
			return RET_NOTHING;
		pp = &(*pp)->next;
	}
	n = zNEW(struct candidate_perdistribution);
	if (FAILEDTOALLOC(n))
		return RET_ERROR_OOM;
	n->into = distribution;
	*pp = n;
	return RET_OK;
}

static struct candidate_package *candidate_newpackage(struct candidate_perdistribution *fordistribution, const struct candidate_file *master) {
	struct candidate_package *n, **pp = &fordistribution->packages;

	while (*pp != NULL)
		pp = &(*pp)->next;
	n = zNEW(struct candidate_package);
	if (FAILEDTOALLOC(n))
		return NULL;
	n->component = atom_unknown;
	n->packagetype = atom_unknown;
	n->master = master;
	*pp = n;
	return n;
}

static retvalue candidate_usefile(const struct incoming *i, const struct candidate *c, struct candidate_file *file);

static retvalue candidate_read(struct incoming *i, int ofs, struct candidate **result, bool *broken) {
	struct candidate *n;
	retvalue r;

	n = zNEW(struct candidate);
	if (FAILEDTOALLOC(n))
		return RET_ERROR_OOM;
	n->ofs = ofs;
	/* first file of any .changes file is the file itself */
	n->files = zNEW(struct candidate_file);
	if (FAILEDTOALLOC(n->files)) {
		free(n);
		return RET_ERROR_OOM;
	}
	n->files->ofs = n->ofs;
	n->files->type = fe_CHANGES;
	r = candidate_usefile(i, n, n->files);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r)) {
		candidate_free(n);
		return r;
	}
	assert (n->files->tempfilename != NULL);
	r = signature_readsignedchunk(n->files->tempfilename, BASENAME(i, ofs),
			&n->control, &n->signatures, broken);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r)) {
		candidate_free(n);
		return r;
	}
	*result = n;
	return RET_OK;
}

static retvalue candidate_addfileline(struct incoming *i, struct candidate *c, const char *fileline) {
	struct candidate_file **p, *n;
	char *basefilename;
	retvalue r;

	n = zNEW(struct candidate_file);
	if (FAILEDTOALLOC(n))
		return RET_ERROR_OOM;

	r = changes_parsefileline(fileline, &n->type, &basefilename,
			&n->h.hashes[cs_md5sum], &n->h.hashes[cs_length],
			&n->section, &n->priority, &n->architecture,
			&n->name);
	if (RET_WAS_ERROR(r)) {
		free(n);
		return r;
	}
	n->ofs = strlist_ofs(&i->files, basefilename);
	if (n->ofs < 0) {
		fprintf(stderr,
"In '%s': file '%s' not found in the incoming dir!\n",
				i->files.values[c->ofs], basefilename);
		free(basefilename);
		candidate_file_free(n);
		return RET_ERROR_MISSING;
	}
	free(basefilename);

	p = &c->files;
	while (*p != NULL)
		p = &(*p)->next;
	*p = n;
	return RET_OK;
}

static retvalue candidate_addhashes(struct incoming *i, struct candidate *c, enum checksumtype cs, const struct strlist *lines) {
	int j;

	for (j = 0 ; j < lines->count ; j++) {
		const char *fileline = lines->values[j];
		struct candidate_file *f;
		const char *basefilename;
		struct hash_data hash, size;
		retvalue r;

		r = hashline_parse(BASENAME(i, c->ofs), fileline, cs,
				&basefilename, &hash, &size);
		if (!RET_IS_OK(r))
			return r;
		f = c->files;
		while (f != NULL && strcmp(BASENAME(i, f->ofs), basefilename) != 0)
			f = f->next;
		if (f == NULL) {
			fprintf(stderr,
"Warning: Ignoring file '%s' listed in '%s' but not in '%s' of '%s'!\n",
					basefilename, changes_checksum_names[cs],
					changes_checksum_names[cs_md5sum],
					BASENAME(i, c->ofs));
			continue;
		}
		if (f->h.hashes[cs_length].len != size.len ||
				memcmp(f->h.hashes[cs_length].start,
					size.start, size.len) != 0) {
			fprintf(stderr,
"Error: Different size of '%s' listed in '%s' and '%s' of '%s'!\n",
					basefilename, changes_checksum_names[cs],
					changes_checksum_names[cs_md5sum],
					BASENAME(i, c->ofs));
			return RET_ERROR;
		}
		f->h.hashes[cs] = hash;
	}
	return RET_OK;
}

static retvalue candidate_finalizechecksums(struct candidate *c) {
	struct candidate_file *f;
	retvalue r;

	/* store collected hashes as checksums structs,
	 * starting after .changes file: */
	for (f = c->files->next ; f != NULL ; f = f->next) {
		r = checksums_initialize(&f->checksums, f->h.hashes);
		if (RET_WAS_ERROR(r))
			return r;
	}
	return RET_OK;
}

static retvalue candidate_parse(struct incoming *i, struct candidate *c) {
	retvalue r;
	struct strlist filelines[cs_hashCOUNT];
	enum checksumtype cs;
	int j;
#define R if (RET_WAS_ERROR(r)) return r;
#define E(err, ...) { \
		if (r == RET_NOTHING) { \
			fprintf(stderr, "In '%s': " err "\n", \
					BASENAME(i, c->ofs), ## __VA_ARGS__); \
			r = RET_ERROR; \
		} \
		if (RET_WAS_ERROR(r)) return r; \
	}
	r = chunk_getnameandversion(c->control, "Source", &c->source,
			&c->sourceversion);
	E("Missing 'Source' field!");
	r = propersourcename(c->source);
	E("Malforce Source name!");
	if (c->sourceversion != NULL) {
		r = properversion(c->sourceversion);
		E("Malforce Source Version number!");
	}
	r = chunk_getwordlist(c->control, "Binary", &c->binaries);
	E("Missing 'Binary' field!");
	r = chunk_getwordlist(c->control, "Architecture", &c->architectures);
	E("Missing 'Architecture' field!");
	r = chunk_getvalue(c->control, "Version", &c->changesversion);
	E("Missing 'Version' field!");
	r = properversion(c->changesversion);
	E("Malforce Version number!");
	// TODO: logic to detect binNMUs to warn against sources?
	if (c->sourceversion == NULL) {
		c->sourceversion = strdup(c->changesversion);
		if (FAILEDTOALLOC(c->sourceversion))
			return RET_ERROR_OOM;
		c->isbinNMU = false;
	} else {
		int cmp;

		r = dpkgversions_cmp(c->sourceversion, c->changesversion, &cmp);
		R;
		c->isbinNMU = cmp != 0;
	}
	r = chunk_getwordlist(c->control, "Distribution", &c->distributions);
	E("Missing 'Distribution' field!");
	r = chunk_getextralinelist(c->control,
			changes_checksum_names[cs_md5sum],
			&filelines[cs_md5sum]);
	E("Missing '%s' field!", changes_checksum_names[cs_md5sum]);
	for (j = 0 ; j < filelines[cs_md5sum].count ; j++) {
		r = candidate_addfileline(i, c, filelines[cs_md5sum].values[j]);
		if (RET_WAS_ERROR(r)) {
			strlist_done(&filelines[cs_md5sum]);
			return r;
		}
	}
	for (cs = cs_firstEXTENDED ; cs < cs_hashCOUNT ; cs++) {
		r = chunk_getextralinelist(c->control,
				changes_checksum_names[cs], &filelines[cs]);

		if (RET_IS_OK(r))
			r = candidate_addhashes(i, c, cs, &filelines[cs]);
		else
			strlist_init(&filelines[cs]);

		if (RET_WAS_ERROR(r)) {
			while (cs-- > cs_md5sum)
				strlist_done(&filelines[cs]);
			return r;
		}
	}
	r = candidate_finalizechecksums(c);
	for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++)
		strlist_done(&filelines[cs]);
	R;
	if (c->files == NULL || c->files->next == NULL) {
		fprintf(stderr, "In '%s': Empty 'Files' section!\n",
				BASENAME(i, c->ofs));
		return RET_ERROR;
	}
	return RET_OK;
}

static retvalue candidate_earlychecks(struct incoming *i, struct candidate *c) {
	struct candidate_file *file;
	retvalue r;

	// TODO: allow being more permissive,
	// that will need some more checks later, though
	r = propersourcename(c->source);
	if (RET_WAS_ERROR(r))
		return r;
	r = properversion(c->sourceversion);
	if (RET_WAS_ERROR(r))
		return r;
	for (file = c->files ; file != NULL ; file = file->next) {
		if (file->type != fe_CHANGES && file->type != fe_BYHAND &&
				file->type != fe_LOG &&
				!atom_defined(file->architecture)) {
			fprintf(stderr,
"'%s' contains '%s' not matching an valid architecture in any distribution known!\n",
					BASENAME(i, c->ofs),
					BASENAME(i, file->ofs));
			return RET_ERROR;
		}
		if (!FE_PACKAGE(file->type))
			continue;
		assert (atom_defined(file->architecture));
		if (strlist_in(&c->architectures,
					atoms_architectures[file->architecture]))
			continue;
		fprintf(stderr,
"'%s' is not listed in the Architecture header of '%s' but file '%s' looks like it!\n",
				atoms_architectures[file->architecture],
				BASENAME(i, c->ofs), BASENAME(i, file->ofs));
		return RET_ERROR;
	}
	return RET_OK;
}

/* Is used before any other candidate fields are set */
static retvalue candidate_usefile(const struct incoming *i, const struct candidate *c, struct candidate_file *file) {
	const char *basefilename;
	char *origfile, *tempfilename;
	struct checksums *readchecksums;
	retvalue r;
	bool improves;
	const char *p;

	if (file->used && file->tempfilename != NULL)
		return RET_OK;
	assert(file->tempfilename == NULL);
	basefilename = BASENAME(i, file->ofs);
	for (p = basefilename; *p != '\0' ; p++) {
		if ((0x80 & *(const unsigned char *)p) != 0) {
			fprintf(stderr,
"Invalid filename '%s' listed in '%s': contains 8-bit characters\n",
					basefilename, BASENAME(i, c->ofs));
			return RET_ERROR;
		}
	}
	tempfilename = calc_dirconcat(i->tempdir, basefilename);
	if (FAILEDTOALLOC(tempfilename))
		return RET_ERROR_OOM;
	origfile = calc_dirconcat(i->directory, basefilename);
	if (FAILEDTOALLOC(origfile)) {
		free(tempfilename);
		return RET_ERROR_OOM;
	}
	r = checksums_copyfile(tempfilename, origfile, true, &readchecksums);
	free(origfile);
	if (RET_WAS_ERROR(r)) {
		free(tempfilename);
		return r;
	}
	if (file->checksums == NULL) {
		file->checksums = readchecksums;
		file->tempfilename = tempfilename;
		file->used = true;
		return RET_OK;
	}
	if (!checksums_check(file->checksums, readchecksums, &improves)) {
		fprintf(stderr,
"ERROR: File '%s' does not match expectations:\n",
				basefilename);
		checksums_printdifferences(stderr,
				file->checksums, readchecksums);
		checksums_free(readchecksums);
		deletefile(tempfilename);
		free(tempfilename);
		return RET_ERROR_WRONG_MD5;
	}
	if (improves) {
		r = checksums_combine(&file->checksums, readchecksums, NULL);
		if (RET_WAS_ERROR(r)) {
			checksums_free(readchecksums);
			deletefile(tempfilename);
			free(tempfilename);
			return r;
		}
	}
	checksums_free(readchecksums);
	file->tempfilename = tempfilename;
	file->used = true;
	return RET_OK;
}

static inline retvalue getsectionprioritycomponent(const struct incoming *i, const struct candidate *c, const struct distribution *into, const struct candidate_file *file, const char *name, const struct overridedata *oinfo, /*@out@*/const char **section_p, /*@out@*/const char **priority_p, /*@out@*/component_t *component) {
	retvalue r;
	const char *section, *priority, *forcecomponent;
	component_t fc;

	section = override_get(oinfo, SECTION_FIELDNAME);
	if (section == NULL) {
		// TODO: warn about disparities here?
		section = file->section;
	}
	if (section == NULL || strcmp(section, "-") == 0) {
		fprintf(stderr, "No section found for '%s' ('%s' in '%s')!\n",
				name,
				BASENAME(i, file->ofs), BASENAME(i, c->ofs));
		return RET_ERROR;
	}
	priority = override_get(oinfo, PRIORITY_FIELDNAME);
	if (priority == NULL) {
		// TODO: warn about disparities here?
		priority = file->priority;
	}
	if (priority == NULL || strcmp(priority, "-") == 0) {
		fprintf(stderr, "No priority found for '%s' ('%s' in '%s')!\n",
				name,
				BASENAME(i, file->ofs), BASENAME(i, c->ofs));
		return RET_ERROR;
	}

	forcecomponent = override_get(oinfo, "$Component");
	if (forcecomponent != NULL) {
		fc = component_find(forcecomponent);
		if (!atom_defined(fc)) {
			fprintf(stderr,
"Unknown component '%s' (in $Component in override file for '%s'\n",
					forcecomponent, name);
			return RET_ERROR;
		}
		/* guess_component will check if that is valid for this
		 * distribution */
	} else
		fc = atom_unknown;
	r = guess_component(into->codename, &into->components,
			BASENAME(i, file->ofs), section,
			fc, component);
	if (RET_WAS_ERROR(r)) {
		return r;
	}
	*section_p = section;
	*priority_p = priority;
	return RET_OK;
}

static retvalue candidate_read_deb(struct incoming *i, struct candidate *c, struct candidate_file *file) {
	retvalue r;
	size_t l;
	char *base;
	const char *packagenametocheck;

	r = binaries_readdeb(&file->deb, file->tempfilename, true);
	if (RET_WAS_ERROR(r))
		return r;
	if (strcmp(file->name, file->deb.name) != 0) {
		// TODO: add permissive thing to ignore this
		fprintf(stderr,
"Name part of filename ('%s') and name within the file ('%s') do not match for '%s' in '%s'!\n",
				file->name, file->deb.name,
				BASENAME(i, file->ofs), BASENAME(i, c->ofs));
		return RET_ERROR;
	}
	if (file->architecture != file->deb.architecture) {
		// TODO: add permissive thing to ignore this in some cases
		// but do not forget to look into into->architectures then
		fprintf(stderr,
"Architecture '%s' of '%s' does not match '%s' specified in '%s'!\n",
				atoms_architectures[file->deb.architecture],
				BASENAME(i, file->ofs),
				atoms_architectures[file->architecture],
				BASENAME(i, c->ofs));
		return RET_ERROR;
	}
	if (strcmp(c->source, file->deb.source) != 0) {
		// TODO: add permissive thing to ignore this
		// (beware if tracking is active)
		fprintf(stderr,
"Source header '%s' of '%s' and source name '%s' within the file '%s' do not match!\n",
				c->source, BASENAME(i, c->ofs),
				file->deb.source, BASENAME(i, file->ofs));
		return RET_ERROR;
	}
	if (strcmp(c->sourceversion, file->deb.sourceversion) != 0) {
		// TODO: add permissive thing to ignore this
		// (beware if tracking is active)
		fprintf(stderr,
"Source version '%s' of '%s' and source version '%s' within the file '%s' do not match!\n",
				c->sourceversion, BASENAME(i, c->ofs),
				file->deb.sourceversion, BASENAME(i, file->ofs));
		return RET_ERROR;
	}

	packagenametocheck = file->deb.name;
	l = strlen(file->deb.name);
	if (l > sizeof("-dbgsym")-1 &&
	    strcmp(file->deb.name + l - (sizeof("dbgsym")), "-dbgsym") == 0) {
		base = strndup(file->deb.name, l - (sizeof("dbgsym")));
		if (FAILEDTOALLOC(base))
			return RET_ERROR_OOM;
		packagenametocheck = base;
	} else {
		base = NULL;
	}

	if (! strlist_in(&c->binaries, packagenametocheck)
	    && !i->permit[pmf_unlistedbinaries]) {
		fprintf(stderr,
"Name '%s' of binary '%s' is not listed in Binaries header of '%s'!\n"
"(use Permit: unlisted_binaries in conf/incoming to ignore this error)\n",
				packagenametocheck, BASENAME(i, file->ofs),
				BASENAME(i, c->ofs));
		free(base);
		return RET_ERROR;
	}
	free(base);
	r = properpackagename(file->deb.name);
	if (RET_IS_OK(r))
		r = propersourcename(file->deb.source);
	if (RET_IS_OK(r))
		r = properversion(file->deb.version);
	if (RET_WAS_ERROR(r))
		return r;
	return RET_OK;
}

static retvalue candidate_read_dsc(struct incoming *i, struct candidate_file *file) {
	retvalue r;
	bool broken = false;
	char *p;

	r = sources_readdsc(&file->dsc, file->tempfilename,
			BASENAME(i, file->ofs), &broken);
	if (RET_WAS_ERROR(r))
		return r;
	p = calc_source_basename(file->dsc.name,
			file->dsc.version);
	if (FAILEDTOALLOC(p))
		return RET_ERROR_OOM;
	r = checksumsarray_include(&file->dsc.files, p, file->checksums);
	if (RET_WAS_ERROR(r)) {
		return r;
	}
	// TODO: take a look at "broken"...
	return RET_OK;
}

static retvalue candidate_read_files(struct incoming *i, struct candidate *c) {
	struct candidate_file *file;
	retvalue r;

	for (file = c->files ; file != NULL ; file = file->next) {

		if (!FE_PACKAGE(file->type))
			continue;
		r = candidate_usefile(i, c, file);
		if (RET_WAS_ERROR(r))
			return r;
		assert(file->tempfilename != NULL);

		if (FE_BINARY(file->type))
			r = candidate_read_deb(i, c, file);
		else if (file->type == fe_DSC)
			r = candidate_read_dsc(i, file);
		else {
			r = RET_ERROR;
			assert (FE_BINARY(file->type) || file->type == fe_DSC);
		}
		if (RET_WAS_ERROR(r))
			return r;
	}
	return RET_OK;
}

static retvalue candidate_preparebuildinfos(const struct incoming *i, const struct candidate *c, struct candidate_perdistribution *per) {
	retvalue r;
	struct candidate_package *package;
	struct candidate_file *firstbuildinfo = NULL, *file;
	component_t component = component_strange;
	int count = 0;

	for (file = c->files ; file != NULL ; file = file->next) {
		if (file->type == fe_BUILDINFO) {
			count++;
			if (firstbuildinfo == NULL)
				firstbuildinfo = file;
		}
	}
	if (count == 0)
		return RET_NOTHING;

	/* search for a component to use */
	for (package = per->packages ; package != NULL ;
	                               package = package->next) {
		if (atom_defined(package->component)) {
			component = package->component;
			break;
		}
	}
	if (!atom_defined(component)) {
		/* How can this happen? */
		fprintf(stderr,
"Found no component to put %s into. (Why is there a buildinfo processed without an corresponding package?)\n", firstbuildinfo->name);
		return RET_ERROR;
	}

	/* pseudo package containing buildinfo files */
	package = candidate_newpackage(per, firstbuildinfo);
	if (FAILEDTOALLOC(package))
		return RET_ERROR_OOM;
	r = strlist_init_n(count, &package->filekeys);
	if (RET_WAS_ERROR(r))
		return r;
	package->files = nzNEW(count, const struct candidate_file *);
	if (FAILEDTOALLOC(package->files))
		return RET_ERROR_OOM;

	for (file = c->files ; file != NULL ; file = file->next) {
		char *filekey;

		if (file->type != fe_BUILDINFO)
			continue;

		r = candidate_usefile(i, c, file);
		if (RET_WAS_ERROR(r))
			return r;

		// TODO: add same checks on the basename contents?

		filekey = calc_filekey(component, c->source, BASENAME(i, file->ofs));
		if (FAILEDTOALLOC(filekey))
			return RET_ERROR_OOM;

		r = files_canadd(filekey, file->checksums);
		if (RET_WAS_ERROR(r))
			return r;
		if (RET_IS_OK(r))
			package->files[package->filekeys.count] = file;
		r = strlist_add(&package->filekeys, filekey);
		assert (r == RET_OK);
	}
	assert (package->filekeys.count == count);
	return RET_OK;
}



static retvalue candidate_preparechangesfile(const struct candidate *c, struct candidate_perdistribution *per) {
	retvalue r;
	char *basefilename, *filekey;
	struct candidate_package *package;
	struct candidate_file *file;
	component_t component = component_strange;
	assert (c->files != NULL && c->files->ofs == c->ofs);

	/* search for a component to use */
	for (package = per->packages ; package != NULL ;
	                               package = package->next) {
		if (atom_defined(package->component)) {
			component = package->component;
			break;
		}
	}
	file = changesfile(c);

	/* make sure the file is already copied */
	assert (file->used);
	assert (file->checksums != NULL);

	/* pseudo package containing the .changes file */
	package = candidate_newpackage(per, c->files);
	if (FAILEDTOALLOC(package))
		return RET_ERROR_OOM;

	basefilename = calc_changes_basename(c->source, c->changesversion,
			&c->architectures);
	if (FAILEDTOALLOC(basefilename))
		return RET_ERROR_OOM;

	filekey = calc_filekey(component, c->source, basefilename);
	free(basefilename);
	if (FAILEDTOALLOC(filekey))
		return RET_ERROR_OOM;

	r = strlist_init_singleton(filekey, &package->filekeys);
	if (RET_WAS_ERROR(r))
		return r;
	assert (package->filekeys.count == 1);
	filekey = package->filekeys.values[0];
	package->files = zNEW(const struct candidate_file *);
	if (FAILEDTOALLOC(package->files))
		return RET_ERROR_OOM;
	r = files_canadd(filekey, file->checksums);
	if (RET_WAS_ERROR(r))
		return r;
	if (RET_IS_OK(r))
		package->files[0] = file;
	return RET_OK;
}

static retvalue prepare_deb(const struct incoming *i, const struct candidate *c, struct candidate_perdistribution *per, const struct candidate_file *file) {
	const char *section, *priority;
	const char *filekey;
	const struct overridedata *oinfo;
	struct candidate_package *package;
	const struct distribution *into = per->into;
	retvalue r;

	assert (FE_BINARY(file->type));
	assert (file->tempfilename != NULL);
	assert (file->deb.name != NULL);

	package = candidate_newpackage(per, file);
	if (FAILEDTOALLOC(package))
		return RET_ERROR_OOM;
	assert (file == package->master);
	if (file->type == fe_DEB)
		package->packagetype = pt_deb;
	else
		package->packagetype = pt_udeb;

	oinfo = override_search(file->type==fe_UDEB?into->overrides.udeb:
			                    into->overrides.deb,
	                        file->name);

	r = getsectionprioritycomponent(i, c, into, file,
			file->name, oinfo,
			&section, &priority, &package->component);
	if (RET_WAS_ERROR(r))
		return r;

	if (file->type == fe_UDEB &&
	    !atomlist_in(&into->udebcomponents, package->component)) {
		fprintf(stderr,
"Cannot put file '%s' of '%s' into component '%s',\n"
"as it is not listed in UDebComponents of '%s'!\n",
			BASENAME(i, file->ofs), BASENAME(i, c->ofs),
			atoms_components[package->component],
			into->codename);
		return RET_ERROR;
	}
	r = binaries_calcfilekeys(package->component, &file->deb,
			package->packagetype, &package->filekeys);
	if (RET_WAS_ERROR(r))
		return r;
	assert (package->filekeys.count == 1);
	filekey = package->filekeys.values[0];
	package->files = zNEW(const struct candidate_file *);
	if (FAILEDTOALLOC(package->files))
		return RET_ERROR_OOM;
	r = files_canadd(filekey, file->checksums);
	if (RET_WAS_ERROR(r))
		return r;
	if (RET_IS_OK(r))
		package->files[0] = file;
	r = binaries_complete(&file->deb, filekey, file->checksums, oinfo,
			section, priority, &package->control);
	if (RET_WAS_ERROR(r))
		return r;
	return RET_OK;
}

static retvalue prepare_source_file(const struct incoming *i, const struct candidate *c, const char *filekey, const char *basefilename, struct checksums **checksums_p, int package_ofs, /*@out@*/const struct candidate_file **foundfile_p){
	struct candidate_file *f;
	const struct checksums * const checksums = *checksums_p;
	retvalue r;
	bool improves;

	f = c->files;
	while (f != NULL && (f->checksums == NULL ||
				strcmp(BASENAME(i, f->ofs), basefilename) != 0))
		f = f->next;

	if (f == NULL) {
		r = files_canadd(filekey, checksums);
		if (!RET_IS_OK(r))
			return r;
		/* no file by this name and also no file with these
		 * characteristics in the pool, look for differently-named
		 * file with the same characteristics: */

		f = c->files;
		while (f != NULL && (f->checksums == NULL ||
					!checksums_check(f->checksums,
						checksums, NULL)))
			f = f->next;

		if (f == NULL) {
			fprintf(stderr,
"file '%s' is needed for '%s', not yet registered in the pool and not found in '%s'\n",
					basefilename, BASENAME(i, package_ofs),
					BASENAME(i, c->ofs));
			return RET_ERROR;
		}
		/* otherwise proceed with the found file: */
	}

	if (!checksums_check(f->checksums, checksums, &improves)) {
		fprintf(stderr,
"file '%s' has conflicting checksums listed in '%s' and '%s'!\n",
				basefilename,
				BASENAME(i, c->ofs),
				BASENAME(i, package_ofs));
		return RET_ERROR;
	}
	if (improves) {
		/* put additional checksums from the .dsc to the information
		 * found in .changes, so that a file matching those in .changes
		 * but not in .dsc is detected */
		r = checksums_combine(&f->checksums, checksums, NULL);
		assert (r != RET_NOTHING);
		if (RET_WAS_ERROR(r))
			return r;
	}
	r = files_canadd(filekey, f->checksums);
	if (r == RET_NOTHING) {
		/* already in the pool, mark as used (in the sense
		 * of "only not needed because it is already there") */
		f->used = true;

	} else if (RET_IS_OK(r)) {
		/* don't have this file in the pool, make sure it is ready
		 * here */

		r = candidate_usefile(i, c, f);
		if (RET_WAS_ERROR(r))
			return r;
		// TODO: update checksums to now received checksums?
		*foundfile_p = f;
	}
	if (!RET_WAS_ERROR(r) && !checksums_iscomplete(checksums)) {
		/* update checksums so the source index can show them */
		r = checksums_combine(checksums_p, f->checksums, NULL);
	}
	return r;
}

static retvalue prepare_dsc(const struct incoming *i, const struct candidate *c, struct candidate_perdistribution *per, const struct candidate_file *file) {
	const char *section, *priority;
	const struct overridedata *oinfo;
	struct candidate_package *package;
	const struct distribution *into = per->into;
	retvalue r;
	int j;

	assert (file->type == fe_DSC);
	assert (file->tempfilename != NULL);
	assert (file->dsc.name != NULL);

	package = candidate_newpackage(per, file);
	if (FAILEDTOALLOC(package))
		return RET_ERROR_OOM;
	assert (file == package->master);
	package->packagetype = pt_dsc;

	if (c->isbinNMU) {
		// TODO: add permissive thing to ignore this
		fprintf(stderr,
"Source package ('%s') in '%s', which look like a binNMU (as '%s' and '%s' differ)!\n",
				BASENAME(i, file->ofs), BASENAME(i, c->ofs),
				c->sourceversion, c->changesversion);
		return RET_ERROR;
	}

	if (strcmp(file->name, file->dsc.name) != 0) {
		// TODO: add permissive thing to ignore this
		fprintf(stderr,
"Name part of filename ('%s') and name within the file ('%s') do not match for '%s' in '%s'!\n",
				file->name, file->dsc.name,
				BASENAME(i, file->ofs), BASENAME(i, c->ofs));
		return RET_ERROR;
	}
	if (strcmp(c->source, file->dsc.name) != 0) {
		// TODO: add permissive thing to ignore this
		// (beware if tracking is active)
		fprintf(stderr,
"Source header '%s' of '%s' and name '%s' within the file '%s' do not match!\n",
				c->source, BASENAME(i, c->ofs),
				file->dsc.name, BASENAME(i, file->ofs));
		return RET_ERROR;
	}
	if (strcmp(c->sourceversion, file->dsc.version) != 0) {
		// TODO: add permissive thing to ignore this
		// (beware if tracking is active)
		fprintf(stderr,
"Source version '%s' of '%s' and version '%s' within the file '%s' do not match!\n",
				c->sourceversion, BASENAME(i, c->ofs),
				file->dsc.version, BASENAME(i, file->ofs));
		return RET_ERROR;
	}
	r = propersourcename(file->dsc.name);
	if (RET_IS_OK(r))
		r = properversion(file->dsc.version);
	if (RET_IS_OK(r))
		r = properfilenames(&file->dsc.files.names);
	if (RET_WAS_ERROR(r))
		return r;

	/* check if signatures match files signed: */
	for (j = 0 ; j < file->dsc.files.names.count ; j++) {
		int jj;
		const char *afn = file->dsc.files.names.values[j];
		size_t al = strlen(afn);
		bool found = false;

		if (al <= 4 || memcmp(afn + al - 4, ".asc", 4) != 0)
			continue;

		for (jj = 0 ; jj < file->dsc.files.names.count ; jj++) {
			const char *fn = file->dsc.files.names.values[jj];
			size_t l = strlen(fn);

			if (l + 4 != al)
				continue;
			if (memcmp(afn, fn, l) != 0)
				continue;
			found = true;
			break;
		}
		if (!found) {
			fprintf(stderr,
"Signature file without file to be signed: '%s'!\n", afn);
			return RET_ERROR;
		}
	}

	oinfo = override_search(into->overrides.dsc, file->dsc.name);

	r = getsectionprioritycomponent(i, c, into, file,
			file->dsc.name, oinfo,
			&section, &priority, &package->component);
	if (RET_WAS_ERROR(r))
		return r;
	package->directory = calc_sourcedir(package->component,
			file->dsc.name);
	if (FAILEDTOALLOC(package->directory))
		return RET_ERROR_OOM;
	r = calc_dirconcats(package->directory, &file->dsc.files.names,
			&package->filekeys);
	if (RET_WAS_ERROR(r))
		return r;
	package->files = nzNEW(package->filekeys.count,
				const struct candidate_file *);
	if (FAILEDTOALLOC(package->files))
		return RET_ERROR_OOM;
	r = files_canadd(package->filekeys.values[0],
			file->checksums);
	if (RET_IS_OK(r))
		package->files[0] = file;
	if (RET_WAS_ERROR(r))
		return r;
	for (j = 1 ; j < package->filekeys.count ; j++) {
		r = prepare_source_file(i, c,
				package->filekeys.values[j],
				file->dsc.files.names.values[j],
				&file->dsc.files.checksums[j],
				file->ofs, &package->files[j]);
		if (RET_WAS_ERROR(r))
			return r;
	}
	r = sources_complete(&file->dsc, package->directory, oinfo,
			section, priority, &package->control);
	if (RET_WAS_ERROR(r))
		return r;

	return RET_OK;
}

static retvalue candidate_preparetrackbyhands(const struct incoming *i, const struct candidate *c, struct candidate_perdistribution *per) {
	retvalue r;
	char *byhanddir;
	struct candidate_package *package;
	struct candidate_file *firstbyhand = NULL, *file;
	component_t component = component_strange;
	int count = 0;

	for (file = c->files ; file != NULL ; file = file->next) {
		if (file->type == fe_BYHAND) {
			count++;
			if (firstbyhand == NULL)
				firstbyhand = file;
		}
	}
	if (count == 0)
		return RET_NOTHING;

	/* search for a component to use */
	for (package = per->packages ; package != NULL ;
	                               package = package->next) {
		if (atom_defined(package->component)) {
			component = package->component;
			break;
		}
	}

	/* pseudo package containing byhand files */
	package = candidate_newpackage(per, firstbyhand);
	if (FAILEDTOALLOC(package))
		return RET_ERROR_OOM;
	r = strlist_init_n(count, &package->filekeys);
	if (RET_WAS_ERROR(r))
		return r;
	package->files = nzNEW(count, const struct candidate_file *);
	if (FAILEDTOALLOC(package->files))
		return RET_ERROR_OOM;

	byhanddir = calc_byhanddir(component, c->source, c->changesversion);
	if (FAILEDTOALLOC(byhanddir))
		return RET_ERROR_OOM;

	for (file = c->files ; file != NULL ; file = file->next) {
		char *filekey;

		if (file->type != fe_BYHAND)
			continue;

		r = candidate_usefile(i, c, file);
		if (RET_WAS_ERROR(r)) {
			free(byhanddir);
			return r;
		}

		filekey = calc_dirconcat(byhanddir, BASENAME(i, file->ofs));
		if (FAILEDTOALLOC(filekey)) {
			free(byhanddir);
			return RET_ERROR_OOM;
		}

		r = files_canadd(filekey, file->checksums);
		if (RET_WAS_ERROR(r)) {
			free(byhanddir);
			return r;
		}
		if (RET_IS_OK(r))
			package->files[package->filekeys.count] = file;
		r = strlist_add(&package->filekeys, filekey);
		assert (r == RET_OK);
	}
	free(byhanddir);
	assert (package->filekeys.count == count);
	return RET_OK;
}

static retvalue candidate_preparelogs(const struct incoming *i, const struct candidate *c, struct candidate_perdistribution *per) {
	retvalue r;
	struct candidate_package *package;
	struct candidate_file *firstlog = NULL, *file;
	component_t component = component_strange;
	int count = 0;

	for (file = c->files ; file != NULL ; file = file->next) {
		if (file->type == fe_LOG) {
			count++;
			if (firstlog == NULL)
				firstlog = file;
		}
	}
	if (count == 0)
		return RET_NOTHING;

	/* search for a component to use */
	for (package = per->packages ; package != NULL ;
	                               package = package->next) {
		if (atom_defined(package->component)) {
			component = package->component;
			break;
		}
	}
	/* if there somehow were no packages to get an component from,
	   put in the main one of this distribution. */
	if (!atom_defined(component)) {
		assert (per->into->components.count > 0);
		component = per->into->components.atoms[0];
	}

	/* pseudo package containing log files */
	package = candidate_newpackage(per, firstlog);
	if (FAILEDTOALLOC(package))
		return RET_ERROR_OOM;
	r = strlist_init_n(count, &package->filekeys);
	if (RET_WAS_ERROR(r))
		return r;
	package->files = nzNEW(count, const struct candidate_file *);
	if (FAILEDTOALLOC(package->files))
		return RET_ERROR_OOM;

	for (file = c->files ; file != NULL ; file = file->next) {
		char *filekey;

		if (file->type != fe_LOG)
			continue;

		r = candidate_usefile(i, c, file);
		if (RET_WAS_ERROR(r))
			return r;

		// TODO: add same checks on the basename contents?

		filekey = calc_filekey(component, c->source, BASENAME(i, file->ofs));
		if (FAILEDTOALLOC(filekey))
			return RET_ERROR_OOM;

		r = files_canadd(filekey, file->checksums);
		if (RET_WAS_ERROR(r))
			return r;
		if (RET_IS_OK(r))
			package->files[package->filekeys.count] = file;
		r = strlist_add(&package->filekeys, filekey);
		assert (r == RET_OK);
	}
	assert (package->filekeys.count == count);
	return RET_OK;
}

static retvalue prepare_hookedbyhand(const struct incoming *i, const struct candidate *c, struct candidate_perdistribution *per, struct candidate_file *file) {
	const struct distribution *d = per->into;
	const struct byhandhook *h = NULL;
	struct byhandfile **b_p, *b;
	retvalue result = RET_NOTHING;
	retvalue r;

	b_p = &per->byhandhookstocall;
	while (*b_p != NULL)
		b_p = &(*b_p)->next;

	while (byhandhooks_matched(d->byhandhooks, &h,
				file->section, file->priority,
				BASENAME(i, file->ofs))) {
		r = candidate_usefile(i, c, file);
		if (RET_WAS_ERROR(r))
			return r;
		b = zNEW(struct byhandfile);
		if (FAILEDTOALLOC(b))
			return RET_ERROR_OOM;
		b->file = file;
		b->hook = h;
		*b_p = b;
		b_p = &b->next;
		result = RET_OK;
	}
	return result;
}

static retvalue prepare_for_distribution(const struct incoming *i, const struct candidate *c, struct candidate_perdistribution *d) {
	struct candidate_file *file;
	retvalue r;

	d->into->lookedat = true;

	for (file = c->files ; file != NULL ; file = file->next) {
		switch (file->type) {
			case fe_UDEB:
			case fe_DEB:
				r = prepare_deb(i, c, d, file);
				break;
			case fe_DSC:
				r = prepare_dsc(i, c, d, file);
				break;
			case fe_BYHAND:
				r = prepare_hookedbyhand(i, c, d, file);
				break;
			default:
				r = RET_NOTHING;
				break;
		}
		if (RET_WAS_ERROR(r)) {
			return r;
		}
	}
	if (d->into->tracking != dt_NONE) {
		if (d->into->trackingoptions.includebyhand) {
			r = candidate_preparetrackbyhands(i, c, d);
			if (RET_WAS_ERROR(r))
				return r;
		}
		if (d->into->trackingoptions.includelogs) {
			r = candidate_preparelogs(i, c, d);
			if (RET_WAS_ERROR(r))
				return r;
		}
		if (d->into->trackingoptions.includebuildinfos) {
			r = candidate_preparebuildinfos(i, c, d);
			if (RET_WAS_ERROR(r))
				return r;
		}
		if (d->into->trackingoptions.includechanges) {
			r = candidate_preparechangesfile(c, d);
			if (RET_WAS_ERROR(r))
				return r;
		}
	}
	//... check if something would be done ...
	return RET_OK;
}

static retvalue candidate_addfiles(struct candidate *c) {
	int j;
	struct candidate_perdistribution *d;
	struct candidate_package *p;
	retvalue r;

	for (d = c->perdistribution ; d != NULL ; d = d->next) {
		for (p = d->packages ; p != NULL ; p = p->next) {
			if (p->skip)
				continue;
			for (j = 0 ; j < p->filekeys.count ; j++) {
				const struct candidate_file *f = p->files[j];
				if (f == NULL)
					continue;
				assert(f->tempfilename != NULL);
				r = files_hardlinkandadd(f->tempfilename,
						p->filekeys.values[j],
						f->checksums);
				if (RET_WAS_ERROR(r))
					return r;
			}
		}
	}
	return RET_OK;
}

static retvalue add_dsc(struct distribution *into, struct trackingdata *trackingdata, struct candidate_package *p) {
	retvalue r;
	struct target *t = distribution_getpart(into,
			p->component, architecture_source, pt_dsc);

	assert (logger_isprepared(into->logger));

	/* finally put it into the source distribution */
	r = target_initpackagesdb(t, READWRITE);
	if (!RET_WAS_ERROR(r)) {
		retvalue r2;
		if (interrupted())
			r = RET_ERROR_INTERRUPTED;
		else
			r = target_addpackage(t, into->logger,
					p->master->dsc.name,
					p->master->dsc.version,
					p->control,
					&p->filekeys,
					false, trackingdata,
					architecture_source,
					NULL, NULL, NULL);
		r2 = target_closepackagesdb(t);
		RET_ENDUPDATE(r, r2);
	}
	RET_UPDATE(into->status, r);
	return r;
}

static retvalue checkadd_dsc(
		struct distribution *into,
		const struct incoming *i,
		bool tracking, struct candidate_package *p) {
	retvalue r;
	struct target *t = distribution_getpart(into,
			p->component, architecture_source, pt_dsc);

	/* check for possible errors putting it into the source distribution */
	r = target_initpackagesdb(t, READONLY);
	if (!RET_WAS_ERROR(r)) {
		retvalue r2;
		if (interrupted())
			r = RET_ERROR_INTERRUPTED;
		else
			r = target_checkaddpackage(t,
					p->master->dsc.name,
					p->master->dsc.version,
					tracking,
					i->permit[pmf_oldpackagenewer]);
		r2 = target_closepackagesdb(t);
		RET_ENDUPDATE(r, r2);
	}
	return r;
}

static retvalue candidate_add_into(const struct incoming *i, const struct candidate *c, const struct candidate_perdistribution *d, const char **changesfilekey_p) {
	retvalue r;
	struct candidate_package *p;
	struct trackingdata trackingdata;
	struct distribution *into = d->into;
	trackingdb tracks;
	struct atomlist binary_architectures;

	if (interrupted())
		return RET_ERROR_INTERRUPTED;

	into->lookedat = true;
	if (into->logger != NULL) {
		r = logger_prepare(d->into->logger);
		if (RET_WAS_ERROR(r))
			return r;
	}

	tracks = NULL;
	if (into->tracking != dt_NONE) {
		r = tracking_initialize(&tracks, into, false);
		if (RET_WAS_ERROR(r))
			return r;
	}
	if (tracks != NULL) {
		r = trackingdata_summon(tracks, c->source, c->sourceversion,
				&trackingdata);
		if (RET_WAS_ERROR(r)) {
			(void)tracking_done(tracks, into);
			return r;
		}
		if (into->trackingoptions.needsources) {
			// TODO, but better before we start adding...
		}
	}

	atomlist_init(&binary_architectures);
	for (p = d->packages ; p != NULL ; p = p->next) {
		if (FE_BINARY(p->master->type)) {
			architecture_t a = p->master->architecture;

			if (a != architecture_all)
				atomlist_add_uniq(&binary_architectures, a);
		}
	}

	r = RET_OK;
	for (p = d->packages ; p != NULL ; p = p->next) {
		if (p->skip) {
			if (verbose >= 0)
				printf(
"Not putting '%s' in '%s' as already in there with equal or newer version.\n",
					BASENAME(i, p->master->ofs),
					into->codename);
			continue;
		}
		if (p->master->type == fe_DSC) {
			r = add_dsc(into, (tracks==NULL)?NULL:&trackingdata,
					p);
		} else if (FE_BINARY(p->master->type)) {
			architecture_t a = p->master->architecture;
			const struct atomlist *as, architectures = {&a, 1, 1};

			if (i->options[iof_limit_arch_all] &&
					a == architecture_all &&
					binary_architectures.count > 0)
				as = &binary_architectures;
			else
				as = &architectures;
			r = binaries_adddeb(&p->master->deb,
					as, p->packagetype, into,
					(tracks==NULL)?NULL:&trackingdata,
					p->component, &p->filekeys,
					p->control);
		} else if (p->master->type == fe_CHANGES) {
			/* finally add the .changes to tracking, if requested */
			assert (p->master->name == NULL);
			assert (tracks != NULL);

			r = trackedpackage_adddupfilekeys(trackingdata.tracks,
					trackingdata.pkg,
					ft_CHANGES, &p->filekeys, false);
			if (p->filekeys.count > 0)
				*changesfilekey_p = p->filekeys.values[0];
		} else if (p->master->type == fe_BYHAND) {
			assert (tracks != NULL);

			r = trackedpackage_adddupfilekeys(trackingdata.tracks,
					trackingdata.pkg,
					ft_XTRA_DATA, &p->filekeys, false);
		} else if (p->master->type == fe_BUILDINFO) {
			assert (tracks != NULL);

			r = trackedpackage_adddupfilekeys(trackingdata.tracks,
					trackingdata.pkg,
					ft_BUILDINFO, &p->filekeys, false);
		} else if (p->master->type == fe_LOG) {
			assert (tracks != NULL);

			r = trackedpackage_adddupfilekeys(trackingdata.tracks,
					trackingdata.pkg,
					ft_LOG, &p->filekeys, false);
		} else
			r = RET_ERROR_INTERNAL;

		if (RET_WAS_ERROR(r))
			break;
	}
	atomlist_done(&binary_architectures);

	if (tracks != NULL) {
		retvalue r2;
		r2 = trackingdata_finish(tracks, &trackingdata);
		RET_UPDATE(r, r2);
		r2 = tracking_done(tracks, into);
		RET_ENDUPDATE(r, r2);
	}
	return r;
}

static inline retvalue candidate_checkadd_into(const struct incoming *i, const struct candidate_perdistribution *d) {
	retvalue r;
	struct candidate_package *p;
	struct distribution *into = d->into;
	bool somethingtodo = false;

	for (p = d->packages ; p != NULL ; p = p->next) {
		if (p->master->type == fe_DSC) {
			r = checkadd_dsc(into, i, into->tracking != dt_NONE,
					p);
		} else if (FE_BINARY(p->master->type)) {
			r = binaries_checkadddeb(&p->master->deb,
					p->master->architecture,
					p->packagetype,
					into, into->tracking != dt_NONE,
					p->component,
					i->permit[pmf_oldpackagenewer]);
		} else if (p->master->type == fe_CHANGES
				|| p->master->type == fe_BYHAND
				|| p->master->type == fe_BUILDINFO
				|| p->master->type == fe_LOG) {
			continue;
		} else
			r = RET_ERROR_INTERNAL;

		if (RET_WAS_ERROR(r))
			return r;
		if (r == RET_NOTHING)
			p->skip = true;
		else
			somethingtodo = true;
	}
	if (somethingtodo)
		return RET_OK;
	else
		return RET_NOTHING;
}

static inline bool isallowed(UNUSED(struct incoming *i), struct candidate *c, struct distribution *into, struct upload_conditions *conditions) {
	const struct candidate_file *file;

	do switch (uploaders_nextcondition(conditions)) {
		case uc_ACCEPTED:
			return true;
		case uc_REJECTED:
			return false;
		case uc_CODENAME:
			(void)uploaders_verifystring(conditions, into->codename);
			break;
		case uc_SOURCENAME:
			assert (c->source != NULL);
			(void)uploaders_verifystring(conditions, c->source);
			break;
		case uc_SECTIONS:
			for (file = c->files ; file != NULL ;
					file = file->next) {
				if (!FE_PACKAGE(file->type))
					continue;
				if (!uploaders_verifystring(conditions,
							(file->section == NULL)
							?"-":file->section))
					break;
			}
			break;
		case uc_BINARIES:
			for (file = c->files ; file != NULL ;
					file = file->next) {
				if (!FE_BINARY(file->type))
					continue;
				if (!uploaders_verifystring(conditions,
							file->name))
					break;
			}
			break;
		case uc_ARCHITECTURES:
			for (file = c->files ; file != NULL ;
					file = file->next) {
				if (!FE_PACKAGE(file->type))
					continue;
				if (!uploaders_verifyatom(conditions,
						file->architecture))
					break;
			}
			break;
		case uc_BYHAND:
			for (file = c->files ; file != NULL ;
					file = file->next) {
				if (file->type != fe_BYHAND)
					continue;
				if (!uploaders_verifystring(conditions,
						file->section))
					break;
			}
			break;
	} while (true);
}

static retvalue candidate_checkpermissions(struct incoming *i, struct candidate *c, struct distribution *into) {
	retvalue r;
	struct upload_conditions *conditions;
	bool allowed;

	/* no rules means allowed */
	if (into->uploaders == NULL)
		return RET_OK;

	r = distribution_loaduploaders(into);
	if (RET_WAS_ERROR(r))
		return r;
	assert(into->uploaderslist != NULL);

	r = uploaders_permissions(into->uploaderslist, c->signatures,
			&conditions);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r))
		return r;
	allowed = isallowed(i, c, into, conditions);
	free(conditions);
	if (allowed)
		return RET_OK;
	else
		/* reject */
		return RET_NOTHING;
}

static retvalue check_architecture_availability(const struct incoming *i, const struct candidate *c) {
	struct candidate_perdistribution *d;
	bool check_all_availability = false;
	bool have_all_available = false;
	int j;

	// TODO: switch to instead ensure every architecture can be put into
	// one distribution at least would be nice. If implementing this do not
	// forget to check later to only put files in when the distribution can
	// cope with that.

	for (j = 0 ; j < c->architectures.count ; j++) {
		const char *architecture = c->architectures.values[j];
		if (strcmp(architecture, "all") == 0) {
			check_all_availability = true;
			continue;
		}
		for (d = c->perdistribution ; d != NULL ; d = d->next) {
			if (atomlist_in(&d->into->architectures, architecture_find(architecture)))
				continue;
			fprintf(stderr,
"'%s' lists architecture '%s' not found in distribution '%s'!\n",
					BASENAME(i, c->ofs), architecture,
					d->into->codename);
			return RET_ERROR;
		}
		if (strcmp(architecture, "source") != 0)
			have_all_available = true;
	}
	if (check_all_availability && ! have_all_available) {
		for (d = c->perdistribution ; d != NULL ; d = d->next) {
			if (d->into->architectures.count > 1)
				continue;
			if (d->into->architectures.count > 0 &&
				d->into->architectures.atoms[0] != architecture_source)
				continue;
			fprintf(stderr,
"'%s' lists architecture 'all' but no binary architecture found in distribution '%s'!\n",
					BASENAME(i, c->ofs), d->into->codename);
			return RET_ERROR;
		}
	}
	return RET_OK;
}

static retvalue create_uniq_logsubdir(const char *logdir, const char *name, const char *version, const struct strlist *architectures, /*@out@*/char **subdir_p) {
	char *dir, *p;
	size_t l;
	retvalue r;

	r = dirs_make_recursive(logdir);
	if (RET_WAS_ERROR(r))
		return r;

	p = calc_changes_basename(name, version, architectures);
	if (FAILEDTOALLOC(p))
		return RET_ERROR_OOM;
	dir = calc_dirconcat(logdir, p);
	free(p);
	if (FAILEDTOALLOC(dir))
		return RET_ERROR_OOM;
	l = strlen(dir);
	assert (l > 8 && strcmp(dir + l - 8 , ".changes") == 0);
	memset(dir + l - 7, '0', 7);
	r = dirs_create(dir);
	while (r == RET_NOTHING) {
		p = dir + l - 1;
		while (*p == '9') {
			*p = '0';
			p--;
		}
		if (*p < '0' || *p > '8') {
			fprintf(stderr,
"Failed to create a new directory of the form '%s'\n"
"it looks like all 10000000 such directories are already there...\n",
				dir);
			return RET_ERROR;
		}
		(*p)++;
		r = dirs_create(dir);
	}
	*subdir_p = dir;
	return RET_OK;

}

static retvalue candidate_prepare_logdir(struct incoming *i, struct candidate *c) {
	int count, j;
	struct candidate_file *file;
	retvalue r;

	r = create_uniq_logsubdir(i->logdir,
			c->source, c->changesversion,
			&c->architectures,
			&c->logsubdir);
	assert (RET_IS_OK(r));
	if (RET_WAS_ERROR(r))
		return RET_ERROR_OOM;
	count = 0;
	for (file = c->files ; file != NULL ; file = file->next) {
		if (file->ofs == c->ofs || file->type == fe_LOG
				|| file->type == fe_BUILDINFO
				|| (file->type == fe_BYHAND && !file->used))
			count++;
	}
	c->logcount = count;
	c->logfiles = nzNEW(count, const struct candidate_file *);
	if (FAILEDTOALLOC(c->logfiles))
		return RET_ERROR_OOM;
	j = 0;
	for (file = c->files ; file != NULL ; file = file->next) {
		if (file->ofs == c->ofs || file->type == fe_LOG
				|| file->type == fe_BUILDINFO
				|| (file->type == fe_BYHAND && !file->used)) {
			r = candidate_usefile(i, c, file);
			if (RET_WAS_ERROR(r))
				return r;
			c->logfiles[j++] = file;
		}
	}
	assert (count == j);
	return RET_OK;
}

static retvalue candidate_finish_logdir(struct incoming *i, struct candidate *c) {
	int j;

	for (j = 0 ; j < c->logcount ; j++) {
		retvalue r;
		const struct candidate_file *f = c->logfiles[j];

		r = checksums_hardlink(c->logsubdir,
				BASENAME(i, f->ofs), f->tempfilename,
				f->checksums);
		if (RET_WAS_ERROR(r))
			return r;
	}
	return RET_OK;
}

static retvalue candidate_add_byhands(struct incoming *i, UNUSED(struct candidate *c), struct candidate_perdistribution *d) {
	struct byhandfile *b;
	retvalue r;

	for (b = d->byhandhookstocall ; b != NULL ; b = b->next){
		const struct candidate_file *f = b->file;

		r = byhandhook_call(b->hook, d->into->codename,
				f->section, f->priority, BASENAME(i, f->ofs),
				f->tempfilename);
		if (RET_WAS_ERROR(r))
			return r;
	}
	return RET_OK;
}

/* the actual adding of packages,
 * everything that can be tested earlier should be already tested now */
static retvalue candidate_really_add(struct incoming *i, struct candidate *c) {
	struct candidate_perdistribution *d;
	retvalue r;

	for (d = c->perdistribution ; d != NULL ; d = d->next) {
		if (d->byhandhookstocall == NULL)
			continue;
		r = candidate_add_byhands(i, c, d);
		if (RET_WAS_ERROR(r))
			return r;
	}

	/* make hardlinks/copies of the files */
	r = candidate_addfiles(c);
	if (RET_WAS_ERROR(r))
		return r;
	if (interrupted())
		return RET_ERROR_INTERRUPTED;

	if (i->logdir != NULL) {
		r = candidate_finish_logdir(i, c);
		if (RET_WAS_ERROR(r))
			return r;
	}
	if (interrupted())
		return RET_ERROR_INTERRUPTED;
	r = RET_OK;
	for (d = c->perdistribution ; d != NULL ; d = d->next) {
		struct distribution *into = d->into;
		const char *changesfilekey = NULL;

		/* if there are regular packages to add,
		 * add them and call the log.
		 * If all packages were skipped but a byhandhook run,
		 * still advertise the .changes file to loggers */
		if (!d->skip) {
			r = candidate_add_into(i, c, d,
					&changesfilekey);
			if (RET_WAS_ERROR(r))
				return r;
		} else if (d->byhandhookstocall == NULL)
			continue;
		logger_logchanges(into->logger, into->codename,
				c->source, c->changesversion, c->control,
				changesfile(c)->tempfilename, changesfilekey);
	}
	return RET_OK;
}

static retvalue candidate_add(struct incoming *i, struct candidate *c) {
	struct candidate_perdistribution *d;
	struct candidate_file *file;
	retvalue r;
	bool somethingtodo;
	char *origfilename;
	assert (c->perdistribution != NULL);

	/* check if every distribution this is to be added to supports
	 * all architectures we have files for */
	r = check_architecture_availability(i, c);
	if (RET_WAS_ERROR(r))
		return r;

	for (d = c->perdistribution ; d != NULL ; d = d->next) {
		r = distribution_loadalloverrides(d->into);
		if (RET_WAS_ERROR(r))
			return r;
	}

	// TODO: once uploaderlist allows one to look for package names or
	// existing override entries or such things, check package names here
	// enable checking for content name with outer name

	/* when we get here, the package is allowed in, now we have to
	 * read the parts and check all stuff we only know now */

	r = candidate_read_files(i, c);
	if (RET_WAS_ERROR(r))
		return r;

	/* now the distribution specific part starts: */
	for (d = c->perdistribution ; d != NULL ; d = d->next) {
		r = prepare_for_distribution(i, c, d);
			if (RET_WAS_ERROR(r))
				return r;
	}
	if (i->logdir != NULL) {
		r = candidate_prepare_logdir(i, c);
		if (RET_WAS_ERROR(r))
			return r;

	}
	for (file = c->files ; file != NULL ; file = file->next) {
		/* silently ignore unused buildinfo files: */
		if (file->type == fe_BUILDINFO)
			continue;
		/* otherwise complain unless unused_files is given */
		if (!file->used && !i->permit[pmf_unused_files]) {
			// TODO: find some way to mail such errors...
			fprintf(stderr,
"Error: '%s' contains unused file '%s'!\n"
"(Do Permit: unused_files to conf/incoming to ignore and\n"
" additionally Cleanup: unused_files to delete them)\n",
				BASENAME(i, c->ofs), BASENAME(i, file->ofs));
			if (file->type == fe_LOG || file->type == fe_BYHAND)
				fprintf(stderr,
"Alternatively, you can also add a LogDir: for '%s' into conf/incoming\n"
"then files like that will be stored there.\n",
					i->name);
			return RET_ERROR;
		}
	}

	/* additional test run to see if anything could go wrong,
	 * or if there are already newer versions */
	somethingtodo = false;
	for (d = c->perdistribution ; d != NULL ; d = d->next) {
		r = candidate_checkadd_into(i, d);
		if (RET_WAS_ERROR(r))
			return r;
		if (r == RET_NOTHING) {
			d->skip = true;
			if (d->byhandhookstocall != NULL)
				somethingtodo = true;
		} else
			somethingtodo = true;
	}
	if (! somethingtodo) {
		if (verbose >= 0) {
			printf(
"Skipping %s because all packages are skipped!\n",
					BASENAME(i, c->ofs));
		}
		for (file = c->files ; file != NULL ; file = file->next) {
			if (file->used || i->cleanup[cuf_unused_files] ||
					(file->type == fe_BUILDINFO &&
					 i->cleanup[cuf_unused_buildinfo_files]))
				i->delete[file->ofs] = true;
		}
		return RET_NOTHING;
	}

	// TODO: make sure not two different files are supposed to be installed
	// as the same filekey.

	/* the actual adding of packages, make sure what can be checked was
	 * checked by now */

	origfilename = calc_dirconcat(i->directory,
			BASENAME(i, changesfile(c)->ofs));
	causingfile = origfilename;

	r = candidate_really_add(i, c);

	causingfile = NULL;
	free(origfilename);

	if (RET_WAS_ERROR(r))
		return r;

	/* mark files as done */
	for (file = c->files ; file != NULL ; file = file->next) {
		if (file->used)
			i->processed[file->ofs] = true;
		if (file->used || i->cleanup[cuf_unused_files] ||
				(file->type == fe_BUILDINFO &&
				 i->cleanup[cuf_unused_buildinfo_files])) {
			i->delete[file->ofs] = true;
		}
	}
	return r;
}

static retvalue process_changes(struct incoming *i, int ofs) {
	struct candidate *c;
	retvalue r;
	int j, k;
	bool broken = false, tried = false;

	r = candidate_read(i, ofs, &c, &broken);
	if (RET_WAS_ERROR(r))
		return r;
	assert (RET_IS_OK(r));
	r = candidate_parse(i, c);
	if (RET_WAS_ERROR(r)) {
		candidate_free(c);
		return r;
	}
	r = candidate_earlychecks(i, c);
	if (RET_WAS_ERROR(r)) {
		if (i->cleanup[cuf_on_error]) {
			struct candidate_file *file;

			i->delete[c->ofs] = true;
			for (file = c->files ; file != NULL ;
			                       file = file->next) {
				i->delete[file->ofs] = true;
			}
		}
		candidate_free(c);
		return r;
	}
	for (k = 0 ; k < c->distributions.count ; k++) {
		const char *name = c->distributions.values[k];

		for (j = 0 ; j < i->allow.count ; j++) {
			// TODO: implement "*"
			if (strcmp(name, i->allow.values[j]) == 0) {
				tried = true;
				r = candidate_checkpermissions(i, c,
						i->allow_into[j]);
				if (r == RET_NOTHING)
					continue;
				if (RET_IS_OK(r))
					r = candidate_newdistribution(c,
							i->allow_into[j]);
				if (RET_WAS_ERROR(r)) {
					candidate_free(c);
					return r;
				} else
					break;
			}
		}
		if (c->perdistribution != NULL &&
				!i->options[iof_multiple_distributions])
			break;
	}
	if (c->perdistribution == NULL && i->default_into != NULL) {
		tried = true;
		r = candidate_checkpermissions(i, c, i->default_into);
		if (RET_WAS_ERROR(r)) {
			candidate_free(c);
			return r;
		}
		if (RET_IS_OK(r)) {
			r = candidate_newdistribution(c, i->default_into);
		}
	}
	if (c->perdistribution == NULL) {
		fprintf(stderr, tried?"No distribution accepting '%s' (i.e. none of the candidate distributions allowed inclusion)!\n":
				      "No distribution found for '%s'!\n",
			i->files.values[ofs]);
		if (i->cleanup[cuf_on_deny]) {
			struct candidate_file *file;

			i->delete[c->ofs] = true;
			for (file = c->files ; file != NULL ;
			                       file = file->next) {
				// TODO: implement same-owner check
				if (!i->cleanup[cuf_on_deny_check_owner])
					i->delete[file->ofs] = true;
			}
		}
		r = RET_ERROR_INCOMING_DENY;
	} else {
		if (broken) {
			fprintf(stderr,
"'%s' is signed with only invalid signatures.\n"
"If this was not corruption but willfull modification,\n"
"remove the signatures and try again.\n",
				i->files.values[ofs]);
			r = RET_ERROR;
		} else
			r = candidate_add(i, c);
		if (RET_WAS_ERROR(r) && i->cleanup[cuf_on_error]) {
			struct candidate_file *file;

			i->delete[c->ofs] = true;
			for (file = c->files ; file != NULL ;
			                       file = file->next) {
				i->delete[file->ofs] = true;
			}
		}
	}
	logger_wait();
	candidate_free(c);
	return r;
}

static inline /*@null@*/char *create_uniq_subdir(const char *basedir) {
	char date[16], *dir;
	unsigned long number = 0;
	retvalue r;
	time_t curtime;
	struct tm *tm;
	int e;

	r = dirs_make_recursive(basedir);
	if (RET_WAS_ERROR(r))
		return NULL;

	if (time(&curtime) == (time_t)-1)
		tm = NULL;
	else
		tm = gmtime(&curtime);
	if (tm == NULL || strftime(date, 16, "%Y-%m-%d", tm) != 10)
		strcpy(date, "timeerror");

	for (number = 0 ; number < 10000 ; number ++) {
		dir = mprintf("%s/%s-%lu", basedir, date, number);
		if (FAILEDTOALLOC(dir))
			return NULL;
		if (mkdir(dir, 0777) == 0)
			return dir;
		e = errno;
		if (e != EEXIST) {
			fprintf(stderr,
"Error %d creating directory '%s': %s\n",
					e, dir, strerror(e));
			free(dir);
			return NULL;
		}
		free(dir);
	}
	fprintf(stderr, "Could not create unique subdir in '%s'!\n", basedir);
	return NULL;
}

/* tempdir should ideally be on the same partition like the pooldir */
retvalue process_incoming(struct distribution *distributions, const char *name, const char *changesfilename) {
	struct incoming *i;
	retvalue result, r;
	int j;
	char *morguedir;

	result = RET_NOTHING;

	r = incoming_init(distributions, name, &i);
	if (RET_WAS_ERROR(r))
		return r;

	for (j = 0 ; j < i->files.count ; j ++) {
		const char *basefilename = i->files.values[j];
		size_t l = strlen(basefilename);
#define C_SUFFIX ".changes"
		const size_t c_len = strlen(C_SUFFIX);
		if (l <= c_len ||
		    memcmp(basefilename + (l - c_len), C_SUFFIX, c_len) != 0)
			continue;
		if (changesfilename != NULL && strcmp(basefilename, changesfilename) != 0)
			continue;
		/* a .changes file, check it */
		r = process_changes(i, j);
		RET_UPDATE(result, r);
	}

	logger_wait();
	if (i->morguedir == NULL)
		morguedir = NULL;
	else {
		morguedir = create_uniq_subdir(i->morguedir);
	}
	for (j = 0 ; j < i->files.count ; j ++) {
		char *fullfilename;

		if (!i->delete[j])
			continue;

		fullfilename = calc_dirconcat(i->directory, i->files.values[j]);
		if (FAILEDTOALLOC(fullfilename)) {
			result = RET_ERROR_OOM;
			continue;
		}
		if (morguedir != NULL && !i->processed[j]) {
			char *newname = calc_dirconcat(morguedir,
					i->files.values[j]);
			if (newname != NULL &&
					rename(fullfilename, newname) == 0) {
				free(newname);
				free(fullfilename);
				continue;
			} else if (FAILEDTOALLOC(newname)) {
				result = RET_ERROR_OOM;
			} else {
				int e = errno;

				fprintf(stderr,
"Error %d moving '%s' to '%s': %s\n",
						e, i->files.values[j],
						morguedir, strerror(e));
				RET_UPDATE(result, RET_ERRNO(e));
				/* no continue, instead
				 * delete the file as normal: */
			}
		}
		if (verbose >= 3)
			printf("deleting '%s'...\n", fullfilename);
		deletefile(fullfilename);
		free(fullfilename);
	}
	if (morguedir != NULL) {
		/* in the case it is empty, remove again */
		(void)rmdir(morguedir);
		free(morguedir);
	}
	incoming_free(i);
	return result;
}
