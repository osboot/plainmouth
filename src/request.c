// SPDX-License-Identifier: GPL-2.0-or-later

#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>

#include "macros.h"
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

wchar_t *req_get_kv_wchars(struct ipc_kv *kv)
{
	size_t mbslen = mbstowcs(NULL, kv->val, 0);

	if (mbslen == (size_t) -1) {
		return NULL;
	}

	wchar_t *wcs = calloc(mbslen + 1, sizeof(*wcs));

	if (!wcs) {
		warn("calloc");
		return NULL;
	}

	mbstowcs(wcs, kv->val, mbslen + 1);
	return wcs;
}

wchar_t *req_get_wchars(struct request *req, const char *key)
{
	struct ipc_pair *p = req_data(req);

	for (size_t i = 0; i < p->num_kv; i++) {
		if (streq(p->kv[i].key, key))
			return req_get_kv_wchars(p->kv + i);
	}
	return NULL;
}

int req_get_int(struct request *req, const char *key, int def)
{
	const char *v = req_get_val(req, key);
	return v ? atoi(v) : def;
}

uint32_t req_get_uint(struct request *req, const char *key, uint32_t def)
{
	const char *v = req_get_val(req, key);
	int i = atoi(v);
	return (v && i >= 0) ? (uint32_t) i : def;
}

bool req_get_bool(struct request *req, const char *key, bool def)
{
	const char *v = req_get_val(req, key);
	if (v)
		return streq(v, "1") || strcaseeq(v, "true") || strcaseeq(v, "yes");
	return def;
}
