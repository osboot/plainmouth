// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdio.h>
#include <stdlib.h>
#include <curses.h>
#include <panel.h>

int main(void) {
    SCREEN *scr = newterm(NULL, stdout, stdin);
    set_term(scr);

    WINDOW *w = newwin(3, 10, 1, 1);
    PANEL *p = new_panel(w);

    del_panel(p);
    delwin(w);

    endwin();
    delscreen(scr);

    return 0;
}
