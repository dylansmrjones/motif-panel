# Motif Panel

A lightweight, XDG-compliant desktop panel and taskbar written in pure C and Motif (OpenMotif 2.3). Designed to integrate seamlessly with retro-styled window managers like **EMWM** or **MWM** while providing modern Linux desktop functionality.

## Features

- **Xft Anti-Aliased Fonts:** Full support for modern TrueType fonts (like Helvetica) via your local `~/.Xresources`.
- **TrueColor Icon Engine:** On-the-fly loading, scaling, and alpha-blending of modern PNG and SVG application icons.
- **Dynamic Tasklist:** Real-time updating window buttons showcasing official app icons side-by-side with titles.
- **Graphical Quick Launcher:** An integrated, asynchronous setup dialog to add, update, and remove your favorite shortcuts directly from the desktop.
- **XDG Menu-Cache Integration:** Fast, categorized application structure loading synced with your system's `.desktop` files.
- **Hybrid Event Loop:** Dual-mode asynchronous loop handling native X11 window events and modern D-Bus AppIndicator applets.

## Dependencies

Before compiling, ensure you have the following development libraries installed on your system:

- `motif` (OpenMotif 2.3)
- `gdk-pixbuf` (with SVG/PNG support enabled)
- `librsvg` (required for SVG icon rendering)
- `libmenu-cache from LXDE`
- `glib` (including GIO for D-Bus communication)

## Compilation and Installation

Building and installing the panel is straightforward using the provided `Makefile`. 

1. **Clone the repository:**
   ```bash
   git clone https://github.com/dylansmrjones/motif-panel
   cd motif-panel
   ```

2. **Compile the binary:**
   ```bash
   make -j\$(nproc)
   ```

3. **Install globally (installs to `/usr/local/bin`):**
   ```bash
   sudo make install
   ```

## Configuration

The panel reads its layout and configuration on startup from `~/.config/motif_panelrc`. You can adjust the panel height and vertical positioning directly inside the file:

```rc
position=bottom
panel_height=40
```

To configure your font rendering, add the following resources to your global `~/.Xresources` database:

```rc
*menu_skal*renderTable: xftTable
*menu_skal*XmPushButton.renderTable: xftTable
*XmMenuShell*renderTable: xftTable
```

## License

This project is released under the open-source **BSD 2-Clause License**. See the `LICENSE` file for details.
