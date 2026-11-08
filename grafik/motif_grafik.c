/* ===================================================================
   MOTIF_GRAFIK.C
   Copyright (c) 2026, Kristian Poul Herkild. All rights reserved.
   Use and distribution is permitted under the 2-Clause BSD License.
   =================================================================== */

/* ===================================================================
   MOTIF_GRAFIK.C - GENANVENDELIGT GRAFIKMODUL TIL HÅNDTERING AF PNG/SVG
   =================================================================== */
#include "motif_grafik.h"
#include <X11/Xutil.h>
#include <string.h>
#include <stdlib.h>

/* Interne statiske X11-kontekstvariabler */
static Display *g_display = NULL;
static Visual  *g_visual  = NULL;
static Colormap g_colormap;

/* Initialiserer grafikmodulet med panelets X11-kontekst */
void motif_grafik_init(Display *dpy, Visual *vis, Colormap cmap) {
    g_display  = dpy;
    g_visual   = vis;
    g_colormap = cmap;
}


/* Universel bit-skift metode baseret på maskens placering */
unsigned long beregn_x11_pixel(Visual *visual, int r, int g, int b) {
    unsigned long r_mask = visual->red_mask;
    unsigned long g_mask = visual->green_mask;
    unsigned long b_mask = visual->blue_mask;

    // Find bit-positionerne (hvor mange pladser skal vi skubbe værdien til venstre)
    int r_shift = 0; while (!(r_mask & 1) && r_mask) { r_mask >>= 1; r_shift++; }
    int g_shift = 0; while (!(g_mask & 1) && g_mask) { g_mask >>= 1; g_shift++; }
    int b_shift = 0; while (!(b_mask & 1) && b_mask) { b_mask >>= 1; b_shift++; }

    // Skaler 8-bit RGB til maskens reelle bit-størrelse og skub på plads
    unsigned long p_r = ((r * r_mask) / 255) << r_shift;
    unsigned long p_g = ((g * g_mask) / 255) << g_shift;
    unsigned long p_b = ((b * b_mask) / 255) << b_shift;

    return p_r | p_g | p_b;
}

/* Konverterer en GdkPixbuf til en ægte X11 Pixmap med alfa-blending mod panelbaggrunden */
static Pixmap pixbuf_til_pixmap(GdkPixbuf *pixbuf, int bredde, int hojde) {
    if (!g_display || !pixbuf) return None;

    int skaerm = DefaultScreen(g_display);
    int dybde  = DefaultDepth(g_display, skaerm);

    /* Opret en tom X11 Pixmap i den korrekte skærmdybde */
    Pixmap pixmap = XCreatePixmap(g_display, DefaultRootWindow(g_display), bredde, hojde, dybde);
    if (!pixmap) return None;

    GC gc = XCreateGC(g_display, pixmap, 0, NULL);

    int kanaler      = gdk_pixbuf_get_n_channels(pixbuf);
    int rowstride    = gdk_pixbuf_get_rowstride(pixbuf);
    guchar *pixels   = gdk_pixbuf_get_pixels(pixbuf);
    gboolean har_alfa = gdk_pixbuf_get_has_alpha(pixbuf);

    /* Vi opretter et tomt XImage i X-serverens foretrukne format og lader X11 tildele bufferen */
    XImage *ximage = XCreateImage(g_display, g_visual, dybde, ZPixmap, 0, NULL, bredde, hojde, 32, 0);
    if (!ximage) {
        XFreeGC(g_display, gc);
        XFreePixmap(g_display, pixmap);
        return None;
    }
    
    /* Tildel hukommelse til XImage data */
    ximage->data = (char *)malloc(ximage->bytes_per_line * hojde);

    /* Standard retro-grå baggrundsfarve (Motif #dcdcdc) til at blende gennemsigtige pixels mod */
    int bg_r = 0xdc, bg_g = 0xdc, bg_b = 0xdc;

    for (int y = 0; y < hojde; y++) {
                for (int x = 0; x < bredde; x++) {
            guchar *p = pixels + y * rowstride + x * kanaler;
            int r = p[0];
            int g = p[1];
            int b = p[2];

            if (har_alfa) {
                int a = p[3];
                /* Lineær alfa-blending mod panelets grå farve */
                r = (r * a + bg_r * (255 - a)) / 255;
                g = (g * a + bg_g * (255 - a)) / 255;
                b = (b * a + bg_b * (255 - a)) / 255;
            }

            /* ===================================================================
               NY UNIVERSEL RETTELSE: Dynamisk bitmask-beregning ud fra hardwaren
               =================================================================== */
            unsigned long pixel_vaerdi = beregn_x11_pixel(g_visual, r, g, b);

            /* KRITISK RETTELSE: Vi bruger XPutPixel, som automatisk håndterer rowstride og skærmens endianness */
            XPutPixel(ximage, x, y, pixel_vaerdi);
        }

    }

    /* Skub billedet op til Pixmap-hukommelsen */
    XPutImage(g_display, pixmap, gc, ximage, 0, 0, 0, 0, bredde, hojde);
    
    ximage->data = (char *)malloc(ximage->bytes_per_line * hojde);
    if (!ximage->data) {
        /* RETTELSE: Tving data feltet til NULL, så XDestroyImage ved, 
           at den udelukkende skal frigøre selve kontrolstrukturen i RAM */
        ximage->data = NULL; 
        
        XDestroyImage(ximage);
        XFreeGC(g_display, gc);
        XFreePixmap(g_display, pixmap);
        return None;
    }

    return pixmap;
}

/* Indlæser og skalerer en PNG/SVG-fil til en færdig Motif Pixmap */
Pixmap motif_grafik_indlaes_fil(const char *filsti, int maaloes_bredde, int maaloes_hojde) {
    GError *error = NULL;
    
    /* GdkPixbuf indlæser automatisk både PNG og SVG dynamically udefra */
    GdkPixbuf *pb_original = gdk_pixbuf_new_from_file(filsti, &error);
    if (!pb_original) {
        g_printerr("[Motif Grafik] Kunne ikke indlæse fil (%s): %s\n", filsti, error->message);
        g_error_free(error);
        return None;
    }

    /* Skaler billedet uden tab (Bilinear/Hyper interpolation) til målstørrelsen */
    GdkPixbuf *pb_skaleret = gdk_pixbuf_scale_simple(pb_original, maaloes_bredde, maaloes_hojde, GDK_INTERP_HYPER);
    g_object_unref(pb_original);

    if (!pb_skaleret) {
        g_printerr("[Motif Grafik] Skalering fejlede for: %s\n", filsti);
        return None;
    }

    /* Konverter til X11 Pixmap og ryd op i GLib objekterne */
    Pixmap resultat = pixbuf_til_pixmap(pb_skaleret, maaloes_bredde, maaloes_hojde);
    g_object_unref(pb_skaleret);

    return resultat;
}

/* Konverterer rå RGBA/ARGBA pixelbuffers (fx fra DBus eller X11) til en Motif Pixmap */
Pixmap motif_grafik_fra_buffer(const guchar *pixel_data, int kilde_bredde, int kilde_hojde, 
                               int maaloes_bredde, int maaloes_hojde, Boolean har_alfa) {
    if (!pixel_data) return None;

    int kanaler = har_alfa ? 4 : 3;
    int rowstride = kilde_bredde * kanaler;

    /* Opret en GdkPixbuf wrapper direkte ovenpå den rå hukommelses-buffer */
    GdkPixbuf *pb_original = gdk_pixbuf_new_from_data(pixel_data, GDK_COLORSPACE_RGB, har_alfa, 8, 
                                                      kilde_bredde, kilde_hojde, rowstride, 
                                                      NULL, NULL);
    if (!pb_original) return None;

    /* Skaler genvejsikonet eller applet-ikonet til panelets ønskede størrelse */
    GdkPixbuf *pb_skaleret = gdk_pixbuf_scale_simple(pb_original, maaloes_bredde, maaloes_hojde, GDK_INTERP_BILINEAR);
    g_object_unref(pb_original);

    if (!pb_skaleret) return None;

    Pixmap resultat = pixbuf_til_pixmap(pb_skaleret, maaloes_bredde, maaloes_hojde);
    g_object_unref(pb_skaleret);

    return resultat;
}
