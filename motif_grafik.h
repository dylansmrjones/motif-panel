#ifndef MOTIF_GRAFIK_H
#define MOTIF_GRAFIK_H

#include <Xm/Xm.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

/* Initialiserer grafikmodulet med panelets X11-kontekst */
void motif_grafik_init(Display *dpy, Visual *vis, Colormap cmap);

/* Indlæser og skalerer en PNG/SVG-fil til en færdig Motif Pixmap */
Pixmap motif_grafik_indlaes_fil(const char *filsti, int maaloes_bredde, int maaloes_hojde);

/* Konverterer rå RGBA/ARGBA pixelbuffers (fx fra DBus eller X11) til en Motif Pixmap */
Pixmap motif_grafik_fra_buffer(const guchar *pixel_data, int kilde_bredde, int kilde_hojde, 
                               int maaloes_bredde, int maaloes_hojde, Boolean har_alfa);

#endif /* MOTIF_GRAFIK_H */
