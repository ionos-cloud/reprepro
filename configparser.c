/*  This file is part of "reprepro"
 *  Copyright (C) 2007 Bernhard R. Link
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
#include <limits.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "error.h"
#include "names.h"
#include "atoms.h"
#include "filecntl.h"
#include "configparser.h"

struct configiterator {
	FILE *f;
	unsigned int startline, line, column, markerline, markercolumn;
	const char *filename;
	const char *chunkname;
	bool eol;
};

const char *config_filename(const struct configiterator *iter) {
	return iter->filename;
}

unsigned int config_line(const struct configiterator *iter) {
	return iter->line;
}

unsigned int config_column(const struct configiterator *iter) {
	return iter->column;
}

unsigned int config_firstline(const struct configiterator *iter) {
	return iter->startline;
}

unsigned int config_markerline(const struct configiterator *iter) {
	return iter->markerline;
}

unsigned int config_markercolumn(const struct configiterator *iter) {
	return iter->markercolumn;
}

void config_overline(struct configiterator *iter) {
	int c;

	while (!iter->eol) {
		c = fgetc(iter->f);
		if (c == '#') {
			do {
				c = fgetc(iter->f);
			} while (c != EOF && c != '\n');
		}
		if (c == EOF || c == '\n')
			iter->eol = true;
		else
			iter->column++;
	}
}

bool config_nextline(struct configiterator *iter) {
	int c;

	assert (iter->eol);
	c = fgetc(iter->f);
	while (c == '#') {
		do {
			c = fgetc(iter->f);
		} while (c != EOF && c != '\n');
		iter->line++;
		c = fgetc(iter->f);
	}
	if (c == EOF)
		return false;
	if (c == ' ' || c == '\t') {
		iter->line++;
		iter->column = 1;
		iter->eol = false;
		return true;
	}
	(void)ungetc(c, iter->f);
	return false;
}

retvalue linkedlistfinish(UNUSED(void *privdata), void *this, void **last, UNUSED(bool complete), UNUSED(struct configiterator *dummy3)) {
	*last = this;
	return RET_NOTHING;
}

static inline retvalue finishchunk(configfinishfunction finishfunc, void *privdata, struct configiterator *iter, const struct configfield *fields, size_t fieldcount, bool *found, void **this, void **last, bool complete) {
	size_t i;
	retvalue r;

	if (complete)
		for (i = 0 ; i < fieldcount ; i++) {
			if (!fields[i].required)
				continue;
			if (found[i])
				continue;
			fprintf(stderr,
"Error parsing config file %s, line %u:\n"
"Required field '%s' not found in\n"
"%s starting in line %u and ending in line %u.\n",
					iter->filename, iter->line,
					fields[i].name, iter->chunkname,
					iter->startline, iter->line-1);
			(void)finishfunc(privdata, *this, last, false, iter);
			*this = NULL;
			return RET_ERROR_MISSING;
		}
	r = finishfunc(privdata, *this, last, complete, iter);
	*this = NULL;
	return r;
}

char *configfile_expandname(const char *filename, char *fndup) {
	const char *fromdir;
	char *n;

	assert (fndup == NULL || fndup == filename);

	if (filename[0] == '/' || (filename[0] == '.' && filename[1] == '/'))
		return fndup?fndup:strdup(filename);
	if (filename[0] == '~' && filename[1] == '/') {
		n = calc_dirconcat(getenv("HOME"), filename + 2);
		free(fndup);
		return n;
	}
	if (filename[0] != '+' || filename[1] == '\0' || filename[2] != '/') {
		n = calc_dirconcat(global.confdir, filename);
		free(fndup);
		return n;
	}
	if (filename[1] == 'b') {
		fromdir = global.basedir;
	} else if (filename[1] == 'o') {
		fromdir = global.outdir;
	} else if (filename[1] == 'c') {
		fromdir = global.confdir;
	} else {
		fprintf(stderr, "Warning: strange filename '%s'!\n",
				filename);
		return fndup?fndup:strdup(filename);
	}
	n = calc_dirconcat(fromdir, filename + 3);
	free(fndup);
	return n;
}

static retvalue configfile_parse_multi(/*@only@*/char *, bool, configinitfunction, configfinishfunction, const char *, const struct configfield *, size_t, void *, int, void **, struct strlist *);

static retvalue configfile_parse_single(/*@only@*/char *filename, bool ignoreunknown, configinitfunction initfunc, configfinishfunction finishfunc, const char *chunkname, const struct configfield *fields, size_t fieldcount, void *privdata, int depth, void **last_p, struct strlist *filenames) {
	bool found[fieldcount];
	void *this = NULL;
	char key[100];
	size_t keylen;
	int c, ret;
	size_t i;
	struct configiterator iter;
	retvalue result, r;
	bool afterinclude = false;

	if (strlist_in(filenames, filename)) {
		if (verbose >= 0) {
			fprintf(stderr,
"Ignoring subsequent inclusion of '%s'!\n", filename);
		}
		free(filename);
		return RET_NOTHING;
	}
	iter.filename = filename;
	r = strlist_add(filenames, filename);
	if (RET_WAS_ERROR(r))
		return r;
	iter.chunkname = chunkname;
	iter.line = 0;
	iter.column = 0;

	iter.f = fopen(iter.filename, "r");
	if (iter.f == NULL) {
		int e = errno;
		fprintf(stderr, "Error opening config file '%s': %s(%d)\n",
				iter.filename, strerror(e), e);
		return RET_ERRNO(e);
	}
	result = RET_NOTHING;
	do {
		iter.line++;
		iter.column = 1;

		c = fgetc(iter.f);
		while (c == '#') {
			do {
				c = fgetc(iter.f);
			} while (c != EOF && c != '\n');
			iter.line++;
			c = fgetc(iter.f);
		}
		if (c == '\r')  {
			do {
				c = fgetc(iter.f);
			} while (c == '\r');
			if (c != EOF && c != '\n') {
				fprintf(stderr,
"%s:%u: error parsing configuration file: CR without following LF!\n",
						iter.filename, iter.line);
				result = RET_ERROR;
				break;
			}
		}
		if (c == EOF)
			break;
		if (c == '\n') {
			afterinclude = false;
			/* Ignore multiple emptye lines */
			if (this == NULL)
				continue;
			/* finish this chunk, to get ready for the next: */
			r = finishchunk(finishfunc, privdata, &iter,
					fields, fieldcount, found,
					&this, last_p, true);
			if (RET_WAS_ERROR(r)) {
				result = r;
				break;
			}
			continue;
		}
		if (afterinclude) {
			fprintf(stderr,
"Warning parsing %s, line %u: no empty line after '!include'-sequence"
" might cause ambiguity in the future!\n",
					iter.filename, iter.line);
			afterinclude = false;
		}
		if (c == '!') {
			keylen = 0;
			while ((c = fgetc(iter.f)) != EOF && c >= 'a' && c <= 'z') {
				iter.column++;
				key[keylen++] = c;
				if (keylen >= 10)
					break;
			}
			if (c != ':') {
				fprintf(stderr,
"Error parsing %s, line %u: invalid !-sequence!\n",
					iter.filename, iter.line);
				result = RET_ERROR;
				break;
			}
			iter.column++;
			if (keylen == 7 && memcmp(key, "include", 7) == 0) {
				char *filetoinclude;

				if (this != NULL) {
					fprintf(stderr,
"Error parsing %s, line %u: '!include' statement within unterminated %s!\n"
"(perhaps you forgot to put an empty line before this)\n",
						iter.filename, iter.line,
						chunkname);
					result = RET_ERROR;
					break;
				}
				if (depth > 20) {
					fprintf(stderr,
"Error parsing %s, line %u: too many nested '!include' statements!\n",
						iter.filename, iter.line);
					result = RET_ERROR;
					break;
				}
				r = config_getonlyword(&iter, "!include",
						NULL, &filetoinclude);
				if (RET_WAS_ERROR(r)) {
					result = r;
					break;
				}
				filetoinclude = configfile_expandname(
						filetoinclude, filetoinclude);
				r = configfile_parse_multi(filetoinclude,
						ignoreunknown,
						initfunc, finishfunc,
						chunkname,
						fields, fieldcount,
						privdata, depth + 1,
						last_p, filenames);
				if (RET_WAS_ERROR(r)) {
					result = r;
					break;
				}
				afterinclude = true;
			} else {
				key[keylen] = '\0';
				fprintf(stderr,
"Error parsing %s, line %u: unknown !-sequence '%s'!\n",
					iter.filename, iter.line, key);
				result = RET_ERROR;
				break;
			}
			/* ignore all data left of this field */
			do {
				config_overline(&iter);
			} while (config_nextline(&iter));
			continue;
		}
		if (c == '\0') {
			fprintf(stderr,
"Error parsing %s, line %u: \\000 character not allowed in config files!\n",
					iter.filename, iter.line);
			result = RET_ERROR;
			break;
		}
		if (c == ' ' || c == '\t') {
			fprintf(stderr,
"Error parsing %s, line %u: unexpected white space before keyword!\n",
					iter.filename, iter.line);
			result = RET_ERROR;
			break;
		}
		key[0] = c;
		keylen = 1;

		while ((c = fgetc(iter.f)) != EOF && c != ':' && c != '\n'
				&& c != '#' && c != '\0') {
			iter.column++;
			if (c == ' ') {
				fprintf(stderr,
"Error parsing %s, line %u: Unexpected space in header name!\n",
						iter.filename, iter.line);
				result = RET_ERROR;
				break;
			}
			if (c == '\t') {
				fprintf(stderr,
"Error parsing %s, line %u: Unexpected tabulator character in header name!\n",
						iter.filename, iter.line);
				result = RET_ERROR;
				break;
			}
			key[keylen++] = c;
			if (keylen >= 100)
				break;
		}
		if (c != ':') {
			if (c != ' ' && c != '\t')
				/* newline or end-of-file */
				fprintf(stderr,
"Error parsing %s, line %u, column %u: Colon expected!\n",
					iter.filename, iter.line, iter.column);
			result = RET_ERROR;
			break;
		}
		if (this == NULL) {
			/* new chunk, initialize everything */
			r = initfunc(privdata, *last_p, &this);
			assert (r != RET_NOTHING);
			if (RET_WAS_ERROR(r)) {
				result = r;
				break;
			}
			assert (this != NULL);
			iter.startline = iter.line;
			memset(found, 0, sizeof(found));
		}
		for (i = 0 ; i < fieldcount ; i++) {
			if (keylen != fields[i].namelen)
				continue;
			if (strncasecmp(key, fields[i].name, keylen) != 0)
				continue;
			break;
		}
		if (i >= fieldcount) {
			key[keylen] = '\0';
			if (!ignoreunknown) {
				fprintf(stderr,
"Error parsing %s, line %u: Unknown header '%s'!\n",
						iter.filename, iter.line, key);
				result = RET_ERROR_UNKNOWNFIELD;
				break;
			}
			if (verbose >= 0)
				fprintf(stderr,
"Warning parsing %s, line %u: Unknown header '%s'!\n",
						iter.filename, iter.line, key);
		} else if (found[i]) {
			fprintf(stderr,
"Error parsing %s, line %u: Second appearance of '%s' in the same chunk!\n",
				iter.filename, iter.line, fields[i].name);
			result = RET_ERROR;
			break;
		} else
			found[i] = true;
		do {
			c = fgetc(iter.f);
			iter.column++;
		} while (c == ' ' || c == '\t');
		(void)ungetc(c, iter.f);

		iter.eol = false;
		if (i < fieldcount) {
			r = fields[i].setfunc(privdata, fields[i].name, this, &iter);
			RET_UPDATE(result, r);
			if (RET_WAS_ERROR(r))
				break;
		}
		/* ignore all data left of this field */
		do {
			config_overline(&iter);
		} while (config_nextline(&iter));
	} while (true);
	if (this != NULL) {
		r = finishchunk(finishfunc, privdata, &iter,
				fields, fieldcount, found,
				&this, last_p,
				!RET_WAS_ERROR(result));
		RET_UPDATE(result, r);
	}
	if (ferror(iter.f) != 0) {
		int e = errno;
		fprintf(stderr, "Error reading config file '%s': %s(%d)\n",
				iter.filename, strerror(e), e);
		r = RET_ERRNO(e);
		RET_UPDATE(result, r);
	}
	ret = fclose(iter.f);
	if (ret != 0) {
		int e = errno;
		fprintf(stderr, "Error closing config file '%s': %s(%d)\n",
				iter.filename, strerror(e), e);
		r = RET_ERRNO(e);
		RET_UPDATE(result, r);
	}
	return result;
}

static retvalue configfile_parse_multi(/*@only@*/char *fullfilename, bool ignoreunknown, configinitfunction initfunc, configfinishfunction finishfunc, const char *chunkname, const struct configfield *fields, size_t fieldcount, void *privdata, int depth, void **last_p, struct strlist *filenames) {
	retvalue result = RET_NOTHING, r;

	if (isdirectory(fullfilename)) {
		DIR *dir;
		struct dirent *de;
		int e;
		char *subfilename;

		dir = opendir(fullfilename);
		if (dir == NULL) {
			e = errno;
			fprintf(stderr,
"Error %d opening directory '%s': %s\n",
				e, fullfilename, strerror(e));
			free(fullfilename);
			return RET_ERRNO(e);
		}
		while ((errno = 0, de = readdir(dir)) != NULL) {
			size_t l;
			if (de->d_type != DT_REG && de->d_type != DT_LNK
					&& de->d_type != DT_UNKNOWN)
				continue;
			if (de->d_name[0] == '.')
				continue;
			l = strlen(de->d_name);
			if (l < 5 || strcmp(de->d_name + l - 5, ".conf") != 0)
				continue;
			subfilename = calc_dirconcat(fullfilename, de->d_name);
			if (FAILEDTOALLOC(subfilename)) {
				(void)closedir(dir);
				free(fullfilename);
				return RET_ERROR_OOM;
			}
			r = configfile_parse_single(subfilename, ignoreunknown,
				initfunc, finishfunc,
				chunkname, fields, fieldcount, privdata,
				depth, last_p, filenames);
			RET_UPDATE(result, r);
			if (RET_WAS_ERROR(r)) {
				(void)closedir(dir);
				free(fullfilename);
				return r;
			}
		}
		e = errno;
		if (e != 0) {
			(void)closedir(dir);
			fprintf(stderr,
"Error %d reading directory '%s': %s\n",
				e, fullfilename, strerror(e));
			free(fullfilename);
			return RET_ERRNO(e);
		}
		if (closedir(dir) != 0) {
			e = errno;
			fprintf(stderr,
"Error %d closing directory '%s': %s\n",
				e, fullfilename, strerror(e));
			free(fullfilename);
			return RET_ERRNO(e);
		}
		free(fullfilename);
	} else {
		r = configfile_parse_single(fullfilename, ignoreunknown,
				initfunc, finishfunc,
				chunkname, fields, fieldcount, privdata,
				depth, last_p, filenames);
		RET_UPDATE(result, r);
	}
	return result;
}

retvalue configfile_parse(const char *filename, bool ignoreunknown, configinitfunction initfunc, configfinishfunction finishfunc, const char *chunkname, const struct configfield *fields, size_t fieldcount, void *privdata) {
	struct strlist filenames;
	void *last = NULL;
	retvalue r;
	char *fullfilename;

	fullfilename = configfile_expandname(filename, NULL);
	if (fullfilename == NULL)
		return RET_ERROR_OOM;

	strlist_init(&filenames);

	r = configfile_parse_multi(fullfilename, ignoreunknown,
			initfunc, finishfunc,
			chunkname, fields, fieldcount, privdata,
			0, &last, &filenames);

	/* only free filenames last, as they might still be
	 * referenced while running */
	strlist_done(&filenames);
	return r;
}

static inline int config_nextchar(struct configiterator *iter) {
	int c;
	unsigned int realcolumn;

	c = fgetc(iter->f);
	realcolumn = iter->column + 1;
	if (c == '#') {
		do {
			c = fgetc(iter->f);
			realcolumn++;
		} while (c != '\n' && c != EOF && c != '\r');
	}
	if (c == '\r') {
		while (c == '\r') {
			realcolumn++;
			c = fgetc(iter->f);
		}
		if (c != '\n' && c != EOF) {
			fprintf(stderr,
"Warning parsing config file '%s', line '%u', column %u: CR not followed by LF!\n",
					config_filename(iter),
					config_line(iter),
					realcolumn);

		}
	}
	if (c == EOF) {
		fprintf(stderr,
"Warning parsing config file '%s', line '%u': File ending without final LF!\n",
				config_filename(iter),
				config_line(iter));
		/* fake a proper text file: */
		c = '\n';
	}
	iter->column++;
	if (c == '\n')
		iter->eol = true;
	return c;
}

static inline int config_nextnonspace(struct configiterator *iter) {
	int c;

	do {
		iter->markerline = iter->line;
		iter->markercolumn = iter->column;
		if (iter->eol) {
			if (!config_nextline(iter))
				return EOF;
		}
		c = config_nextchar(iter);
	} while (c == '\n' || c == ' ' || c == '\t');
	return c;
}

int config_nextnonspaceinline(struct configiterator *iter) {
	int c;

	do {
		iter->markerline = iter->line;
		iter->markercolumn = iter->column;
		if (iter->eol)
			return EOF;
		c = config_nextchar(iter);
		if (c == '\n')
			return EOF;
	} while (c == '\r' || c == ' ' || c == '\t');
	return c;
}

#define configparser_errorlast(iter, message, ...) \
	fprintf(stderr, "Error parsing %s, line %u, column %u: " message "\n", \
			iter->filename, iter->markerline, \
			iter->markercolumn, ##  __VA_ARGS__);
#define configparser_error(iter, message, ...) \
	fprintf(stderr, "Error parsing %s, line %u, column %u: " message "\n", \
			iter->filename, iter->line, \
			iter->column, ##  __VA_ARGS__);

retvalue config_completeword(struct configiterator *iter, char firstc, char **result_p) {
	size_t size = 0, len = 0;
	char *value = NULL, *nv;
	int c = firstc;

	iter->markerline = iter->line;
	iter->markercolumn = iter->column;
	do {
		if (len + 2 >= size) {
			nv = realloc(value, size+128);
			if (FAILEDTOALLOC(nv)) {
				free(value);
				return RET_ERROR_OOM;
			}
			size += 128;
			value = nv;
		}
		value[len] = c;
		len++;
		c = config_nextchar(iter);
		if (c == '\n')
			break;
	} while (c != ' ' && c != '\t');
	assert (len > 0);
	assert (len < size);
	value[len] = '\0';
	nv = realloc(value, len+1);
	if (nv == NULL)
		*result_p = value;
	else
		*result_p = nv;
	return RET_OK;
}

retvalue config_getwordinline(struct configiterator *iter, char **result_p) {
	int c;

	c = config_nextnonspaceinline(iter);
	if (c == EOF)
		return RET_NOTHING;
	return config_completeword(iter, c, result_p);
}

retvalue config_getword(struct configiterator *iter, char **result_p) {
	int c;

	c = config_nextnonspace(iter);
	if (c == EOF)
		return RET_NOTHING;
	return config_completeword(iter, c, result_p);
}

retvalue config_gettimespan(struct configiterator *iter, const char *header, unsigned long *time_p) {
	long long currentnumber, currentsum = 0;
	bool empty = true;
	int c;

	do {
		c = config_nextnonspace(iter);
		if (c == EOF) {
			if (empty) {
				configparser_errorlast(iter,
"Unexpected end of %s header (value expected).", header);
				return RET_ERROR;
			}
			*time_p = currentsum;
			return RET_OK;
		}
		iter->markerline = iter->line;
		iter->markercolumn = iter->column;
		currentnumber = 0;
		if (c < '0' || c > '9') {
			configparser_errorlast(iter,
"Unexpected character '%c' where a digit was expected in %s header.",
					(char)c, header);
			return RET_ERROR;
		}
		empty = false;
		do {
			if (currentnumber > 3660) {
				configparser_errorlast(iter,
"Absurdly long time span (> 100 years) in %s header.", header);
				return RET_ERROR;
			}
			currentnumber *= 10;
			currentnumber += (c - '0');
			c = config_nextchar(iter);
		} while (c >= '0' && c <= '9');
		if (c == ' ' || c == '\t' || c == '\n')
			c = config_nextnonspace(iter);
		if (c == 'y') {
			if (currentnumber > 100) {
				configparser_errorlast(iter,
"Absurdly long time span (> 100 years) in %s header.", header);
				return RET_ERROR;
			}
			currentnumber *= 365*24*60*60;
		} else if (c == 'm') {
			if (currentnumber > 1200) {
				configparser_errorlast(iter,
"Absurdly long time span (> 100 years) in %s header.", header);
				return RET_ERROR;
			}
			currentnumber *= 31*24*60*60;
		} else if (c == 'd') {
			if (currentnumber > 36600) {
				configparser_errorlast(iter,
"Absurdly long time span (> 100 years) in %s header.", header);
				return RET_ERROR;
			}
			currentnumber *= 24*60*60;
		} else {
			if (currentnumber > 36600) {
				configparser_errorlast(iter,
"Absurdly long time span (> 100 years) in %s header.", header);
				return RET_ERROR;
			}
			currentnumber *= 24*60*60;
			if (c != EOF) {
				configparser_errorlast(iter,
"Unexpected character '%c' where a 'd','m' or 'y' was expected in %s header.",
					(char)c, header);
				return RET_ERROR;
			}
		}
		currentsum += currentnumber;
	} while (true);
}

retvalue config_getonlyword(struct configiterator *iter, const char *header, checkfunc check, char **result_p) {
	char *value;
	retvalue r;

	r = config_getword(iter, &value);
	if (r == RET_NOTHING) {
		configparser_errorlast(iter,
"Unexpected end of %s header (value expected).", header);
		return RET_ERROR;
	}
	if (RET_WAS_ERROR(r))
		return r;
	if (config_nextnonspace(iter) != EOF) {
		configparser_error(iter,
"End of %s header expected (but trailing garbage).", header);
		free(value);
		return RET_ERROR;
	}
	if (check != NULL) {
		const char *errormessage = check(value);
		if (errormessage != NULL) {
			configparser_errorlast(iter,
"Malformed %s content '%s': %s", header, value, errormessage);
			free(value);
			checkerror_free(errormessage);
			return RET_ERROR;
		}
	}
	*result_p = value;
	return RET_OK;
}

retvalue config_getscript(struct configiterator *iter, const char *name, char **value_p) {
	char *value;
	retvalue r;

	r = config_getonlyword(iter, name, NULL, &value);
	if (RET_IS_OK(r)) {
		assert (value != NULL && value[0] != '\0');
		value = configfile_expandname(value, value);
		if (FAILEDTOALLOC(value))
			return RET_ERROR_OOM;
		*value_p = value;
	}
	return r;
}

retvalue config_geturl(struct configiterator *iter, const char *header, char **result_p) {
	char *value, *p;
	retvalue r;
	size_t l;


	r = config_getword(iter, &value);
	if (r == RET_NOTHING) {
		configparser_errorlast(iter,
"Unexpected end of %s header (value expected).", header);
		return RET_ERROR;
	}
	if (RET_WAS_ERROR(r))
		return r;
	// TODO: think about allowing (escaped) spaces...
	if (config_nextnonspace(iter) != EOF) {
		configparser_error(iter,
"End of %s header expected (but trailing garbage).", header);
		free(value);
		return RET_ERROR;
	}
	p = value;
	while (*p != '\0' && (*p == '_' || *p == '-' || *p == '+' ||
				(*p>='a' && *p<='z') || (*p>='A' && *p<='Z') ||
				(*p>='0' && *p<='9'))) {
		p++;
	}
	if (*p != ':') {
		configparser_errorlast(iter,
"Malformed %s field: no colon (must be method:path).", header);
		free(value);
		return RET_ERROR;
	}
	if (p == value) {
		configparser_errorlast(iter,
"Malformed %s field: transport method name expected (colon is not allowed to be the first character)!", header);
		free(value);
		return RET_ERROR;
	}
	p++;
	l = strlen(p);
	/* remove one leading slash, as we always add one and some apt-methods
	 * are confused with //. (end with // if you really want it) */
	if (l > 0 && p[l - 1] == '/')
		p[l - 1] = '\0';
	*result_p = value;
	return RET_OK;
}

retvalue config_getuniqwords(struct configiterator *iter, const char *header, checkfunc check, struct strlist *result_p) {
	char *value;
	retvalue r;
	struct strlist data;
	const char *errormessage;

	strlist_init(&data);
	while ((r = config_getword(iter, &value)) != RET_NOTHING) {
		if (RET_WAS_ERROR(r)) {
			strlist_done(&data);
			return r;
		}
		if (strlist_in(&data, value)) {
			configparser_errorlast(iter,
"Unexpected duplicate '%s' within %s header.", value, header);
			free(value);
			strlist_done(&data);
			return RET_ERROR;
		} else if (check != NULL && (errormessage = check(value)) != NULL) {
			configparser_errorlast(iter,
"Malformed %s element '%s': %s", header, value, errormessage);
			checkerror_free(errormessage);
			free(value);
			strlist_done(&data);
			return RET_ERROR;
		} else {
			r = strlist_add(&data, value);
			if (RET_WAS_ERROR(r)) {
				strlist_done(&data);
				return r;
			}
		}
	}
	strlist_move(result_p, &data);
	return RET_OK;
}

retvalue config_getinternatomlist(struct configiterator *iter, const char *header, enum atom_type type, checkfunc check, struct atomlist *result_p) {
	char *value;
	retvalue r;
	struct atomlist data;
	const char *errormessage;
	atom_t atom;

	atomlist_init(&data);
	while ((r = config_getword(iter, &value)) != RET_NOTHING) {
		if (RET_WAS_ERROR(r)) {
			atomlist_done(&data);
			return r;
		}
		if (check != NULL && (errormessage = check(value)) != NULL) {
			configparser_errorlast(iter,
"Malformed %s element '%s': %s", header, value, errormessage);
			checkerror_free(errormessage);
			free(value);
			atomlist_done(&data);
			return RET_ERROR;
		}
		r = atom_intern(type, value, &atom);
		if (RET_WAS_ERROR(r))
			return r;
		r = atomlist_add_uniq(&data, atom);
		if (r == RET_NOTHING) {
			configparser_errorlast(iter,
"Unexpected duplicate '%s' within %s header.", value, header);
			free(value);
			atomlist_done(&data);
			return RET_ERROR;
		}
		free(value);
		if (RET_WAS_ERROR(r)) {
			atomlist_done(&data);
			return r;
		}
	}
	atomlist_move(result_p, &data);
	return RET_OK;
}

retvalue config_getatom(struct configiterator *iter, const char *header, enum atom_type type, atom_t *result_p) {
	char *value;
	retvalue r;
	atom_t atom;

	r = config_getword(iter, &value);
	if (r == RET_NOTHING) {
		configparser_errorlast(iter,
"Unexpected empty '%s' field.", header);
		r = RET_ERROR_MISSING;
	}
	if (RET_WAS_ERROR(r))
		return r;
	atom = atom_find(type, value);
	if (!atom_defined(atom)) {
		configparser_errorlast(iter,
"Not previously seen %s '%s' within '%s' field.", atomtypes[type], value, header);
		free(value);
		return RET_ERROR;
	}
	*result_p = atom;
	free(value);
	return RET_OK;
}

retvalue config_getatomlist(struct configiterator *iter, const char *header, enum atom_type type, struct atomlist *result_p) {
	char *value;
	retvalue r;
	struct atomlist data;
	atom_t atom;

	atomlist_init(&data);
	while ((r = config_getword(iter, &value)) != RET_NOTHING) {
		if (RET_WAS_ERROR(r)) {
			atomlist_done(&data);
			return r;
		}
		atom = atom_find(type, value);
		if (!atom_defined(atom)) {
			configparser_errorlast(iter,
"Not previously seen %s '%s' within '%s' header.", atomtypes[type], value, header);
			free(value);
			atomlist_done(&data);
			return RET_ERROR;
		}
		r = atomlist_add_uniq(&data, atom);
		if (r == RET_NOTHING) {
			configparser_errorlast(iter,
"Unexpected duplicate '%s' within %s header.", value, header);
			free(value);
			atomlist_done(&data);
			return RET_ERROR;
		}
		free(value);
		if (RET_WAS_ERROR(r)) {
			atomlist_done(&data);
			return r;
		}
	}
	atomlist_move(result_p, &data);
	return RET_OK;
}

retvalue config_getsplitatoms(struct configiterator *iter, const char *header, enum atom_type type, struct atomlist *from_p, struct atomlist *into_p) {
	char *value, *separator;
	atom_t origin, destination;
	retvalue r;
	struct atomlist data_from, data_into;

	atomlist_init(&data_from);
	atomlist_init(&data_into);
	while ((r = config_getword(iter, &value)) != RET_NOTHING) {
		if (RET_WAS_ERROR(r)) {
			atomlist_done(&data_from);
			atomlist_done(&data_into);
			return r;
		}
		separator = strchr(value, '>');
		if (separator == NULL) {
			separator = value;
			destination = atom_find(type, value);
			origin = destination;;
		} else if (separator == value) {
			destination = atom_find(type, separator + 1);
			origin = destination;;
		} else if (separator[1] == '\0') {
			*separator = '\0';
			separator = value;
			destination = atom_find(type, value);
			origin = destination;;
		} else {
			*separator = '\0';
			separator++;
			origin = atom_find(type, value);
			destination = atom_find(type, separator);
		}
		if (!atom_defined(origin)) {
			configparser_errorlast(iter,
"Unknown %s '%s' in %s.", atomtypes[type], value, header);
			free(value);
			atomlist_done(&data_from);
			atomlist_done(&data_into);
			return RET_ERROR;
		}
		if (!atom_defined(destination)) {
			configparser_errorlast(iter,
"Unknown %s '%s' in %s.", atomtypes[type], separator, header);
			free(value);
			atomlist_done(&data_from);
			atomlist_done(&data_into);
			return RET_ERROR;
		}
		free(value);
		r = atomlist_add(&data_from, origin);
		if (RET_WAS_ERROR(r)) {
			atomlist_done(&data_from);
			atomlist_done(&data_into);
			return r;
		}
		r = atomlist_add(&data_into, destination);
		if (RET_WAS_ERROR(r)) {
			atomlist_done(&data_from);
			atomlist_done(&data_into);
			return r;
		}
	}
	atomlist_move(from_p, &data_from);
	atomlist_move(into_p, &data_into);
	return RET_OK;
}

retvalue config_getatomsublist(struct configiterator *iter, const char *header, enum atom_type type, struct atomlist *result_p, const struct atomlist *superset, const char *superset_header) {
	char *value;
	retvalue r;
	struct atomlist data;
	atom_t atom;

	atomlist_init(&data);
	while ((r = config_getword(iter, &value)) != RET_NOTHING) {
		if (RET_WAS_ERROR(r)) {
			atomlist_done(&data);
			return r;
		}
		atom = atom_find(type, value);
		if (!atom_defined(atom) || !atomlist_in(superset, atom)) {
			configparser_errorlast(iter,
"'%s' not allowed in %s as it was not in %s.", value, header, superset_header);
			free(value);
			atomlist_done(&data);
			return RET_ERROR;
		}
		r = atomlist_add_uniq(&data, atom);
		if (r == RET_NOTHING) {
			configparser_errorlast(iter,
"Unexpected duplicate '%s' within %s header.", value, header);
			free(value);
			atomlist_done(&data);
			return RET_ERROR;
		}
		free(value);
		if (RET_WAS_ERROR(r)) {
			atomlist_done(&data);
			return r;
		}
	}
	atomlist_move(result_p, &data);
	return RET_OK;
}

retvalue config_getwords(struct configiterator *iter, struct strlist *result_p) {
	char *value;
	retvalue r;
	struct strlist data;

	strlist_init(&data);
	while ((r = config_getword(iter, &value)) != RET_NOTHING) {
		if (RET_WAS_ERROR(r)) {
			strlist_done(&data);
			return r;
		}
		r = strlist_add(&data, value);
		if (RET_WAS_ERROR(r)) {
			strlist_done(&data);
			return r;
		}
	}
	strlist_move(result_p, &data);
	return RET_OK;
}

retvalue config_getsignwith(struct configiterator *iter, const char *name, struct strlist *result_p) {
	char *value;
	retvalue r;
	struct strlist data;
	int c;

	strlist_init(&data);

	c = config_nextnonspace(iter);
	if (c == EOF) {
		configparser_errorlast(iter,
"Missing value for %s field.", name);
		return RET_ERROR;
	}
	/* if the first character is a '!', a script to start follows */
	if (c == '!') {
		const char *type = "!";

		iter->markerline = iter->line;
		iter->markercolumn = iter->column;
		c = config_nextchar(iter);
		if (c == '-') {
			configparser_errorlast(iter,
"'!-' in signwith lines reserved for future usage!\n");
			return RET_ERROR;
			type = "!-";
			c = config_nextnonspace(iter);
		} else if (c == '\n' || c == ' ' || c == '\t')
			c = config_nextnonspace(iter);
		if (c == EOF) {
			configparser_errorlast(iter,
"Missing value for %s field.", name);
			return RET_ERROR;
		}
		r = config_completeword(iter, c, &value);
		if (RET_WAS_ERROR(r))
			return r;
		if (config_nextnonspace(iter) != EOF) {
			configparser_error(iter,
"End of %s header expected (but trailing garbage).", name);
			free(value);
			return RET_ERROR;
		}
		assert (value != NULL && value[0] != '\0');
		value = configfile_expandname(value, value);
		if (FAILEDTOALLOC(value))
			return RET_ERROR_OOM;
		r = strlist_add_dup(&data, type);
		if (RET_WAS_ERROR(r)) {
			free(value);
			return r;
		}
		r = strlist_add(&data, value);
		if (RET_WAS_ERROR(r)) {
			strlist_done(&data);
			return r;
		}
		strlist_move(result_p, &data);
		return RET_OK;
	}
	/* otherwise each word is stored in the strlist */
	r = config_completeword(iter, c, &value);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r))
		return r;
	r = strlist_add(&data, value);
	if (RET_WAS_ERROR(r))
		return r;
	while ((r = config_getword(iter, &value)) != RET_NOTHING) {
		if (RET_WAS_ERROR(r)) {
			strlist_done(&data);
			return r;
		}
		r = strlist_add(&data, value);
		if (RET_WAS_ERROR(r)) {
			strlist_done(&data);
			return r;
		}
	}
	strlist_move(result_p, &data);
	return RET_OK;
}

retvalue config_getsplitwords(struct configiterator *iter, UNUSED(const char *header), struct strlist *from_p, struct strlist *into_p) {
	char *value, *origin, *destination, *separator;
	retvalue r;
	struct strlist data_from, data_into;

	strlist_init(&data_from);
	strlist_init(&data_into);
	while ((r = config_getword(iter, &value)) != RET_NOTHING) {
		if (RET_WAS_ERROR(r)) {
			strlist_done(&data_from);
			strlist_done(&data_into);
			return r;
		}
		separator = strchr(value, '>');
		if (separator == NULL) {
			destination = strdup(value);
			origin = value;
		} else if (separator == value) {
			destination = strdup(separator+1);
			origin = strdup(separator+1);
			free(value);
		} else if (separator[1] == '\0') {
			*separator = '\0';
			destination = strdup(value);
			origin = value;
		} else {
			origin = strndup(value, separator-value);
			destination = strdup(separator+1);
			free(value);
		}
		if (FAILEDTOALLOC(origin) || FAILEDTOALLOC(destination)) {
			free(origin); free(destination);
			strlist_done(&data_from);
			strlist_done(&data_into);
			return RET_ERROR_OOM;
		}
		r = strlist_add(&data_from, origin);
		if (RET_WAS_ERROR(r)) {
			free(destination);
			strlist_done(&data_from);
			strlist_done(&data_into);
			return r;
		}
		r = strlist_add(&data_into, destination);
		if (RET_WAS_ERROR(r)) {
			strlist_done(&data_from);
			strlist_done(&data_into);
			return r;
		}
	}
	strlist_move(from_p, &data_from);
	strlist_move(into_p, &data_into);
	return RET_OK;
}

retvalue config_getconstant(struct configiterator *iter, const struct constant *constants, int *result_p) {
	retvalue r;
	char *value;
	const struct constant *c;

	/* that could be done more in-situ,
	 * but is not runtime-critical at all */

	r = config_getword(iter, &value);
	if (r == RET_NOTHING)
		return r;
	if (RET_WAS_ERROR(r))
		return r;
	for (c = constants ; c->name != NULL ; c++) {
		if (strcmp(c->name, value) == 0) {
			free(value);
			*result_p = c->value;
			return RET_OK;
		}
	}
	free(value);
	return RET_ERROR_UNKNOWNFIELD;
}

retvalue config_getflags(struct configiterator *iter, const char *header, const struct constant *constants, bool *flags, bool ignoreunknown, const char *msg) {
	retvalue r, result = RET_NOTHING;
	int option = -1;

	while (true) {
		r = config_getconstant(iter, constants, &option);
		if (r == RET_NOTHING)
			break;
		if (r == RET_ERROR_UNKNOWNFIELD) {
// TODO: would be nice to have the wrong flag here to put it in the error message:
			if (ignoreunknown) {
				fprintf(stderr,
"Warning: ignored error parsing config file %s, line %u, column %u:\n"
"Unknown flag in %s header.%s\n",
					config_filename(iter),
					config_markerline(iter),
					config_markercolumn(iter),
					header, msg);
				continue;
			}
			fprintf(stderr,
"Error parsing config file %s, line %u, column %u:\n"
"Unknown flag in %s header.%s\n",
					config_filename(iter),
					config_markerline(iter),
					config_markercolumn(iter),
					header, msg);
		}
		if (RET_WAS_ERROR(r))
			return r;
		assert (option >= 0);
		flags[option] = true;
		result = RET_OK;
		option = -1;
	}
	return result;
}

retvalue config_getall(struct configiterator *iter, char **result_p) {
	size_t size = 0, len = 0;
	char *value = NULL, *nv;
	int c;

	c = config_nextnonspace(iter);
	if (c == EOF)
		return RET_NOTHING;
	iter->markerline = iter->line;
	iter->markercolumn = iter->column;
	do {
		if (len + 2 >= size) {
			nv = realloc(value, size+128);
			if (FAILEDTOALLOC(nv)) {
				free(value);
				return RET_ERROR_OOM;
			}
			size += 128;
			value = nv;
		}
		value[len] = c;
		len++;
		if (iter->eol) {
			if (!config_nextline(iter))
				break;
		}
		c = config_nextchar(iter);
	} while (true);
	assert (len > 0);
	assert (len < size);
	while (len > 0 && (value[len-1] == ' ' || value[len-1] == '\t' ||
			    value[len-1] == '\n' || value[len-1] == '\r'))
		len--;
	value[len] = '\0';
	nv = realloc(value, len+1);
	if (nv == NULL)
		*result_p = value;
	else
		*result_p = nv;
	return RET_OK;
}

retvalue config_gettruth(struct configiterator *iter, const char *header, bool *result_p) {
	char *value = NULL;
	retvalue r;

	/* wastefull, but does not happen that often */

	r = config_getword(iter, &value);
	if (r == RET_NOTHING) {
		configparser_errorlast(iter,
"Unexpected empty boolean %s header (something like Yes or No expected).", header);
		return RET_ERROR;
	}
	if (RET_WAS_ERROR(r))
		return r;
	// TODO: check against trailing garbage
	if (strcasecmp(value, "Yes") == 0) {
		*result_p = true;
		free(value);
		return RET_OK;
	}
	if (strcasecmp(value, "No") == 0) {
		*result_p = false;
		free(value);
		return RET_OK;
	}
	if (strcmp(value, "1") == 0) {
		*result_p = true;
		free(value);
		return RET_OK;
	}
	if (strcmp(value, "0") == 0) {
		*result_p = false;
		free(value);
		return RET_OK;
	}
	configparser_errorlast(iter,
"Unexpected value in boolean %s header (something like Yes or No expected).", header);
	free(value);
	return RET_ERROR;
}

retvalue config_getnumber(struct configiterator *iter, const char *name, long long *result_p, long long minval, long long maxval) {
	char *word = NULL;
	retvalue r;
	long long value;
	char *e;

	r = config_getword(iter, &word);
	if (r == RET_NOTHING) {
		configparser_errorlast(iter,
"Unexpected end of line (%s number expected).", name);
		return RET_ERROR;
	}
	if (RET_WAS_ERROR(r))
		return r;

	value = strtoll(word, &e, 10);
	if (e == word) {
		fprintf(stderr,
"Error parsing config file %s, line %u, column %u:\n"
"Expected %s number but got '%s'\n",
			config_filename(iter), config_markerline(iter),
			config_markercolumn(iter), name, word);
		free(word);
		return RET_ERROR;
	}
	if (e != NULL && *e != '\0') {
		unsigned char digit1, digit2, digit3;
		digit1 = ((unsigned char)(*e))&0x7;
		digit2 = (((unsigned char)(*e)) >> 3)&0x7;
		digit3 = (((unsigned char)(*e)) >> 6)&0x7;
		fprintf(stderr,
"Error parsing config file %s, line %u, column %u:\n"
"Unexpected character \\%01hhu%01hhu%01hhu in %s number '%s'\n",
			config_filename(iter), config_markerline(iter),
			config_markercolumn(iter) + (int)(e-word),
			digit3, digit2, digit1,
			name, word);
		free(word);
		return RET_ERROR;
	}
	if (value == LLONG_MAX || value > maxval) {
		fprintf(stderr,
"Error parsing config file %s, line %u, column %u:\n"
"Too large %s number '%s'\n",
			config_filename(iter), config_markerline(iter),
			config_markercolumn(iter), name, word);
		free(word);
		return RET_ERROR;
	}
	if (value == LLONG_MIN || value < minval) {
		fprintf(stderr,
"Error parsing config file %s, line %u, column %u:\n"
"Too small %s number '%s'\n",
			config_filename(iter), config_markerline(iter),
			config_markercolumn(iter), name, word);
		free(word);
		return RET_ERROR;
	}
	free(word);
	*result_p = value;
	return RET_OK;
}

static retvalue config_getline(struct configiterator *iter, /*@out@*/char **result_p) {
	size_t size = 0, len = 0;
	char *value = NULL, *nv;
	int c;

	c = config_nextnonspace(iter);
	if (c == EOF)
		return RET_NOTHING;
	iter->markerline = iter->line;
	iter->markercolumn = iter->column;
	do {
		if (len + 2 >= size) {
			nv = realloc(value, size+128);
			if (FAILEDTOALLOC(nv)) {
				free(value);
				return RET_ERROR_OOM;
			}
			size += 128;
			value = nv;
		}
		value[len] = c;
		len++;
		c = config_nextchar(iter);
	} while (c != '\n');
	assert (len > 0);
	assert (len < size);
	while (len > 0 && (value[len-1] == ' ' || value[len-1] == '\t'
			 || value[len-1] == '\r'))
		len--;
	assert (len > 0);
	value[len] = '\0';
	nv = realloc(value, len+1);
	if (nv == NULL)
		*result_p = value;
	else
		*result_p = nv;
	return RET_OK;
}

retvalue config_getlines(struct configiterator *iter, struct strlist *result) {
	char *line;
	struct strlist list;
	retvalue r;

	strlist_init(&list);
	do {
		r = config_getline(iter, &line);
		if (RET_WAS_ERROR(r)) {
			strlist_done(&list);
			return r;
		}
		if (r == RET_NOTHING)
			r = strlist_add_dup(&list, "");
		else
			r = strlist_add(&list, line);
		if (RET_WAS_ERROR(r)) {
			strlist_done(&list);
			return r;
		}
	} while (config_nextline(iter));
	strlist_move(result, &list);
	return RET_OK;
}
