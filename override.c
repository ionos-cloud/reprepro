/*  This file is part of "reprepro"
 *  Copyright (C) 2004,2005,2007,2010 Bernhard R. Link
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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <search.h>
#include "error.h"
#include "chunks.h"
#include "sources.h"
#include "names.h"
#include "globmatch.h"
#include "override.h"
#include "configparser.h"

struct overridedata {
	struct strlist fields;
};

struct overridepackage {
	char *packagename;
	struct overridedata data;
};

struct overridepattern {
	struct overridepattern *next;
	char *pattern;
	struct overridedata data;
};

struct overridefile {
	/* a <search.h> tree root of struct overridepackage */
	void *packages;
	struct overridepattern *patterns;
};

#ifdef HAVE_TDESTROY
static void freeoverridepackage(void *n) {
	struct overridepackage *p = n;

	free(p->packagename);
	strlist_done(&p->data.fields);
	free(p);
}
#endif

void override_free(struct overridefile *info) {
	struct overridepattern *i;

	if (info == NULL)
		return;

#ifdef HAVE_TDESTROY
	tdestroy(info->packages, freeoverridepackage);
#endif
	while ((i = info->patterns) != NULL) {
		if (i == NULL)
			return;
		strlist_done(&i->data.fields);
		free(i->pattern);
		info->patterns = i->next;
		free(i);
	}
	free(info);
}

static bool forbidden_field_name(bool source, const char *field) {
	if (strcasecmp(field, "Package") == 0)
		return true;
	if (strcasecmp(field, "Version") == 0)
		return true;
	if (source) {
		if (strcasecmp(field, "Files") == 0)
			return true;
		if (strcasecmp(field, "Directory") == 0)
				return true;
		if (strcasecmp(field, "Checksums-Sha256") == 0)
				return true;
		if (strcasecmp(field, "Checksums-Sha1") == 0)
				return true;
		return false;
	} else {
		if (strcasecmp(field, "Filename") == 0)
			return true;
		if (strcasecmp(field, "MD5sum") == 0)
				return true;
		if (strcasecmp(field, "SHA1") == 0)
				return true;
		if (strcasecmp(field, "SHA256") == 0)
				return true;
		if (strcasecmp(field, "Size") == 0)
				return true;
		return false;
	}
}

static retvalue add_override_field(struct overridedata *data, const char *secondpart, const char *thirdpart, bool source) {
	retvalue r;
	char *p;

	if (forbidden_field_name(source, secondpart)) {
		fprintf(stderr,
"Error: field '%s' not allowed in override files.\n",
				secondpart);
		return RET_ERROR;
	}
	if (secondpart[0] == '$') {
		if (strcasecmp(secondpart, "$Delete") == 0) {
			if (forbidden_field_name(source, thirdpart)) {
				fprintf(stderr,
"Error: field '%s' not allowed in override files (not even as to be deleted).\n",
						thirdpart);
				return RET_ERROR;
			}
		} else if (strcasecmp(secondpart, "$Component") != 0) {
			fprintf(stderr,
"Warning: special override field '%s' unknown and will be ignored\n",
					secondpart);
		}
	}
	p = strdup(secondpart);
	if (FAILEDTOALLOC(p))
		return RET_ERROR_OOM;
	r = strlist_add(&data->fields, p);
	if (RET_WAS_ERROR(r))
		return r;
	p = strdup(thirdpart);
	if (FAILEDTOALLOC(p))
		return RET_ERROR_OOM;
	r = strlist_add(&data->fields, p);
	return r;
}

static struct overridepackage *new_package(const char *name) {
	struct overridepackage *p;

	p = zNEW(struct overridepackage);
	if (FAILEDTOALLOC(p))
		return NULL;
	p->packagename = strdup(name);
	if (FAILEDTOALLOC(p->packagename)) {
		free(p);
		return NULL;
	}
	return p;
}

static int opackage_compare(const void *a, const void *b) {
	const struct overridepackage *p1 = a, *p2 = b;

	return strcmp(p1->packagename, p2->packagename);
}

static retvalue add_override(struct overridefile *i, const char *firstpart, const char *secondpart, const char *thirdpart, bool source) {
	struct overridepackage *pkg, **node;
	retvalue r;
	const char *c;
	struct overridepattern *p, **l;

	c = firstpart;
	while (*c != '\0' && *c != '*' && *c != '[' && *c != '?')
		c++;
	if (*c != '\0') {
		/* This is a pattern, put into the pattern list */
		l = &i->patterns;
		while ((p = *l) != NULL
				&& strcmp(p->pattern, firstpart) != 0) {
			l = &p->next;
		}
		if (p == NULL) {
			p = zNEW(struct overridepattern);
			if (FAILEDTOALLOC(p))
				return RET_ERROR_OOM;
			p->pattern = strdup(firstpart);
			if (FAILEDTOALLOC(p->pattern)) {
				free(p);
				return RET_ERROR_OOM;
			}
		}
		r = add_override_field(&p->data,
				secondpart, thirdpart, source);
		if (RET_WAS_ERROR(r)) {
			if (*l != p) {
				free(p->pattern);
				free(p);
			}
			return r;
		}
		*l = p;
		return RET_OK;
	}

	pkg = new_package(firstpart);
	if (FAILEDTOALLOC(pkg))
		return RET_ERROR_OOM;
	node = tsearch(pkg, &i->packages, opackage_compare);
	if (FAILEDTOALLOC(node))
		return RET_ERROR_OOM;
	if (*node == pkg) {
		r = strlist_init_n(6, &pkg->data.fields);
		if (RET_WAS_ERROR(r))
			return r;
	} else {
		free(pkg->packagename);
		free(pkg);
		pkg = *node;
	}
	return add_override_field(&(*node)->data,
			secondpart, thirdpart, source);
}

retvalue override_read(const char *filename, struct overridefile **info, bool source) {
	struct overridefile *i;
	FILE *file;
	char buffer[1001];

	if (filename == NULL) {
		*info = NULL;
		return RET_OK;
	}
	char *fn = configfile_expandname(filename, NULL);
	if (FAILEDTOALLOC(fn))
		return RET_ERROR_OOM;
	file = fopen(fn, "r");
	free(fn);

	if (file == NULL) {
		int e = errno;
		fprintf(stderr, "Error %d opening override file '%s': %s\n",
				e, filename, strerror(e));
		return RET_ERRNO(e);
	}
	i = zNEW(struct overridefile);
	if (FAILEDTOALLOC(i)) {
		(void)fclose(file);
		return RET_ERROR_OOM;
	}

	while (fgets(buffer, 1000, file) != NULL){
		retvalue r;
		const char *firstpart, *secondpart, *thirdpart;
		char *p;
		size_t l = strlen(buffer);

		if (buffer[l-1] != '\n') {
			if (l >= 999) {
				fprintf(stderr,
"Too long line in '%s'!\n",
						filename);
				override_free(i);
				(void)fclose(file);
				return RET_ERROR;
			}
			fprintf(stderr, "Missing line terminator in '%s'!\n",
					filename);
		} else {
			l--;
			buffer[l] = '\0';
		}
		while (l>0 && xisspace(buffer[l])) {
			buffer[l] = '\0';
			l--;
		}
		if (l== 0)
			continue;
		p = buffer;
		while (*p !='\0' && xisspace(*p))
			*(p++)='\0';
		firstpart = p;
		while (*p !='\0' && !xisspace(*p))
			p++;
		while (*p !='\0' && xisspace(*p))
			*(p++)='\0';
		secondpart = p;
		while (*p !='\0' && !xisspace(*p))
			p++;
		while (*p !='\0' && xisspace(*p))
			*(p++)='\0';
		thirdpart = p;
		r = add_override(i, firstpart, secondpart, thirdpart, source);
		if (RET_WAS_ERROR(r)) {
			override_free(i);
			(void)fclose(file);
			return r;
		}
	}
	(void)fclose(file);
	if (i->packages != NULL || i->patterns != NULL) {
		*info = i;
		return RET_OK;
	} else {
		override_free(i);
		*info = NULL;
		return RET_NOTHING;
	}
}

const struct overridedata *override_search(const struct overridefile *overrides, const char *package) {
	struct overridepackage pkg, **node;
	struct overridepattern *p;

	if (overrides == NULL)
		return NULL;

	pkg.packagename = (char*)package;
	node = tfind(&pkg, &overrides->packages, opackage_compare);
	if (node != NULL && *node != NULL)
		return &(*node)->data;
	for (p = overrides->patterns ; p != NULL ; p = p->next) {
		if (globmatch(package, p->pattern))
			return &p->data;
	}
	return NULL;
}

const char *override_get(const struct overridedata *override, const char *field) {
	int i;

	if (override == NULL)
		return NULL;

	for (i = 0 ; i+1 < override->fields.count ; i+=2) {
		// TODO currently case-sensitiv. warn if otherwise?
		if (strcmp(override->fields.values[i], field) == 0)
			return override->fields.values[i+1];
	}
	return NULL;
}

/* add new fields to otherreplaces, but not "Section", or "Priority".
 * incorporates otherreplaces, or frees them on error,
 * returns otherreplaces when nothing was to do, NULL on RET_ERROR_OOM*/
struct fieldtoadd *override_addreplacefields(const struct overridedata *override, struct fieldtoadd *otherreplaces) {
	int i;

	if (override == NULL)
		return otherreplaces;

	for (i = 0 ; i+1 < override->fields.count ; i+=2) {
		if (strcmp(override->fields.values[i],
					SECTION_FIELDNAME) != 0 &&
				strcmp(override->fields.values[i],
					PRIORITY_FIELDNAME) != 0 &&
				override->fields.values[i][0] != '$') {
			otherreplaces = addfield_new(
				override->fields.values[i],
				override->fields.values[i+1],
				otherreplaces);
			if (otherreplaces == NULL)
				return NULL;
		} else if (strcasecmp(override->fields.values[i],
					"$delete") == 0) {
			otherreplaces = deletefield_new(
				override->fields.values[i+1], otherreplaces);
			if (otherreplaces == NULL)
				return NULL;
		}
	}
	return otherreplaces;

}

retvalue override_allreplacefields(const struct overridedata *override, struct fieldtoadd **fields_p) {
	int i;
	struct fieldtoadd *fields = NULL;

	assert (override != NULL);

	for (i = 0 ; i+1 < override->fields.count ; i+=2) {
		if (override->fields.values[i][0] != '$') {
			fields = addfield_new(
				override->fields.values[i],
				override->fields.values[i+1],
				fields);
			if (FAILEDTOALLOC(fields))
				return RET_ERROR_OOM;
		} else if (strcasecmp(override->fields.values[i],
					"$delete") == 0) {
			fields = deletefield_new(
				override->fields.values[i+1], fields);
			if (FAILEDTOALLOC(fields))
				return RET_ERROR_OOM;
		}
	}
	if (fields == NULL)
		return RET_NOTHING;
	*fields_p = fields;
	return RET_OK;
}
