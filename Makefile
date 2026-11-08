# ===================================================================
# MAKEFILE MED UNDERKATALOGER OG DYNAMISK INSTALLATION
# ===================================================================
CC      = gcc
CFLAGS  = -O2 -g -Wall $(shell pkg-config --cflags gio-2.0 glib-2.0 libmenu-cache gdk-pixbuf-2.0)
LIBS    = -lXm -lXt -lX11 $(shell pkg-config --libs gio-2.0 glib-2.0 libmenu-cache gdk-pixbuf-2.0)
INCDIRS = -I/usr/include

# Installation paths
PREFIX  = /usr/local
BINDIR  = $(PREFIX)/bin

TARGET  = motif_panel

# Subdirectories for graphics
SRCS    = motif_panel.c grafik/motif_grafik.c
OBJS    = motif_panel.o grafik/motif_grafik.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(INCDIRS) -o $(TARGET) $(OBJS) $(LIBS)

# Rule to build object files in root directory and subdirectories
%.o: %.c
	$(CC) $(CFLAGS) $(INCDIRS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

install: $(TARGET)
	@echo "[Install] Creates installation directories..."
	install -d $(DESTDIR)$(BINDIR)
	@echo "[Install] Installering the binary file in $(BINDIR)..."
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	@echo "[Install] Installation completed succesfully!"

uninstall:
	@echo "[Uninstall] Removing the binary file from $(BINDIR)..."
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

.PHONY: all clean install uninstall
