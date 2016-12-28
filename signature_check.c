/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006,2007,2009,2012 Bernhard R. Link
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
#include <ctype.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include "signature_p.h"
#include "ignore.h"
#include "chunks.h"
#include "readtextfile.h"


#ifdef HAVE_LIBGPGME

static retvalue parse_condition_part(bool *allow_subkeys_p, bool *allow_bad_p, const char *full_condition, const char **condition_p, /*@out@*/ char **next_key_p) {
	const char *key = *condition_p, *p;
	char *next_key, *q;
	size_t kl;

	*allow_bad_p = false;
	*allow_subkeys_p = false;

	while (*key != '\0' && xisspace(*key))
		key++;
	if (*key == '\0') {
		fprintf(stderr,
"Error: unexpected end of VerifyRelease condition '%s'!\n",
				full_condition);
		return RET_ERROR;
	}

	p = key;
	while ((*p >= 'A' && *p <= 'F') || (*p >= 'a' && *p <= 'f')
			|| (*p >= '0' && *p <= '9'))
		p++;
	if (*p != '\0' && !xisspace(*p) && *p != '|' && *p != '!' && *p != '+') {
		fprintf(stderr,
"Error: Unexpected character 0x%02hhx='%c' in VerifyRelease condition '%s'!\n",
				*p, *p, full_condition);
		return RET_ERROR;
	}
	kl = p - key;
	if (kl < 8) {
		fprintf(stderr,
"Error: Too short key id '%.*s' in VerifyRelease condition '%s'!\n",
				(int)kl, key, full_condition);
		return RET_ERROR;
	}
	next_key = strndup(key, kl);
	if (FAILEDTOALLOC(next_key))
		return RET_ERROR_OOM;
	key = p;
	for (q = next_key ; *q != '\0' ; q++) {
		if (*q >= 'a' && *q <= 'f')
			*q -= 'a' - 'A';
	}
	while (*key != '\0' && xisspace(*key))
		key++;
	if (*key == '!') {
		*allow_bad_p = true;
		key++;
	}
	while (*key != '\0' && xisspace(*key))
		key++;
	if (*key == '+') {
		*allow_subkeys_p = true;
		key++;
	}
	while (*key != '\0' && xisspace(*key))
		key++;
	if ((*key >= 'A' && *key <= 'F')
			|| (*key >= 'a' && *key <= 'f')
			|| (*key >= '0' && *key <= '9')) {
		free(next_key);
		fprintf(stderr,
"Error: Space separated key-ids in VerifyRelease condition '%s'!\n"
"(Alternate keys can be separated with '|'. Do not put spaces in key-ids.)\n",
				full_condition);
		return RET_ERROR;
	}
	if (*key != '\0' && *key != '|') {
		free(next_key);
		fprintf(stderr,
"Error: Unexpected character 0x%02hhx='%c' in VerifyRelease condition '%s'!\n",
				*key, *key, full_condition);
		return RET_ERROR;
	}
	if (*key == '|')
		key++;
	*next_key_p = next_key;
	*condition_p = key;
	return RET_OK;
}

static struct known_key {
	struct known_key *next;
	/* subkeys, first is primary key */
	int count;
	struct known_subkey {
		/* full fingerprint or keyid */
		char *name;
		unsigned int name_len;
		/* true if revoked */
		bool revoked;
		/* true if expired */
		bool expired;
		/* false if invalid or cannot sign */
		bool cansign;
	} subkeys[];
} *known_keys = NULL;

struct requested_key {
	/* pointer to the key in question */
	const struct known_key *key;
	/* which of those keys are requested, -1 for any (i.e. allow subkeys) */
	int subkey;
	/* allow some problems, if requested by the user */
	bool allow_bad;
};

static retvalue found_key(struct known_key *k, int i, bool allow_subkeys, bool allow_bad, const char *full_condition, const struct known_key **key_found, int *index_found) {
	if (!allow_bad && k->subkeys[i].revoked) {
		fprintf(stderr,
"VerifyRelease condition '%s' lists revoked key '%s'.\n"
"(To use it anyway, append it with a '!' to force usage).\n",
			full_condition, k->subkeys[i].name);
		return RET_ERROR;
	}
	if (!allow_bad && k->subkeys[i].expired) {
		fprintf(stderr,
"VerifyRelease condition '%s' lists expired key '%s'.\n"
"(To use it anyway, append it with a '!' to force usage).\n",
			full_condition, k->subkeys[i].name);
		return RET_ERROR;
	}
	if (!allow_bad && !k->subkeys[i].cansign) {
		fprintf(stderr,
"VerifyRelease condition '%s' lists non-signing key '%s'.\n"
"(To use it anyway, append it with a '!' to force usage).\n",
			full_condition, k->subkeys[i].name);
		return RET_ERROR;
	}
	if (allow_subkeys) {
		if (i != 0) {
			fprintf(stderr,
"VerifyRelease condition '%s' lists non-primary key '%s' with '+'.\n",
			full_condition, k->subkeys[i].name);
			return RET_ERROR;
		}
		*index_found = -1;
	} else
		*index_found = i;
	*key_found = k;
	return RET_OK;
}

/* name must already be upper-case */
static retvalue load_key(const char *name, bool allow_subkeys, bool allow_bad, const char *full_condition, const struct known_key **key_found, int *index_found) {
	gpg_error_t err;
	gpgme_key_t gpgme_key = NULL;
	gpgme_subkey_t subkey;
	int found = -1;
	struct known_key *k;
	int i;
	size_t l = strlen(name);

	/* first look if this key was already retrieved: */
	for (k = known_keys ; k != NULL ; k = k->next) {
		for(i = 0 ; i < k->count ; i++) {
			struct known_subkey *s = &k->subkeys[i];

			if (s->name_len < l)
				continue;
			if (memcmp(name, s->name + (s->name_len - l), l) != 0)
				continue;
			return found_key(k, i, allow_subkeys, allow_bad,
					full_condition,
					key_found, index_found);
		}
	}
	/* If not yet found, request it: */
	err = gpgme_get_key(context, name, &gpgme_key, 0);
	if ((gpg_err_code(err) == GPG_ERR_EOF) && gpgme_key == NULL) {
		fprintf(stderr, "Error: unknown key '%s'!\n", name);
		return RET_ERROR_MISSING;
	}
	if (err != 0) {
		fprintf(stderr, "gpgme error %s:%d retrieving key '%s': %s\n",
				gpg_strsource(err), (int)gpg_err_code(err),
				name, gpg_strerror(err));
		if (gpg_err_code(err) == GPG_ERR_ENOMEM)
			return RET_ERROR_OOM;
		else
			return RET_ERROR_GPGME;
	}
	i = 0;
	subkey = gpgme_key->subkeys;
	while (subkey != NULL) {
		subkey = subkey->next;
		i++;
	}
	k = calloc(1, sizeof(struct known_key)
			+ i * sizeof(struct known_subkey));
	if (FAILEDTOALLOC(k)) {
		gpgme_key_unref(gpgme_key);
		return RET_ERROR_OOM;
	}
	k->count = i;
	k->next = known_keys;
	known_keys = k;

	subkey = gpgme_key->subkeys;
	for (i = 0 ; i < k->count ; i++ , subkey = subkey->next) {
		struct known_subkey *s = &k->subkeys[i];

		assert (subkey != NULL);

		s->revoked = subkey->revoked;
		s->expired = subkey->expired;
		s->cansign = subkey->can_sign && !subkey->invalid;
		s->name = strdup(subkey->keyid);
		if (FAILEDTOALLOC(s->name)) {
			gpgme_key_unref(gpgme_key);
			return RET_ERROR_OOM;
		}
		for (char *p = s->name ; *p != '\0' ; p++) {
			if (*p >= 'a' && *p <= 'z')
				*p -= 'a'-'A';
		}
		s->name_len = strlen(s->name);
		if (memcmp(name, s->name + (s->name_len - l), l) == 0)
			found = i;
	}
	assert (subkey == NULL);
	gpgme_key_unref(gpgme_key);
	if (found < 0) {
		fprintf(stderr, "Error: not a valid key id '%s'!\n"
"Use hex-igits from the end of the key as identifier\n", name);
		return RET_ERROR;
	}
	return found_key(k, found, allow_subkeys, allow_bad,
			full_condition, key_found, index_found);
}

static void free_known_key(/*@only@*/struct known_key *k) {
	int i;

	for (i = 0 ; i < k->count ; i++) {
		free(k->subkeys[i].name);
	}
	free(k);
}

void free_known_keys(void) {
	while (known_keys != NULL) {
		struct known_key *k = known_keys;
		known_keys = k->next;
		free_known_key(k);
	}
	known_keys = NULL;
}

/* This checks a Release.gpg/Release file pair. requirements is a list of
 * requirements. (as this Release file can be requested by multiple update
 * rules, there can be multiple requirements for one file) */

struct signature_requirement {
	/* next condition */
	struct signature_requirement *next;
	/* the original description for error messages */
	char *condition;
	/* an array of or-connected conditions */
	size_t num_keys;
	struct requested_key keys[];
};
#define sizeof_requirement(n) (sizeof(struct signature_requirement) + (n) * sizeof(struct requested_key))

void signature_requirements_free(struct signature_requirement *list) {
	while (list != NULL) {
		struct signature_requirement *p = list;
		list = p->next;

		free(p->condition);
		free(p);
	}
}

static bool key_good(const struct requested_key *req, const gpgme_signature_t signatures) {
	const struct known_key *k = req->key;
	gpgme_signature_t sig;

	for (sig = signatures ; sig != NULL ; sig = sig->next) {
		const char *fpr = sig->fpr;
		size_t l = strlen(sig->fpr);
		int i;
		/* while gpg reports the subkey of an key that is expired
		   to be expired to, it does not tell this in the signature,
		   so we use this here... */
		bool key_expired = false;

		if (req->subkey < 0) {
			/* any subkey is allowed */
			for(i = 0 ; i < k->count ; i++) {
				const struct known_subkey *s = &k->subkeys[i];

				if (s->name_len > l)
					continue;
				if (memcmp(s->name, fpr + (l - s->name_len),
							s->name_len) != 0)
					continue;
				key_expired = k->subkeys[i].expired;
				break;
			}
			if (i >= k->count)
				continue;
		} else {
			const struct known_subkey *s;

			assert (req->subkey < k->count);
			s = &k->subkeys[req->subkey];
			if (memcmp(s->name, fpr + (l - s->name_len),
						s->name_len) != 0)
				continue;
			key_expired = k->subkeys[req->subkey].expired;
		}
		/* only accept perfectly good signatures and silently
		   ignore everything else. Those are warned about or
		   even accepted in the run with key_good_enough */
		if (gpg_err_code(sig->status) == GPG_ERR_NO_ERROR
				&& !key_expired)
			return true;
		/* we have to continue otherwise,
		   as another subkey might still follow */
		continue;
	}
	/* no valid signature with this key found */
	return false;
}

static bool key_good_enough(const struct requested_key *req, const gpgme_signature_t signatures, const char *releasegpg, const char *release) {
	const struct known_key *k = req->key;
	gpgme_signature_t sig;

	for (sig = signatures ; sig != NULL ; sig = sig->next) {
		const char *fpr = sig->fpr;
		size_t l = strlen(sig->fpr);
		int i;
		bool key_expired = false; /* dito */

		if (req->subkey < 0) {
			/* any subkey is allowed */
			for(i = 0 ; i < k->count ; i++) {
				const struct known_subkey *s = &k->subkeys[i];

				if (s->name_len > l)
					continue;
				if (memcmp(s->name, fpr + (l - s->name_len),
							s->name_len) != 0)
					continue;
				key_expired = k->subkeys[i].expired;
				break;
			}
			if (i >= k->count)
				continue;
		} else {
			const struct known_subkey *s;

			assert (req->subkey < k->count);
			s = &k->subkeys[req->subkey];
			if (memcmp(s->name, fpr + (l - s->name_len),
						s->name_len) != 0)
				continue;
			key_expired = k->subkeys[req->subkey].expired;
		}
		/* this key we look for. if it is acceptable, we are finished.
		   if it is not acceptable, we still have to look at the other
		   signatures, as a signature with another subkey is following
		 */
		switch (gpg_err_code(sig->status)) {
			case GPG_ERR_NO_ERROR:
				if (! key_expired)
					return true;
				if (req->allow_bad && IGNORABLE(expiredkey)) {
					if (verbose >= 0)
						fprintf(stderr,
"WARNING: valid signature in '%s' with parent-expired '%s' is accepted as requested!\n",
							releasegpg, fpr);
					return true;
				}
				fprintf(stderr,
"Not accepting valid signature in '%s' with parent-EXPIRED '%s'\n", releasegpg, fpr);
				if (verbose >= 0)
					fprintf(stderr,
"(To ignore it append a ! to the key and run reprepro with --ignore=expiredkey)\n");
				/* not accepted */
				continue;
			case GPG_ERR_KEY_EXPIRED:
				if (req->allow_bad && IGNORABLE(expiredkey)) {
					if (verbose >= 0)
						fprintf(stderr,
"WARNING: valid signature in '%s' with expired '%s' is accepted as requested!\n",
							releasegpg, fpr);
					return true;
				}
				fprintf(stderr,
"Not accepting valid signature in '%s' with EXPIRED '%s'\n", releasegpg, fpr);
				if (verbose >= 0)
					fprintf(stderr,
"(To ignore it append a ! to the key and run reprepro with --ignore=expiredkey)\n");
				/* not accepted */
				continue;
			case GPG_ERR_CERT_REVOKED:
				if (req->allow_bad && IGNORABLE(revokedkey)) {
					if (verbose >= 0)
						fprintf(stderr,
"WARNING: valid signature in '%s' with revoked '%s' is accepted as requested!\n",
							releasegpg, fpr);
					return RET_OK;
				}
				fprintf(stderr,
"Not accepting valid signature in '%s' with REVOKED '%s'\n", releasegpg, fpr);
				if (verbose >= 0)
					fprintf(stderr,
"(To ignore it append a ! to the key and run reprepro with --ignore=revokedkey)\n");
				/* not accepted */
				continue;
			case GPG_ERR_SIG_EXPIRED:
				if (req->allow_bad && IGNORABLE(expiredsignature)) {
					if (verbose >= 0)
						fprintf(stderr,
"WARNING: valid but expired signature in '%s' with '%s' is accepted as requested!\n",
							releasegpg, fpr);
					return RET_OK;
				}
				fprintf(stderr,
"Not accepting valid but EXPIRED signature in '%s' with '%s'\n", releasegpg, fpr);
				if (verbose >= 0)
					fprintf(stderr,
"(To ignore it append a ! to the key and run reprepro with --ignore=expiredsignature)\n");
				/* not accepted */
				continue;
			case GPG_ERR_BAD_SIGNATURE:
			case GPG_ERR_NO_PUBKEY:
				/* not accepted */
				continue;
			case GPG_ERR_GENERAL:
				if (release == NULL)
					fprintf(stderr,
"gpgme returned an general error verifing signature with '%s' in '%s'!\n"
"Try running gpg --verify '%s' manually for hints what is happening.\n"
"If this does not print any errors, retry the command causing this message.\n",
						fpr, releasegpg,
						releasegpg);
				else
					fprintf(stderr,
"gpgme returned an general error verifing signature with '%s' in '%s'!\n"
"Try running gpg --verify '%s' '%s' manually for hints what is happening.\n"
"If this does not print any errors, retry the command causing this message.\n",
						fpr, releasegpg,
						releasegpg, release);
				continue;
			/* there sadly no more is a way to make sure we have
			 * all possible ones handled */
			default:
				break;
		}
		fprintf(stderr,
"Error checking signature (gpgme returned unexpected value %d)!\n"
"Please file a bug report, so reprepro can handle this in the future.\n",
			gpg_err_code(sig->status));
		return false;
	}
	return false;
}

retvalue signature_requirement_add(struct signature_requirement **list_p, const char *condition) {
	struct signature_requirement *req;
	const char *full_condition = condition;
	retvalue r;

	r = signature_init(false);
	if (RET_WAS_ERROR(r))
		return r;

	if (condition == NULL || strcmp(condition, "blindtrust") == 0)
		return RET_NOTHING;

	/* no need to add the same condition multiple times */
	for (req = *list_p ; req != NULL ; req = req->next) {
		if (strcmp(req->condition, condition) == 0)
			return RET_NOTHING;
	}

	req = malloc(sizeof_requirement(1));
	if (FAILEDTOALLOC(req))
		return RET_ERROR_OOM;
	req->next = NULL;
	req->condition = strdup(condition);
	if (FAILEDTOALLOC(req->condition)) {
		free(req);
		return RET_ERROR_OOM;
	}
	req->num_keys = 0;
	do {
		bool allow_subkeys, allow_bad;
		char *next_key;

		r = parse_condition_part(&allow_subkeys, &allow_bad,
				full_condition, &condition, &next_key);
		ASSERT_NOT_NOTHING(r);
		if (RET_WAS_ERROR(r)) {
			signature_requirements_free(req);
			return r;
		}
		req->keys[req->num_keys].allow_bad = allow_bad;
		r = load_key(next_key, allow_subkeys, allow_bad,
				full_condition,
				&req->keys[req->num_keys].key,
				&req->keys[req->num_keys].subkey);
		free(next_key);
		if (RET_WAS_ERROR(r)) {
			signature_requirements_free(req);
			return r;
		}
		req->num_keys++;

		if (*condition != '\0') {
			struct signature_requirement *h;

			h = realloc(req, sizeof_requirement(req->num_keys+1));
			if (FAILEDTOALLOC(h)) {
				signature_requirements_free(req);
				return r;
			}
			req = h;
		} else
			break;
	} while (true);
	req->next = *list_p;
	*list_p = req;
	return RET_OK;
}

static void print_signatures(FILE *f, gpgme_signature_t s, const char *releasegpg) {
	char timebuffer[20];
	struct tm *tm;
	time_t t;

	if (s == NULL) {
		fprintf(f, "gpgme reported no signatures in '%s':\n"
"Either there are really none or something else is strange.\n"
"One known reason for this effect is forgeting -b when signing.\n",
				releasegpg);
		return;
	}

	fprintf(f, "Signatures in '%s':\n", releasegpg);
	for (; s != NULL ; s = s->next) {
		t = s->timestamp; tm = localtime(&t);
		strftime(timebuffer, 19, "%Y-%m-%d", tm);
		fprintf(f, "'%s' (signed %s): ", s->fpr, timebuffer);
		switch (gpg_err_code(s->status)) {
			case GPG_ERR_NO_ERROR:
				fprintf(f, "valid\n");
				continue;
			case GPG_ERR_KEY_EXPIRED:
				fprintf(f, "expired key\n");
				continue;
			case GPG_ERR_CERT_REVOKED:
				fprintf(f, "key revoced\n");
				continue;
			case GPG_ERR_SIG_EXPIRED:
				t = s->exp_timestamp; tm = localtime(&t);
				strftime(timebuffer, 19, "%Y-%m-%d", tm);
				fprintf(f, "expired signature (since %s)\n",
						timebuffer);
				continue;
			case GPG_ERR_BAD_SIGNATURE:
				fprintf(f, "bad signature\n");
				continue;
			case GPG_ERR_NO_PUBKEY:
				fprintf(f, "missing pubkey\n");
				continue;
			default:
				fprintf(f, "unknown\n");
				continue;
		}
	}
}

static inline retvalue verify_signature(const struct signature_requirement *requirements, const char *releasegpg, const char *releasename) {
	gpgme_verify_result_t result;
	int i;
	const struct signature_requirement *req;

	result = gpgme_op_verify_result(context);
	if (result == NULL) {
		fprintf(stderr,
"Internal error communicating with libgpgme: no result record!\n\n");
		return RET_ERROR_GPGME;
	}

	for (req = requirements ; req != NULL ; req = req->next) {
		bool fulfilled = false;

		/* check first for good signatures, and then for good enough
		   signatures, to not pester the user with warnings of one
		   of the alternate keys, if the last one is good enough */

		for (i = 0 ; (size_t)i < req->num_keys ; i++) {

			if (key_good(&req->keys[i], result->signatures)) {
				fulfilled = true;
				break;
			}
		}
		for (i = 0 ; !fulfilled && (size_t)i < req->num_keys ; i++) {

			if (key_good_enough(&req->keys[i], result->signatures,
						releasegpg, releasename)) {
				fulfilled = true;
				break;
			}
		}
		if (!fulfilled) {
			fprintf(stderr,
"ERROR: Condition '%s' not fulfilled for '%s'.\n",
					req->condition, releasegpg);
			print_signatures(stderr, result->signatures,
					releasegpg);
			return RET_ERROR_BADSIG;
		}
		if (verbose > 10) {
			fprintf(stdout, "Condition '%s' fulfilled for '%s'.\n",
					req->condition, releasegpg);
		}
	}
	if (verbose > 20)
		print_signatures(stdout, result->signatures, releasegpg);
	return RET_OK;
}

retvalue signature_check(const struct signature_requirement *requirements, const char *releasegpg, const char *releasename, const char *releasedata, size_t releaselen) {
	gpg_error_t err;
	int gpgfd;
	gpgme_data_t dh, dh_gpg;

	assert (requirements != NULL);

	if (FAILEDTOALLOC(releasedata) || FAILEDTOALLOC(releasegpg))
		return RET_ERROR_OOM;

	assert (context != NULL);

	/* Read the file and its signature into memory: */
	gpgfd = open(releasegpg, O_RDONLY|O_NOCTTY);
	if (gpgfd < 0) {
		int e = errno;
		fprintf(stderr, "Error opening '%s': %s\n",
				releasegpg, strerror(e));
		return RET_ERRNO(e);
	}
	err = gpgme_data_new_from_fd(&dh_gpg, gpgfd);
	if (err != 0) {
		(void)close(gpgfd);
		fprintf(stderr, "Error reading '%s':\n", releasegpg);
		return gpgerror(err);
	}
	err = gpgme_data_new_from_mem(&dh, releasedata, releaselen, 0);
	if (err != 0) {
		gpgme_data_release(dh_gpg);
		return gpgerror(err);
	}

	/* Verify the signature */

	err = gpgme_op_verify(context, dh_gpg, dh, NULL);
	gpgme_data_release(dh_gpg);
	gpgme_data_release(dh);
	close(gpgfd);
	if (err != 0) {
		fprintf(stderr, "Error verifying '%s':\n", releasegpg);
		return gpgerror(err);
	}

	return verify_signature(requirements, releasegpg, releasename);
}

retvalue signature_check_inline(const struct signature_requirement *requirements, const char *filename, char **chunk_p) {
	gpg_error_t err;
	gpgme_data_t dh, dh_gpg;
	int fd;

	fd = open(filename, O_RDONLY|O_NOCTTY);
	if (fd < 0) {
		int e = errno;
		fprintf(stderr, "Error opening '%s': %s\n",
				filename, strerror(e));
		return RET_ERRNO(e);
	}
	err = gpgme_data_new_from_fd(&dh_gpg, fd);
	if (err != 0) {
		(void)close(fd);
		return gpgerror(err);
	}

	err = gpgme_data_new(&dh);
	if (err != 0) {
		(void)close(fd);
		gpgme_data_release(dh_gpg);
		return gpgerror(err);
	}
	err = gpgme_op_verify(context, dh_gpg, NULL, dh);
	(void)close(fd);
	if (gpg_err_code(err) == GPG_ERR_NO_DATA) {
		char *chunk; const char *n;
		size_t len;
		retvalue r;

		gpgme_data_release(dh);
		gpgme_data_release(dh_gpg);

		r = readtextfile(filename, filename, &chunk, &len);
		assert (r != RET_NOTHING);
		if (RET_WAS_ERROR(r))
			return r;

		assert (chunk[len] == '\0');
		len = chunk_extract(chunk, chunk, len, false, &n);
		if (chunk[0] == '-' || *n != '\0') {
			fprintf(stderr,
"Cannot parse '%s': found no signature but does not looks safe to be assumed unsigned, either.\n",
				filename);
			free(chunk);
			return RET_ERROR;
		}
		if (requirements != NULL) {
			free(chunk);
			return RET_ERROR_BADSIG;
		}
		fprintf(stderr,
"WARNING: No signature found in %s, assuming it is unsigned!\n",
				filename);
		assert (chunk[len] == '\0');
		*chunk_p = realloc(chunk, len+1);
		if (FAILEDTOALLOC(*chunk_p))
			*chunk_p = chunk;
		return RET_OK;
	} else {
		char *plain_data, *chunk;
		const char *n;
		size_t plain_len, len;
		retvalue r;

		if (err != 0) {
			gpgme_data_release(dh_gpg);
			gpgme_data_release(dh);
			return gpgerror(err);
		}
		gpgme_data_release(dh_gpg);
		plain_data = gpgme_data_release_and_get_mem(dh, &plain_len);
		if (plain_data == NULL) {
			fprintf(stderr,
"Error: libgpgme failed to extract the plain data out of\n"
"'%s'.\n"
"While it did so in a way indicating running out of memory, experience says\n"
"this also happens when gpg returns a error code it does not understand.\n"
"To check this please try running gpg --verify '%s' manually.\n"
"Continuing extracting it ignoring all signatures...",
					filename, filename);
			return RET_ERROR;
		}
		chunk = malloc(plain_len+1);
		if (FAILEDTOALLOC(chunk))
			return RET_ERROR_OOM;
		len = chunk_extract(chunk, plain_data, plain_len, false, &n);
#ifdef HAVE_GPGPME_FREE
		gpgme_free(plain_data);
#else
		free(plain_data);
#endif
		assert (len <= plain_len);
		if (plain_len != (size_t)(n - plain_data)) {
			fprintf(stderr,
"Cannot parse '%s': extraced signed data looks malformed.\n",
				filename);
			r = RET_ERROR;
		} else
			r = verify_signature(requirements, filename, NULL);
		if (RET_IS_OK(r)) {
			*chunk_p = realloc(chunk, len+1);
			if (FAILEDTOALLOC(*chunk_p))
				*chunk_p = chunk;
		} else
			free(chunk);
		return r;
	}
}
#else /* HAVE_LIBGPGME */

retvalue signature_check(const struct signature_requirement *requirements, const char *releasegpg, const char *releasename, const char *releasedata, size_t releaselen) {
	assert (requirements != NULL);

	if (FAILEDTOALLOC(releasedata) || FAILEDTOALLOC(releasegpg))
		return RET_ERROR_OOM;
	fprintf(stderr,
"ERROR: Cannot check signatures as this reprepro binary is compiled with support\n"
"for libgpgme.\n"); // TODO: "Only running external programs is supported.\n"
	return RET_ERROR_GPGME;
}

retvalue signature_check_inline(const struct signature_requirement *requirements, const char *filename, char **chunk_p) {
	retvalue r;
	char *chunk; size_t len;
	const char *n;

	if (requirements != NULL) {
		fprintf(stderr,
"ERROR: Cannot check signatures as this reprepro binary is compiled with support\n"
"for libgpgme.\n");
		return RET_ERROR_GPGME;
	}
	r = readtextfile(filename, filename, &chunk, &len);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r))
		return r;
	assert (chunk[len] == '\0');

	len = chunk_extract(chunk, chunk, len, false, &n);
	if (len == 0) {
		fprintf(stderr, "Could not find any data within '%s'!\n",
				filename);
		free(chunk);
		return RET_ERROR;
	}
	if (chunk[0] == '-') {
		const char *endmarker;

		if (len < 10 || memcmp(chunk, "-----BEGIN", 10) != 0) {
			fprintf(stderr,
"Strange content of '%s': First non-space character is '-',\n"
"but it does not begin with '-----BEGIN'.\n", filename);
			free(chunk);
			return RET_ERROR;
		}
		len = chunk_extract(chunk, n, strlen(n), false, &n);

		endmarker = strstr(chunk, "\n-----");
		if (endmarker != NULL) {
			endmarker++;
			assert ((size_t)(endmarker-chunk) < len);
			len = endmarker-chunk;
			chunk[len] = '\0';
		} else if (*n == '\0') {
			fprintf(stderr,
"ERROR: Could not find end marker of signed data within '%s'.\n"
"Cannot determine what is data and what is not!\n",
						filename);
				free(chunk);
				return RET_ERROR;
		} else if (strncmp(n, "-----", 5) != 0) {
			fprintf(stderr,
"ERROR: Spurious empty line within '%s'.\n"
"Cannot determine what is data and what is not!\n",
					filename);
			free(chunk);
			return RET_ERROR;
		}
	} else {
		if (*n != '\0') {
			fprintf(stderr,
"Cannot parse '%s': found no signature but does not looks safe to be assumed unsigned, either.\n",
					filename);
			return RET_ERROR;
		}
		fprintf(stderr,
"WARNING: No signature found in %s, assuming it is unsigned!\n",
				filename);
	}
	assert (chunk[len] == '\0');
	*chunk_p = realloc(chunk, len+1);
	if (FAILEDTOALLOC(*chunk_p))
		*chunk_p = chunk;
	return RET_OK;
}

void signature_requirements_free(/*@only@*/struct signature_requirement *p) {
	free(p);
}

retvalue signature_requirement_add(UNUSED(struct signature_requirement **x), const char *condition) {
	if (condition == NULL || strcmp(condition, "blindtrust") == 0)
		return RET_NOTHING;

	fprintf(stderr,
"ERROR: Cannot check signatures as this reprepro binary is compiled with support\n"
"for libgpgme.\n"); // TODO: "Only running external programs is supported.\n"
	return RET_ERROR_GPGME;
}

void free_known_keys(void) {
}

#endif /* HAVE_LIBGPGME */
