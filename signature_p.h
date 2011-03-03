#ifndef REPREPRO_SIGNATURE_P_H
#define REPREPRO_SIGNATURE_P_H

#ifdef HAVE_LIBGPGME
#include <gpg-error.h>
#include <gpgme.h>

extern gpgme_ctx_t context;
#endif

#include "globals.h"
#include "error.h"
#include "signature.h"

#ifdef HAVE_LIBGPGME
retvalue gpgerror(gpg_error_t err);
#endif
#endif
