// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <linux/major.h>

#include <curses.h>
#include <term.h>

#include "daemon_animation.h"

enum daemon_animation daemon_animation_resolve(enum daemon_animation mode, int fd)
{
	if (mode != DAEMON_ANIMATION_AUTO)
		return mode;

	unsigned int device = 0;

	if (ioctl(fd, TIOCGDEV, &device) < 0)
		return DAEMON_ANIMATION_NONE;

	unsigned int tty_major = major(device);

	if (tty_major != PTY_SLAVE_MAJOR &&
	    (tty_major < UNIX98_PTY_SLAVE_MAJOR ||
	     tty_major >= UNIX98_PTY_SLAVE_MAJOR + UNIX98_PTY_MAJOR_COUNT))
		return DAEMON_ANIMATION_NONE;

	char *cursor = tigetstr("cup");

	if (!cursor || cursor == (char *) -1 || !*cursor || tigetflag("hc") > 0)
		return DAEMON_ANIMATION_NONE;

	return DAEMON_ANIMATION_SLIDE;
}
