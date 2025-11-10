// SPDX-License-Identifier: GPL-2.0-or-later

#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "helpers.h"
#include "request.h"

const char *req_get_val(struct request *req, const char *key)
{
	struct ipc_pair *p = req_data(req);

	for (size_t i = 0; i < p->num_kv; i++) {
		if (streq(p->kv[i].key, key))
			return p->kv[i].val;
	}
	return NULL;
}

int req_get_int(struct request *req, const char *key, int def)
{
	const char *v = req_get_val(req, key);
	return v ? atoi(v) : def;
}

bool req_get_bool(struct request *req, const char *key, bool def)
{
	const char *v = req_get_val(req, key);
	if (v)
		return streq(v, "1") || strcaseeq(v, "true") || strcaseeq(v, "yes");
	return def;
}
