CFLAGS = $(shell pkg-config --cflags ncursesw panelw)
LIBS   = $(shell pkg-config --libs ncursesw panelw)

PROG = panel
SRC = panel.c

$(PROG): $(SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -o $@ $(LIBS)
