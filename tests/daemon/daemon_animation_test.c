// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <curses.h>

#include "daemon_animation.h"

static void test_terminal(const char *name, int tty_fd, enum daemon_animation expected)
{
	FILE *input = tmpfile();
	FILE *output = tmpfile();
	assert(input && output);
	SCREEN *screen = newterm(name, output, input);
	assert(screen);

	assert(daemon_animation_resolve(DAEMON_ANIMATION_AUTO, tty_fd) == expected);
	assert(daemon_animation_resolve(DAEMON_ANIMATION_NONE, tty_fd) == DAEMON_ANIMATION_NONE);
	assert(daemon_animation_resolve(DAEMON_ANIMATION_SLIDE, tty_fd) == DAEMON_ANIMATION_SLIDE);
	assert(daemon_animation_resolve(DAEMON_ANIMATION_AUTO, fileno(output)) == DAEMON_ANIMATION_NONE);
	assert(daemon_animation_resolve(DAEMON_ANIMATION_NONE, fileno(output)) == DAEMON_ANIMATION_NONE);
	assert(daemon_animation_resolve(DAEMON_ANIMATION_SLIDE, fileno(output)) == DAEMON_ANIMATION_SLIDE);

	endwin();
	delscreen(screen);
	assert(fclose(input) == 0);
	assert(fclose(output) == 0);
}

int main(void)
{
	int master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
	assert(master >= 0);
	assert(grantpt(master) == 0 && unlockpt(master) == 0);
	char *name = ptsname(master);
	assert(name);
	int slave = open(name, O_RDWR | O_NOCTTY | O_CLOEXEC);
	assert(slave >= 0);

	test_terminal("xterm", slave, DAEMON_ANIMATION_SLIDE);
	test_terminal("linux", slave, DAEMON_ANIMATION_SLIDE);
	test_terminal("dumb", slave, DAEMON_ANIMATION_NONE);

	assert(close(slave) == 0);
	assert(close(master) == 0);
	return 0;
}
