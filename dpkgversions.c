/*
 * Most contents of this file are taken from:
 * libdpkg - Debian packaging suite library routines
 * from the files
 * parsehelp.c - helpful routines for parsing and writing
 * and
 * vercmp.c - comparison of version numbers
 *
 * Copyright (C) 1995 Ian Jackson <ian@chiark.greenend.org.uk>
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with dpkg; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02111-1301  USA
 */
#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <ctype.h>
#include "error.h"
#include "dpkgversions.h"

#define _(a) a
#define cisalpha(a) (isalpha(a)!=0)
#define cisdigit(a) (isdigit(a)!=0)

/* from dpkg-db.h.in: */

struct versionrevision {
  unsigned long epoch;
  const char *version;
  const char *revision;
};

/* from parsehelp.c */

static
const char *parseversion(struct versionrevision *rversion, const char *string) {
  char *hyphen, *colon, *eepochcolon;
  const char *end, *ptr;
  unsigned long epoch;

  if (!*string) return _("version string is empty");

  /* trim leading and trailing space */
  while (*string && (*string == ' ' || *string == '\t')) string++;
  /* string now points to the first non-whitespace char */
  end = string;
  /* find either the end of the string, or a whitespace char */
  while (*end && *end != ' ' && *end != '\t') end++;
  /* check for extra chars after trailing space */
  ptr = end;
  while (*ptr && (*ptr == ' ' || *ptr == '\t')) ptr++;
  if (*ptr) return _("version string has embedded spaces");

  colon= strchr(string, ':');
  if (colon) {
    epoch= strtoul(string, &eepochcolon, 10);
    if (colon != eepochcolon) return _("epoch in version is not number");
    if (!*++colon) return _("nothing after colon in version number");
    string= colon;
    rversion->epoch= epoch;
  } else {
    rversion->epoch= 0;
  }
  rversion->version= strndup(string, end - string);
  hyphen= strrchr(rversion->version,'-');
  if (hyphen) *hyphen++= 0;
  rversion->revision= hyphen ? hyphen : "";

  return NULL;
}

/* from vercmp.c */

/* assume ascii; warning: evaluates x multiple times! */
#define order(x) ((x) == '~' ? -1 \
		: cisdigit((x)) ? 0 \
		: !(x) ? 0 \
		: cisalpha((x)) ? (x) \
		: (x) + 256)

static int verrevcmp(const char *val, const char *ref) {
  if (!val) val= "";
  if (!ref) ref= "";

  while (*val || *ref) {
    int first_diff= 0;

    while ((*val && !cisdigit(*val)) || (*ref && !cisdigit(*ref))) {
      int vc= order(*val), rc= order(*ref);
      if (vc != rc) return vc - rc;
      val++; ref++;
    }

    while (*val == '0') val++;
    while (*ref == '0') ref++;
    while (cisdigit(*val) && cisdigit(*ref)) {
      if (!first_diff) first_diff= *val - *ref;
      val++; ref++;
    }
    if (cisdigit(*val)) return 1;
    if (cisdigit(*ref)) return -1;
    if (first_diff) return first_diff;
  }
  return 0;
}

static
int versioncompare(const struct versionrevision *version,
                   const struct versionrevision *refversion) {
  int r;

  if (version->epoch > refversion->epoch) return 1;
  if (version->epoch < refversion->epoch) return -1;
  r= verrevcmp(version->version,refversion->version);  if (r) return r;
  return verrevcmp(version->revision,refversion->revision);
}

/* now own code */

retvalue dpkgversions_cmp(const char *first,const char *second,int *result) {
	struct versionrevision v1,v2;
	const char *m;

	if ((m = parseversion(&v1,first)) != NULL) {
	   fprintf(stderr,"Error while parsing '%s' as version: %s\n",first,m);
	   return RET_ERROR;
	}
	if ((m = parseversion(&v2,second)) != NULL) {
	   fprintf(stderr,"Error while parsing '%s' as version: %s\n",second,m);
	   return RET_ERROR;
	}
	*result = versioncompare(&v1,&v2);
	free((char*)v1.version);
	free((char*)v2.version);
	return RET_OK;
}
