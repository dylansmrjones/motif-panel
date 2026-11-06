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

            /* Hent de præcise RGB-masker fra skærmens Visual for at ramme TrueColor rigtigt */
            unsigned long pixel_vaerdi = 0;
            
            if (g_visual->red_mask == 0xff0000 && g_visual->green_mask == 0xff00 && g_visual->blue_mask == 0xff) {
                pixel_vaerdi = (r << 16) | (g << 8) | b;
            } else if (g_visual->red_mask == 0xff && g_visual->green_mask == 0xff00 && g_visual->blue_mask == 0xff0000) {
                pixel_vaerdi = (b << 16) | (g << 8) | r;
            } else {
                /* Universel matematisk bitmask-fordeling hvis overstående fejler */
                pixel_vaerdi = ((r * g_visual->red_mask / 255) & g_visual->red_mask) |
                               ((g * g_visual->green_mask / 255) & g_visual->green_mask) |
                               ((b * g_visual->blue_mask / 255) & g_visual->blue_mask);
            }

            /* KRITISK RETTELSE: Vi bruger XPutPixel, som automatisk håndterer rowstride og skærmens endianness */
            XPutPixel(ximage, x, y, pixel_vaerdi);
        }
    }

    /* Skub billedet op til Pixmap-hukommelsen */
    XPutImage(g_display, pixmap, gc, ximage, 0, 0, 0, 0, bredde, hojde);
    
    /* Ryd op */
    XDestroyImage(ximage);
    XFreeGC(g_display, gc);

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
