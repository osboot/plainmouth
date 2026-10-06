// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PLAINMOUTH_DAEMON_ANIMATION_H
#define PLAINMOUTH_DAEMON_ANIMATION_H

enum daemon_animation {
	DAEMON_ANIMATION_NONE,
	DAEMON_ANIMATION_SLIDE,
	DAEMON_ANIMATION_AUTO,
};

/* Resolve auto after ncurses initialization, using the actual output TTY. */
enum daemon_animation daemon_animation_resolve(enum daemon_animation mode, int fd);

#endif
