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
#include <zlib.h>
#include "error.h"
#include "md5sum.h"
#include "chunks.h"
#include "checkindsc.h"

extern int verbose;

// This file shall include the code to include sources, i.e.
// create or adopt the chunk of the Sources.gz-file and 
// putting it in the various databases.

// should superseed the add_source from main.c for inclusion
// of downloaded packages from main.c

/* things to do with .dsc's checkin by hand: (by comparison with apt-ftparchive)
Get all from .dsc (search the chunk with
the Source:-field. end the chunk artifical
before the pgp-end-block.(in case someone
missed the newline there))

* check to have source,version,maintainer,
  standards-version, files. And also look
  at binary,architecture and build*, as
  described in policy 5.4

Get overwrite information, ecspecially
the priority(if there is a binaries field,
check the one with the highest) and the section 
(...what else...?)

- Rename Source-Field to Package-Field

- add dsc to files-list. (check other files md5sum and size)

- add Directory-field

- Add Priority and Statues

- apply possible maintainer-updates from the overwrite-file
  or arbitrary tag changes from the extra-overwrite-file

- keep rest (perhaps sort alphabetical)

*/
