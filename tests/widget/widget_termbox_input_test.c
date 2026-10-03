// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "plugins/termbox-input.h"

int main(void)
{
	assert(setlocale(LC_CTYPE, "C.UTF-8"));
	struct termbox_input *input = calloc(1, sizeof(*input));
	assert(input);
	input->erase = 127;
	const char text[] = "hl0\304\203\b\r";
	assert(termbox_input_append(input, text, sizeof(text) - 1));
	assert(!memcmp(input->data, "hl0\304\203\177\n", sizeof(text) - 1));
	size_t original = input->length;
	assert(!termbox_input_append(input, "prefix\033", 7));
	assert(errno == EINVAL && input->length == original);
	assert(!termbox_input_append(input, "\304", 1));
	assert(errno == EINVAL && input->length == original);
	assert(!termbox_input_append(input, "\t", 1));
	assert(errno == EINVAL && input->length == original);

	const char response[] = "\033[1;2R";
	assert(termbox_input_append_bytes(input, response, sizeof(response) - 1));
	assert(!memcmp(input->data + original, response, sizeof(response) - 1));

	char *payload = malloc(TERMBOX_INPUT_LIMIT);
	char *received = malloc(TERMBOX_INPUT_LIMIT);
	assert(payload && received);
	for (size_t i = 0; i < TERMBOX_INPUT_LIMIT; i++)
		payload[i] = (char) ('a' + i % 26);
	input->length = 0;
	assert(termbox_input_append(input, payload, TERMBOX_INPUT_LIMIT));
	assert(!termbox_input_append(input, "x", 1));
	assert(errno == ENOBUFS && input->length == TERMBOX_INPUT_LIMIT);

	int fds[2];
	assert(pipe2(fds, O_NONBLOCK | O_CLOEXEC) == 0);
	assert(fcntl(fds[1], F_SETPIPE_SZ, 4096) >= 4096);
	assert(termbox_input_flush(input, fds[1]));
	assert(input->length > 0 && input->offset > 0);
	size_t count = 0;
	while (input->length || count < TERMBOX_INPUT_LIMIT) {
		ssize_t n = read(fds[0], received + count, TERMBOX_INPUT_LIMIT - count);
		assert(n > 0);
		count += (size_t) n;
		assert(termbox_input_flush(input, fds[1]));
	}
	assert(input->offset == 0);
	assert(!memcmp(payload, received, TERMBOX_INPUT_LIMIT));

	/* Appending after a short write must preserve the pending suffix. */
	assert(termbox_input_append(input, payload, TERMBOX_INPUT_LIMIT));
	assert(termbox_input_flush(input, fds[1]));
	size_t written = input->offset;
	assert(written > 0 && input->length > 0);
	assert(termbox_input_append(input, "END", 3));
	assert(input->offset == 0);
	assert(!memcmp(input->data, payload + written, TERMBOX_INPUT_LIMIT - written));
	assert(!memcmp(input->data + input->length - 3, "END", 3));
	struct sigaction ignored = { .sa_handler = SIG_IGN };
	assert(sigemptyset(&ignored.sa_mask) == 0);
	assert(sigaction(SIGPIPE, &ignored, NULL) == 0);
	close(fds[0]);
	assert(!termbox_input_flush(input, fds[1]));
	assert(errno == EPIPE);
	close(fds[1]);
	free(received);
	free(payload);
	free(input);
	return 0;
}
