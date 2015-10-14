#include <config.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "packagedata.h"

// The return structure packagedata must already be allocated.
retvalue parse_packagedata(const char *data, /*@out@*/struct packagedata *packagedata) {
	assert (packagedata != NULL);

	packagedata->chunk = (char*)data;
	return RET_OK;
}
