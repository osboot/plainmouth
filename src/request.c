// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <err.h>
#include <stdarg.h>
#include <stdio.h>

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

bool req_error(struct request *req, const char *format, ...)
{
	char *message = NULL;
	va_list args;

	va_start(args, format);
	int rc = vasprintf(&message, format, args);
	va_end(args);
	ipc_send_string(req_fd(req), "RESPDATA %s ERR=%s", req_id(req),
			rc < 0 ? "no memory" : message);
	free(message);
	return false;
}

bool req_validate_parameters(struct request *req, const struct req_parameter *parameters,
			     const char *unknown_error, const char *duplicate_error)
{
	struct ipc_pair *pairs = req_data(req);

	for (size_t i = 0; i < pairs->num_kv; i++) {
		const char *key = pairs->kv[i].key;
		const struct req_parameter *parameter = parameters;

		while (parameter->name && !streq(parameter->name, key))
			parameter++;

		if (!parameter->name)
			return req_error(req, "%s: %s", unknown_error, key);

		if (parameter->repeatable)
			continue;

		for (size_t j = 0; j < i; j++) {
			if (streq(key, pairs->kv[j].key))
				return req_error(req, "%s: %s", duplicate_error, key);
		}
	}

	return true;
}

bool req_read_int(struct request *req, const char *key, int *value)
{
	const char *text = req_get_val(req, key);
	char *end;

	if (!text)
		return req_error(req, "field is missing: %s", key);

	errno = 0;
	long number = strtol(text, &end, 10);
	if (errno || end == text || *end || number < INT_MIN || number > INT_MAX)
		return req_error(req, "invalid value: %s", key);

	*value = (int) number;
	return true;
}

static bool req_parse_bool(struct request *req, const char *key, const char *text, bool *value)
{
	if (streq(text, "1") || strcaseeq(text, "true") || strcaseeq(text, "yes"))
		*value = true;
	else if (streq(text, "0") || strcaseeq(text, "false") || strcaseeq(text, "no"))
		*value = false;
	else
		return req_error(req, "invalid value: %s", key);
	return true;
}

bool req_read_bool(struct request *req, const char *key, bool def, bool *value)
{
	const char *text = req_get_val(req, key);
	if (!text) {
		*value = def;
		return true;
	}
	return req_parse_bool(req, key, text, value);
}

bool req_read_kv_bool(struct request *req, const struct ipc_kv *kv, bool *value)
{
	return req_parse_bool(req, kv->key, kv->val, value);
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
	char *end = NULL;
	unsigned long value;

	if (!v)
		return def;

	errno = 0;
	value = strtoul(v, &end, 10);
	if (errno || end == v || *end != '\0' || value > UINT32_MAX)
		return def;

	return (uint32_t) value;
}

bool req_get_bool(struct request *req, const char *key, bool def)
{
	const char *v = req_get_val(req, key);
	if (v)
		return streq(v, "1") || strcaseeq(v, "true") || strcaseeq(v, "yes");
	return def;
}
