/*  This file is part of "mirrorer" (TODO: find better title)
 *  Copyright (C) 2003 Bernhard R. Link
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <config.h>

#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <malloc.h>
#include <ctype.h>
#include <db.h>
#include "error.h"
#include "strlist.h"
#include "md5sum.h"
#include "names.h"
#include "dirs.h"
#include "chunks.h"
#include "reference.h"
#include "packages.h"
#include "signature.h"
#include "sources.h"
#include "files.h"
#include "checkindsc.h"
#include "checkindeb.h"
#include "checkin.h"

extern int verbose;

/* insert the given .changes into the mirror in the <distribution>
 * if forcecomponent, forcesection or forcepriority is NULL
 * get it from the files or try to guess it. */
retvalue changes_add(const char *dbdir,DB *references,DB *filesdb,const char *mirrordir,const char *forcecomponent,const char *forcedsection,const char *forcepriority,struct distribution *distribution,const char *changesfilename,int force) {
	// TODO: implement it
}
