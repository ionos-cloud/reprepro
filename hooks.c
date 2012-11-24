/*  This file is part of "reprepro"
 *  Copyright (C) 2007,2012 Bernhard R. Link
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

/* general helpers infrastructure for all hooks: */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "error.h"
#include "hooks.h"

void sethookenvironment(const char *causing_file, const char *causing_rule, const char *suite_from, const char *exitcode) {
	if (exitcode != NULL)
		setenv("REPREPRO_EXIT_CODE", exitcode, true);
	else
		unsetenv("REPREPRO_EXIT_CODE");
	if (causing_file != NULL)
		setenv("REPREPRO_CAUSING_FILE", causing_file, true);
	else
		unsetenv("REPREPRO_CAUSING_FILE");
	if (causing_rule != NULL)
		setenv("REPREPRO_CAUSING_RULE", causing_rule, true);
	else
		unsetenv("REPREPRO_CAUSING_RULE");
	if (suite_from != NULL)
		setenv("REPREPRO_FROM", suite_from, true);
	else
		unsetenv("REPREPRO_FROM");
	if (atom_defined(causingcommand))
		setenv("REPREPRO_CAUSING_COMMAND",
				atoms_commands[causingcommand],
				true);
	else
		unsetenv("REPREPRO_CAUSING_COMMAND");
	setenv("REPREPRO_BASE_DIR", global.basedir, true);
	setenv("REPREPRO_OUT_DIR", global.outdir, true);
	setenv("REPREPRO_CONF_DIR", global.confdir, true);
	setenv("REPREPRO_CONFIG_DIR", global.confdir, true);
	setenv("REPREPRO_DIST_DIR", global.distdir, true);
	setenv("REPREPRO_LOG_DIR", global.logdir, true);
}

/* global variables to denote current state */
const char *causingfile = NULL; /* only valid while being called */
command_t causingcommand = atom_unknown; /* valid till end of program */

