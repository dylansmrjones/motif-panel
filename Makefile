# ===================================================================
# MAKEFILE MED UNDERKATALOGER OG DYNAMISK INSTALLATION
# ===================================================================
CC      = gcc
CFLAGS  = -O2 -g -Wall $(shell pkg-config --cflags gio-2.0 glib-2.0 libmenu-cache gdk-pixbuf-2.0)
LIBS    = -lXm -lXt -lX11 $(shell pkg-config --libs gio-2.0 glib-2.0 libmenu-cache gdk-pixbuf-2.0)
INCDIRS = -I/usr/include

# Installationsstier
PREFIX  = /usr/local
BINDIR  = $(PREFIX)/bin

TARGET  = motif_panel

# RETTELSE: Opdateret kildestier til underkataloger
SRCS    = motif_panel.c grafik/motif_grafik.c
OBJS    = motif_panel.o grafik/motif_grafik.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(INCDIRS) -o $(TARGET) $(OBJS) $(LIBS)

# Regel til at bygge objektfiler i rodmappen og underkataloger
%.o: %.c
	$(CC) $(CFLAGS) $(INCDIRS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

install: $(TARGET)
	@echo "[Install] Opretter installationsmapper..."
	install -d $(DESTDIR)$(BINDIR)
	@echo "[Install] Installerer den binære fil til $(BINDIR)..."
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	@echo "[Install] Installation fuldført succesfuldt!"

uninstall:
	@echo "[Uninstall] Fjerner den binære fil fra $(BINDIR)..."
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

.PHONY: all clean install uninstall
