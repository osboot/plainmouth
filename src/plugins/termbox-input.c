// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#include "termbox-input.h"

bool termbox_input_append(struct termbox_input *input, const char *text, size_t length)
{
	if (length > TERMBOX_INPUT_LIMIT - input->length) {
		errno = ENOBUFS;
		return false;
	}

	/* Validate the entire submission before modifying the pending queue. */
	mbstate_t state = { 0 };
	for (size_t i = 0; i < length;) {
		wchar_t ch;
		size_t n = mbrtowc(&ch, text + i, length - i, &state);
		if (!n || n == (size_t) -1 || n == (size_t) -2 ||
		    (!iswprint((wint_t) ch) && ch != L'\n' && ch != L'\r' && ch != L'\b' && ch != 127)) {
			errno = EINVAL;
			return false;
		}
		i += n;
	}

	if (input->offset) {
		memmove(input->data, input->data + input->offset, input->length);
		input->offset = 0;
	}
	for (size_t i = 0; i < length; i++) {
		unsigned char ch = (unsigned char) text[i];
		if (ch == '\r')
			ch = '\n';
		else if (ch == '\b' || ch == 127)
			ch = input->erase;
		input->data[input->length++] = (char) ch;
	}
	return true;
}

bool termbox_input_flush(struct termbox_input *input, int fd)
{
	while (input->length) {
		ssize_t n = write(fd, input->data + input->offset, input->length);
		if (n > 0) {
			input->offset += (size_t) n;
			input->length -= (size_t) n;
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return true;
		if (!n)
			errno = EIO;
		return false;
	}
	input->offset = 0;
	return true;
}
