/* ===================================================================
   MOTIF_PANEL.C
   Copyright (c) 2026, Kristian Poul Herkild. All rights reserved.
   Use and distribution is permitted under the 2-Clause BSD License.
   =================================================================== */

#include <Xm/Xm.h>
#include <Xm/RowColumn.h>
#include <Xm/PushB.h>
#include <Xm/CascadeB.h>
#include <Xm/Label.h>
#include <Xm/Form.h>
#include <Xm/Separator.h>
#include <Xm/List.h>
#include <Xm/TextF.h>
#include <Xm/MessageB.h>
#include <X11/Shell.h>
#include <X11/Xatom.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <locale.h>
#include <gio/gio.h>
#include <glib.h>
#include <menu-cache.h>
#include "grafik/motif_grafik.h"

/* Globale Xt-kontekster og layouts */
XtAppContext app_context;
Widget ur_label;
Widget task_list; 
Widget pager_list;
Widget tray_area;
Widget menu_knap;
Widget quick_launch;
XmRenderTable minXftTable;
Display *display;

/* Tilstandsvariabler for skriveborde og placering */
long aktuelt_skrivebord = 0;
long total_skriveborde = 4;
char panel_position[16] = "bottom";
int panel_hojde = 32;
/* Struktur til Quick Launch i hukommelsen */
typedef struct {
    char navn[64];
    char exec_cmd[256];
    char ikon_sti[256];
} QuickLaunchItem;

#define MAX_QUICK_ITEMS 20
QuickLaunchItem quick_items[MAX_QUICK_ITEMS];
int quick_items_antal = 0;

/* Globale dialog-widgets til grafisk konfiguration */
Widget ql_dialog = NULL;
Widget ql_list_widget = NULL;
Widget ql_text_navn = NULL;
Widget ql_text_exec = NULL;
Widget ql_text_ikon = NULL;

/* Forward declarations */
void opdater_quick_launch_bar(void);
void vis_quicklaunch_dialog(Widget parent);
void gem_konfiguration(void);
void parse_quicklaunch_streng(char *streng);
void laes_konfiguration(void);
XmString opret_utf8_xmstring(char *tekst);
void start_applikation(Widget w, XtPointer client_data, XtPointer call_data);
void opdater_vindues_liste(void);
Pixmap hent_vindues_ikon(Window win, int maaloes_str);
/* Spørger X-serveren dynamisk om antallet af skriveborde */
long hent_total_skriveborde() {
    Atom prop = XInternAtom(display, "_NET_NUMBER_OF_DESKTOPS", False);
    Atom type; int format; unsigned long nitems, bytes_after;
    unsigned char *prop_data = NULL; long antal_skriveborde = 4;

    if (XGetWindowProperty(display, DefaultRootWindow(display), prop, 0, 1, False,
                           XA_CARDINAL, &type, &format, &nitems, &bytes_after, &prop_data) == Success && prop_data) {
        if (nitems > 0) antal_skriveborde = *(long *)prop_data;
        XFree(prop_data);
    }
    return antal_skriveborde;
}

/* Henter hvilket skrivebord et givet vindue befinder sig på */
long hent_vindues_skrivebord(Window win) {
    Atom prop = XInternAtom(display, "_NET_WM_DESKTOP", False);
    Atom type; int format; unsigned long nitems, bytes_after;
    unsigned char *prop_data = NULL; long skrivebord = 0;

    if (XGetWindowProperty(display, win, prop, 0, 1, False, AnyPropertyType,
                           &type, &format, &nitems, &bytes_after, &prop_data) == Success && prop_data) {
        if (nitems > 0) skrivebord = *(long *)prop_data;
        XFree(prop_data);
    }
    return skrivebord;
}

/* Tjekker om et vindue er minimeret */
Boolean er_vindue_minimeret(Window win) {
    Atom prop = XInternAtom(display, "WM_STATE", False);
    Atom type; int format; unsigned long nitems, bytes_after;
    unsigned char *prop_data = NULL; Boolean minimeret = False;

    if (XGetWindowProperty(display, win, prop, 0, 2, False, AnyPropertyType,
                           &type, &format, &nitems, &bytes_after, &prop_data) == Success && prop_data) {
        unsigned long *state = (unsigned long *)prop_data;
        if (nitems > 0 && *state == IconicState) minimeret = True;
        XFree(prop_data);
    }
    return minimeret;
}

/* Tjekker om et vindue er det aktive forgrundsvindue */
Boolean er_vindue_aktivt(Window win) {
    Atom prop = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
    Atom type; int format; unsigned long nitems, bytes_after;
    unsigned char *prop_data = NULL; Boolean aktivt = False;

    if (XGetWindowProperty(display, DefaultRootWindow(display), prop, 0, 1, False, XA_WINDOW,
                           &type, &format, &nitems, &bytes_after, &prop_data) == Success && prop_data) {
        Window aktuelt_aktivt = *(Window *)prop_data;
        if (aktuelt_aktivt == win) aktivt = True;
        XFree(prop_data);
    }
    return aktivt;
}

/* Henter vinduestitlen i ægte UTF-8 format */
char* hent_vindues_navn(Window win) {
    Atom prop = XInternAtom(display, "_NET_WM_NAME", False);
    XTextProperty text_prop; char **liste = NULL; int antal = 0; char *retur_navn = NULL;
    Atom faktisk_type; int faktisk_format; unsigned long antal_items, rest_bytes;

    if (XGetWindowProperty(display, win, prop, 0, 100, False, AnyPropertyType,
                           &faktisk_type, &faktisk_format, &antal_items,
                           &rest_bytes, &text_prop.value) == Success && text_prop.value) {
        text_prop.encoding = faktisk_type; text_prop.format = faktisk_format; text_prop.nitems = antal_items;
        if (Xutf8TextPropertyToTextList(display, &text_prop, &liste, &antal) == Success && antal > 0 && liste) {
            retur_navn = strdup(liste[0]); XFreeStringList(liste);
        }
        XFree(text_prop.value);
    }
    if (!retur_navn) XFetchName(display, win, &retur_navn);
    return retur_navn ? retur_navn : strdup("Ukendt vindue");
}
/* Skriver panelets indstillinger og hurtigstart-bjælke til disken */
void gem_konfiguration() {
    char sti[512]; char *home = getenv("HOME");
    if (!home) return;
    
    snprintf(sti, sizeof(sti), "%s/.config/motif_panelrc", home);
    FILE *f = fopen(sti, "w");
    if (!f) { g_printerr("[Config] Kunne ikke åbne konfigurationsfilen for skrivning.\n"); return; }

    fprintf(f, "position=%s\n", panel_position);
    fprintf(f, "panel_height=%d\n", panel_hojde);

    if (quick_items_antal > 0) {
        fprintf(f, "quicklaunch=");
        for (int i = 0; i < quick_items_antal; i++) {
            fprintf(f, "%s;%s;%s", quick_items[i].navn, quick_items[i].exec_cmd, quick_items[i].ikon_sti);
            if (i < quick_items_antal - 1) fprintf(f, "|");
        }
        fprintf(f, "\n");
    }
    fclose(f);
}

/* Dekomponerer hurtigstart-strengen sikkert via snprintf */
void parse_quicklaunch_streng(char *streng) {
    quick_items_antal = 0;
    if (!streng || strlen(streng) == 0) return;

    char *kopi = strdup(streng);
    char *saveptr1;
    char *token = strtok_r(kopi, "|", &saveptr1);
    
    while (token && quick_items_antal < MAX_QUICK_ITEMS) {
        char *element = strdup(token);
        char *saveptr2;
        char *navn = strtok_r(element, ";", &saveptr2);
        char *exec = strtok_r(NULL, ";", &saveptr2);
        char *ikon = strtok_r(NULL, ";", &saveptr2);

        if (navn && exec && ikon) {
            g_strstrip(navn); g_strstrip(exec); g_strstrip(ikon);

            snprintf(quick_items[quick_items_antal].navn, sizeof(quick_items[quick_items_antal].navn), "%s", navn);
            snprintf(quick_items[quick_items_antal].exec_cmd, sizeof(quick_items[quick_items_antal].exec_cmd), "%s", exec);
            snprintf(quick_items[quick_items_antal].ikon_sti, sizeof(quick_items[quick_items_antal].ikon_sti), "%s", ikon);
            quick_items_antal++;
        }
        free(element);
        token = strtok_r(NULL, "|", &saveptr1);
    }
    free(kopi);
}

void laes_konfiguration() {
    char sti[512]; char *home = getenv("HOME");
    if (!home) return;
    
    snprintf(sti, sizeof(sti), "%s/.config/motif_panelrc", home);
    FILE *f = fopen(sti, "r");
    if (!f) return;

    char linje[1024];
    while (fgets(linje, sizeof(linje), f)) {
        linje[strcspn(linje, "\r\n")] = 0;
        char *nogle = strtok(linje, "=");
        char *vaerdi = strtok(NULL, "=");
        
        if (nogle && vaerdi) {
            if (strcmp(nogle, "position") == 0) strncpy(panel_position, vaerdi, sizeof(panel_position) - 1);
            else if (strcmp(nogle, "panel_height") == 0) {
                panel_hojde = atoi(vaerdi);
                if (panel_hojde < 16) panel_hojde = 16;
            } else if (strcmp(nogle, "quicklaunch") == 0) {
                parse_quicklaunch_streng(vaerdi);
            }
        }
    }
    fclose(f);
}
XmString opret_utf8_xmstring(char *tekst) {
    if (!tekst) return NULL;
    return XmStringGenerate((XtPointer)tekst, NULL, XmCHARSET_TEXT, XmFONTLIST_DEFAULT_TAG);
}

void skift_skrivebord_cb(Widget w, XtPointer client_data, XtPointer call_data) {
    long mål_skrivebord = (long)client_data; XEvent xev;
    long mask = SubstructureRedirectMask | SubstructureNotifyMask;
    Atom current_desktop_atom = XInternAtom(display, "_NET_CURRENT_DESKTOP", False);
    memset(&xev, 0, sizeof(xev)); xev.xclient.type = ClientMessage;
    xev.xclient.message_type = current_desktop_atom; xev.xclient.window = DefaultRootWindow(display);
    xev.xclient.format = 32; xev.xclient.data.l[0] = mål_skrivebord; xev.xclient.data.l[1] = CurrentTime;
    XSendEvent(display, DefaultRootWindow(display), False, mask, &xev); XFlush(display);
}

void aktiver_vindue_cb(Widget w, XtPointer client_data, XtPointer call_data) {
    Window win = (Window)client_data; long mål_skrivebord = hent_vindues_skrivebord(win);
    long mask = SubstructureRedirectMask | SubstructureNotifyMask; XEvent xev;
    if (mål_skrivebord != aktuelt_skrivebord && mål_skrivebord != -1) {
        skift_skrivebord_cb(NULL, (XtPointer)mål_skrivebord, NULL);
    }
    memset(&xev, 0, sizeof(xev)); xev.xclient.type = ClientMessage; xev.xclient.display = display;
    xev.xclient.window = win; xev.xclient.format = 32;
    if (er_vindue_minimeret(win) || !er_vindue_aktivt(win)) {
        xev.xclient.message_type = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
        xev.xclient.data.l[0] = 1; xev.xclient.data.l[1] = CurrentTime;
    } else {
        xev.xclient.message_type = XInternAtom(display, "WM_CHANGE_STATE", False);
        xev.xclient.data.l[0] = IconicState;
    }
    XSendEvent(display, DefaultRootWindow(display), False, mask, &xev); XFlush(display);
}

void flyt_vindue_menu_cb(Widget w, XtPointer client_data, XtPointer call_data) {
    unsigned long pakke = (unsigned long)client_data;
    long mål_skrivebord = pakke & 0xFF; Window win_at_flytte = (Window)(pakke >> 8);
    if (win_at_flytte != None) {
        Atom wm_desktop = XInternAtom(display, "_NET_WM_DESKTOP", False); XEvent xev;
        long mask = SubstructureRedirectMask | SubstructureNotifyMask;
        memset(&xev, 0, sizeof(xev)); xev.xclient.type = ClientMessage; xev.xclient.display = display;
        xev.xclient.message_type = wm_desktop; xev.xclient.window = win_at_flytte; xev.xclient.format = 32;
        xev.xclient.data.l[0] = mål_skrivebord; xev.xclient.data.l[1] = 1;
        XSendEvent(display, DefaultRootWindow(display), False, mask, &xev); XFlush(display);
    }
}

void vindue_knap_popup_handler(Widget w, XtPointer client_data, XEvent *event, Boolean *continue_to_dispatch) {
    Widget private_menu = (Widget)client_data;
    if (event->type == ButtonPress && event->xbutton.button == Button3) {
        XmMenuPosition(private_menu, (XButtonPressedEvent *)event); XtManageChild(private_menu);
    }
}

void afslut_panel_cb(Widget w, XtPointer client_data, XtPointer call_data) { exit(0); }
void systray_xembed_handler(Widget w, XtPointer client_data, XEvent *event, Boolean *continue_to_dispatch) {
    Atom tray_opcode = XInternAtom(display, "_NET_SYSTEM_TRAY_OPCODE", False);
    if (event->type == ClientMessage && event->xclient.message_type == tray_opcode) {
        long opcode = event->xclient.data.l[1]; Window applet_window = event->xclient.data.l[2];
        if (opcode == 0) {
            Window tray_win = XtWindow(tray_area);
            if (tray_win && applet_window != None) {
                XReparentWindow(display, applet_window, tray_win, 0, 0); XMapWindow(display, applet_window);
                XEvent reply; Atom xembed_atom = XInternAtom(display, "_XEMBED", False);
                memset(&reply, 0, sizeof(reply)); reply.xclient.type = ClientMessage;
                reply.xclient.window = applet_window; reply.xclient.message_type = xembed_atom;
                reply.xclient.format = 32; reply.xclient.data.l[0] = CurrentTime;
                reply.xclient.data.l[1] = 0; reply.xclient.data.l[2] = tray_win;
                XSendEvent(display, applet_window, False, StructureNotifyMask, &reply); XFlush(display);
            }
        }
    }
}

const gchar watcher_introspection_xml[] =
    "<node><interface name='org.freedesktop.StatusNotifierWatcher'>"
    "<method name='RegisterStatusNotifierItem'><arg type='s' name='service' direction='in'/></method>"
    "<method name='RegisterStatusNotifierHost'><arg type='s' name='service' direction='in'/></method>"
    "<property name='RegisteredStatusNotifierItems' type='as' access='read'/>"
    "<property name='IsStatusNotifierHostRegistered' type='b' access='read'/>"
    "<property name='ProtocolVersion' type='i' access='read'/></interface></node>";

const gchar watcher_introspection_kde_xml[] =
    "<node><interface name='org.kde.StatusNotifierWatcher'>"
    "<method name='RegisterStatusNotifierItem'><arg type='s' name='service' direction='in'/></method>"
    "<method name='RegisterStatusNotifierHost'><arg type='s' name='service' direction='in'/></method>"
    "<property name='RegisteredStatusNotifierItems' type='as' access='read'/>"
    "<property name='IsStatusNotifierHostRegistered' type='b' access='read'/>"
    "<property name='ProtocolVersion' type='i' access='read'/></interface></node>";

GVariant *handle_get_property(GDBusConnection *connection, const gchar *sender, const gchar *object_path, const gchar *interface_name, const gchar *property_name, GError **error, gpointer user_data) {
    if (g_strcmp0(property_name, "IsStatusNotifierHostRegistered") == 0) return g_variant_new_boolean(TRUE);
    if (g_strcmp0(property_name, "ProtocolVersion") == 0) return g_variant_new_int32(0);
    if (g_strcmp0(property_name, "RegisteredStatusNotifierItems") == 0) {
        GVariantBuilder builder; g_variant_builder_init(&builder, G_VARIANT_TYPE("as")); return g_variant_builder_end(&builder);
    }
    return NULL;
}

void handle_method_call(GDBusConnection *connection, const gchar *sender, const gchar *object_path, const gchar *interface_name, const gchar *method_name, GVariant *parameters, GDBusMethodInvocation *invocation, gpointer user_data) {
    if (g_strcmp0(method_name, "RegisterStatusNotifierItem") == 0) {
        const gchar *service_navn; g_variant_get(parameters, "(&s)", &service_navn);
        g_print("[AppIndicator] NY APPLET ANMODNING over DBus fra service: %s\n", service_navn);
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "RegisterStatusNotifierHost") == 0) {
        g_dbus_method_invocation_return_value(invocation, NULL);
    }
}

const GDBusInterfaceVTable watcher_vtable = { handle_method_call, handle_get_property, NULL };

void on_bus_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
    static int allerede_registreret = 0; if (allerede_registreret) return; allerede_registreret = 1;
    GError *error = NULL;
    GDBusNodeInfo *introspection_fd = g_dbus_node_info_new_for_xml(watcher_introspection_xml, &error);
    GDBusNodeInfo *introspection_kde = g_dbus_node_info_new_for_xml(watcher_introspection_kde_xml, &error);
    if (error) { g_error_free(error); return; }

    g_dbus_connection_register_object(connection, "/org/freedesktop/StatusNotifierWatcher", introspection_fd->interfaces[0], &watcher_vtable, NULL, NULL, &error);
    g_dbus_connection_register_object(connection, "/StatusNotifierWatcher", introspection_kde->interfaces[0], &watcher_vtable, NULL, NULL, &error);
    if (error) { g_error_free(error); return; }

    g_bus_own_name_on_connection(connection, "org.freedesktop.StatusNotifierHost-motif_panel", G_BUS_NAME_OWNER_FLAGS_REPLACE, NULL, NULL, NULL, NULL);
    g_bus_own_name_on_connection(connection, "org.kde.StatusNotifierHost-motif_panel", G_BUS_NAME_OWNER_FLAGS_REPLACE, NULL, NULL, NULL, NULL);
    g_dbus_connection_emit_signal(connection, NULL, "/org/freedesktop/StatusNotifierWatcher", "org.freedesktop.StatusNotifierWatcher", "StatusNotifierHostRegistered", NULL, &error);
    g_dbus_connection_emit_signal(connection, NULL, "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher", "StatusNotifierHostRegistered", NULL, &error);
}
void on_name_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {}
void on_name_lost(GDBusConnection *connection, const gchar *name, gpointer user_data) {}
/* Henter applikationens officielle ikon fra _NET_WM_ICON og konverterer til Motif Pixmap */
Pixmap hent_vindues_ikon(Window win, int maaloes_str) {
    Atom prop = XInternAtom(display, "_NET_WM_ICON", False);
    Atom faktisk_type; int faktisk_format;
    unsigned long antal_items, rest_bytes;
    unsigned char *prop_data = NULL;
    Pixmap resultat = None;

    if (XGetWindowProperty(display, win, prop, 0, 10000, False, XA_CARDINAL,
                           &faktisk_type, &faktisk_format, &antal_items,
                           &rest_bytes, &prop_data) == Success && prop_data) {
        
        if (antal_items > 2) {
            unsigned long *data = (unsigned long *)prop_data;
            int kilde_bredde = data[0];
            int kilde_hojde = data[1];
            unsigned long *pixel_start = &data[2];

            int total_pixels = kilde_bredde * kilde_hojde;
            guchar *rgba_buffer = (guchar *)malloc(total_pixels * 4);

            if (rgba_buffer) {
                for (int i = 0; i < total_pixels; i++) {
                    unsigned long p = pixel_start[i];
                    guchar a = (p >> 24) & 0xFF;
                    guchar r = (p >> 16) & 0xFF;
                    guchar g = (p >> 8)  & 0xFF;
                    guchar b = p         & 0xFF;

                    rgba_buffer[i * 4 + 0] = r;
                    rgba_buffer[i * 4 + 1] = g;
                    rgba_buffer[i * 4 + 2] = b;
                    rgba_buffer[i * 4 + 3] = a;
                }

                resultat = motif_grafik_fra_buffer(rgba_buffer, kilde_bredde, kilde_hojde, 
                                                   maaloes_str, maaloes_str, True);
                free(rgba_buffer);
            }
        }
        XFree(prop_data);
    }
    return resultat;
}
void opdater_vindues_liste() {
    Atom current_desktop_atom = XInternAtom(display, "_NET_CURRENT_DESKTOP", False);
    Atom ret_type = None; int ret_format = 0; unsigned long ret_nitems = 0, ret_bytes_after = 0;
    unsigned char *desktop_prop = NULL;
    
    if (XGetWindowProperty(display, DefaultRootWindow(display), current_desktop_atom, 0, 1, False,
                           XA_CARDINAL, &ret_type, &ret_format, &ret_nitems, &ret_bytes_after, &desktop_prop) == Success && desktop_prop) {
        aktuelt_skrivebord = *(long *)desktop_prop; XFree(desktop_prop);
    }

    WidgetList gamle_knapper = NULL; Cardinal antal_gamle = 0;
    XtVaGetValues(task_list, XmNchildren, &gamle_knapper, XmNnumChildren, &antal_gamle, NULL);
    
    if (gamle_knapper && antal_gamle > 0) {
        Widget *kopi_liste = (Widget *)malloc(sizeof(Widget) * antal_gamle);
        for (int i = 0; i < antal_gamle; i++) kopi_liste[i] = gamle_knapper[i];
        XtUnmanageChildren(kopi_liste, antal_gamle);
        for (int i = 0; i < antal_gamle; i++) {
            if (kopi_liste[i]) {
                Window *old_win = NULL; 
                XtVaGetValues(kopi_liste[i], XmNuserData, &old_win, NULL);
                
                /* RETTELSE: Opdelt, så XtDestroyWidget står på sin egen linje uden for if-blokken */
                if (old_win) {
                    free(old_win);
                }
                XtDestroyWidget(kopi_liste[i]);
            }
        }
        free(kopi_liste);
    }

    Atom client_list_atom = XInternAtom(display, "_NET_CLIENT_LIST", False);
    Atom faktisk_type = None; int faktisk_format = 0; unsigned long antal_vinduer = 0, rest_bytes = 0; unsigned char *data = NULL;

    if (XGetWindowProperty(display, DefaultRootWindow(display), client_list_atom, 0, 1024, False,
                           XA_WINDOW, &faktisk_type, &faktisk_format, &antal_vinduer, &rest_bytes, &data) == Success && data) {
        Window *vinduer = (Window *)data;
        Atom wm_type_atom = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
        Atom type_dock = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DOCK", False);
        Atom type_desktop = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);

        for (unsigned long i = 0; i < antal_vinduer; i++) {
            long v_skrivebord = hent_vindues_skrivebord(vinduer[i]);
            if (v_skrivebord != aktuelt_skrivebord && v_skrivebord != -1) continue;

            Atom aktuel_type = None; unsigned char *type_prop = NULL;
            if (XGetWindowProperty(display, vinduer[i], wm_type_atom, 0, 1, False, XA_ATOM, &ret_type, &ret_format, &ret_nitems, &ret_bytes_after, &type_prop) == Success && type_prop) {
                aktuel_type = *(Atom *)type_prop; XFree(type_prop);
            }
            if (aktuel_type == type_dock || aktuel_type == type_desktop) continue;

            char *navn = hent_vindues_navn(vinduer[i]); if (!navn) continue;
            if (strstr(navn, "pcmanfm-desktop") || strstr(navn, "LXQt Panel") || strstr(navn, "./motif_panel")) { 
                free(navn); continue; 
            }
            
            char formateret_navn[512];
            if (er_vindue_minimeret(vinduer[i])) snprintf(formateret_navn, sizeof(formateret_navn), "[%s]", navn);
            else snprintf(formateret_navn, sizeof(formateret_navn), " %s ", navn);

            XmString xm_navn = opret_utf8_xmstring(formateret_navn);
            Window *win_alloc = malloc(sizeof(Window)); *win_alloc = vinduer[i];

            Widget knap = XtVaCreateManagedWidget("task_knap", xmPushButtonWidgetClass, task_list, XmNlabelString, xm_navn, XmNrecomputeSize, False, XmNwidth, 120, XmNuserData, win_alloc, NULL);
            XmStringFree(xm_navn); free(navn);

            /* Dynamisk parring af det officielle farve-ikon direkte på pushbælten */
            int vindues_ikon_str = panel_hojde - 14; 
            Pixmap v_ikon = hent_vindues_ikon(vinduer[i], vindues_ikon_str);
            if (v_ikon != None) {
                XtVaSetValues(knap,
                    XmNlabelType, XmPIXMAP_AND_STRING,
                    XmNlabelPixmap, v_ikon,
                    NULL);
            }

            Widget knap_menu = XmCreatePopupMenu(knap, "menu_skal", NULL, 0);
            for (long m = 0; m < total_skriveborde; m++) {
                char menu_label_streng[64]; snprintf(menu_label_streng, sizeof(menu_label_streng), "Flyt til skrivebord %ld", m + 1);
                XmString xm_mstr = XmStringCreateLocalized(menu_label_streng);
                char item_name[32]; snprintf(item_name, sizeof(item_name), "menu_item_%ld", m + 1);

                Widget m_punkt = XtVaCreateManagedWidget(item_name, xmPushButtonWidgetClass, knap_menu, XmNlabelString, xm_mstr, NULL);
                XmStringFree(xm_mstr);

                unsigned long pakke = (vinduer[i] << 8) | (m & 0xFF);
                XtAddCallback(m_punkt, XmNactivateCallback, flyt_vindue_menu_cb, (XtPointer)pakke);
            }
            XtAddCallback(knap, XmNactivateCallback, aktiver_vindue_cb, (XtPointer)vinduer[i]);
            XtAddEventHandler(knap, ButtonPressMask, False, vindue_knap_popup_handler, (XtPointer)knap_menu);

            /* ===================================================================
               KRITISK SIKKERHED: Fejlfælde mod race-conditions (BadWindow)
               =================================================================== */
            /* 1. Slå X11 standard-fejlhåndtering fra midlertidigt */
            XSetErrorHandler(NULL); 
            XSync(display, False); /* Tvinger X-serveren til at synkronisere */

            /* 2. Kør de kald, der risikerer at ramme et "dødt" vindue */
            XSelectInput(display, vinduer[i], PropertyChangeMask);

            /* 3. Tænd for standard-fejlhåndtering igen med det samme */
            XSync(display, False);
            /* X11 standard error handler genetableres automatisk ved at sende NULL 
               eller lade Xt-miljøet håndtere det (eller en tom dummyfunktion) */
        }
        XFree(data);
    }
    WidgetList p_knapper; Cardinal a_pager; XtVaGetValues(pager_list, XmNchildren, &p_knapper, XmNnumChildren, &a_pager, NULL);
    for (int i = 0; i < (int)a_pager; i++) {
        XtVaSetValues(p_knapper[i], XmNshadowThickness, (i == aktuelt_skrivebord) ? 4 : 2, NULL);
    }
}
void ql_dialog_genindlaes_liste() {
    if (!ql_list_widget) return;
    XmListDeleteAllItems(ql_list_widget);
    for (int i = 0; i < quick_items_antal; i++) {
        XmString xm_str = opret_utf8_xmstring(quick_items[i].navn);
        XmListAddItemUnselected(ql_list_widget, xm_str, 0); XmStringFree(xm_str);
    }
}

void ql_list_valgt_cb(Widget w, XtPointer client_data, XtPointer call_data) {
    XmListCallbackStruct *cbs = (XmListCallbackStruct *)call_data;
    int indeks = cbs->item_position - 1;
    if (indeks >= 0 && indeks < quick_items_antal) {
        XmTextFieldSetString(ql_text_navn, quick_items[indeks].navn);
        XmTextFieldSetString(ql_text_exec, quick_items[indeks].exec_cmd);
        XmTextFieldSetString(ql_text_ikon, quick_items[indeks].ikon_sti);
    }
}

void ql_knap_tilfoej_cb(Widget w, XtPointer client_data, XtPointer call_data) {
    if (quick_items_antal >= MAX_QUICK_ITEMS) return;
    char *navn = XmTextFieldGetString(ql_text_navn);
    char *exec = XmTextFieldGetString(ql_text_exec);
    char *ikon = XmTextFieldGetString(ql_text_ikon);

    if (navn && strlen(navn) > 0 && exec && strlen(exec) > 0) {
        strncpy(quick_items[quick_items_antal].navn, navn, 63);
        strncpy(quick_items[quick_items_antal].exec_cmd, exec, 255);
        strncpy(quick_items[quick_items_antal].ikon_sti, ikon ? ikon : "", 255);
        quick_items_antal++;
        gem_konfiguration(); opdater_quick_launch_bar(); ql_dialog_genindlaes_liste();
        XmTextFieldSetString(ql_text_navn, ""); XmTextFieldSetString(ql_text_exec, ""); XmTextFieldSetString(ql_text_ikon, "");
    }
    if (navn) { XtFree(navn); } 
    if (exec) { XtFree(exec); } 
    if (ikon) { XtFree(ikon); }
}

void ql_knap_opdater_cb(Widget w, XtPointer client_data, XtPointer call_data) {
    int *pos_list = NULL, pos_count = 0;
    if (XmListGetSelectedPos(ql_list_widget, &pos_list, &pos_count) && pos_count > 0) {
        /* RETTELSE: Ændret fra pos_list - 1 til pos_list[0] - 1 */
        int indeks = pos_list[0] - 1;

        char *navn = XmTextFieldGetString(ql_text_navn);
        char *exec = XmTextFieldGetString(ql_text_exec);
        char *ikon = XmTextFieldGetString(ql_text_ikon);

        if (navn && strlen(navn) > 0 && exec && strlen(exec) > 0) {
            strncpy(quick_items[indeks].navn, navn, 63);
            strncpy(quick_items[indeks].exec_cmd, exec, 255);
            strncpy(quick_items[indeks].ikon_sti, ikon ? ikon : "", 255);
            gem_konfiguration(); opdater_quick_launch_bar(); ql_dialog_genindlaes_liste();
        }
        if (navn) { XtFree(navn); } 
        if (exec) { XtFree(exec); } 
        if (ikon) { XtFree(ikon); }
        free(pos_list);
    }
}

void ql_knap_slet_cb(Widget w, XtPointer client_data, XtPointer call_data) {
    int *pos_list = NULL, pos_count = 0;
    if (XmListGetSelectedPos(ql_list_widget, &pos_list, &pos_count) && pos_count > 0) {
        /* RETTELSE: Ændret fra pos_list - 1 til pos_list[0] - 1 */
        int indeks = pos_list[0] - 1;
        
        for (int i = indeks; i < quick_items_antal - 1; i++) quick_items[i] = quick_items[i + 1];
        quick_items_antal--;
        gem_konfiguration(); opdater_quick_launch_bar(); ql_dialog_genindlaes_liste();
        XmTextFieldSetString(ql_text_navn, ""); XmTextFieldSetString(ql_text_exec, ""); XmTextFieldSetString(ql_text_ikon, "");
        free(pos_list);
    }
}

void opdater_ur(XtPointer client_data, XtIntervalId *id) {
    time_t nu; struct tm *tids_info; char tids_streng[16];
    time(&nu); tids_info = localtime(&nu); strftime(tids_streng, sizeof(tids_streng), "%H:%M:%S", tids_info);
    XmString xm_streng = opret_utf8_xmstring(tids_streng);
    XtVaSetValues(ur_label, XmNlabelString, xm_streng, NULL); XmStringFree(xm_streng);
    XtAppAddTimeOut(app_context, 1000, opdater_ur, NULL);
}

void start_applikation(Widget w, XtPointer client_data, XtPointer call_data) {
    char *kommando = (char *)client_data;
    if (fork() == 0) { execl("/bin/sh", "sh", "-c", kommando, NULL); exit(1); }
}

void generer_menu_niveau(MenuCacheDir *dir, Widget parent_menu) {
    GSList *l; GSList *children = menu_cache_dir_list_children(dir);
    for (l = children; l != NULL; l = l->next) {
        MenuCacheItem *item = MENU_CACHE_ITEM(l->data);
        MenuCacheType type = menu_cache_item_get_type(item);
        if (type == MENU_CACHE_TYPE_APP) {
            MenuCacheApp *app = MENU_CACHE_APP(item);
            const char *navn = menu_cache_item_get_name(item);
            const char *exec_cmd = menu_cache_app_get_exec(app);
            if (navn && exec_cmd) {
                char *ren_cmd = strdup(exec_cmd); char *procent = strchr(ren_cmd, '%'); if (procent) *procent = '\0';
                XmString xm_navn = opret_utf8_xmstring((char *)navn);
                Widget m_punkt = XtVaCreateManagedWidget("xdg_app", xmPushButtonWidgetClass, parent_menu, XmNlabelString, xm_navn, NULL);
                XmStringFree(xm_navn); XtAddCallback(m_punkt, XmNactivateCallback, start_applikation, (XtPointer)ren_cmd);
            }
        } else if (type == MENU_CACHE_TYPE_DIR) {
            MenuCacheDir *sub_dir = MENU_CACHE_DIR(item); const char *kat_navn = menu_cache_item_get_name(item);
            if (kat_navn) {
                Widget under_menu_skal = XmCreatePulldownMenu(parent_menu, "under_menu", NULL, 0);
                XmString xm_kat = opret_utf8_xmstring((char *)kat_navn);
                Widget cascade_btn = XtVaCreateManagedWidget("xdg_kategori", xmCascadeButtonWidgetClass, parent_menu, XmNlabelString, xm_kat, XmNsubMenuId, under_menu_skal, NULL);
                XmStringFree(xm_kat); (void)cascade_btn; generer_menu_niveau(sub_dir, under_menu_skal);
            }
        }
    }
}

void byg_xdg_applikations_menu(Widget menu_skal) {
    MenuCache *cache = menu_cache_lookup_sync("applications.menu"); if (!cache) return;
    MenuCacheDir *root_dir = menu_cache_get_root_dir(cache);
    if (root_dir) generer_menu_niveau(root_dir, menu_skal);
    menu_cache_unref(cache);
}

void opdater_quick_launch_bar(void) {
    if (!quick_launch) return;
    WidgetList born = NULL; Cardinal antal_born = 0;
    XtVaGetValues(quick_launch, XmNchildren, &born, XmNnumChildren, &antal_born, NULL);
    if (born && antal_born > 0) {
        Widget *kopi = (Widget *)malloc(sizeof(Widget) * antal_born);
        for (int i = 0; i < antal_born; i++) kopi[i] = born[i];
        XtUnmanageChildren(kopi, antal_born); for (int i = 0; i < antal_born; i++) XtDestroyWidget(kopi[i]);
        free(kopi);
    }
    int ikon_str = panel_hojde - 12;
    for (int i = 0; i < quick_items_antal; i++) {
        Widget knap = XtVaCreateManagedWidget("ql_bar_button", xmPushButtonWidgetClass, quick_launch, XmNrecomputeSize, False, XmNwidth, panel_hojde, XmNheight, panel_hojde, NULL);
        Pixmap ikon = motif_grafik_indlaes_fil(quick_items[i].ikon_sti, ikon_str, ikon_str);
        if (ikon != None) { XtVaSetValues(knap, XmNlabelType, XmPIXMAP, XmNlabelPixmap, ikon, NULL); }
        else {
            char kort[4] = { quick_items[i].navn[0], quick_items[i].navn[0] ? quick_items[i].navn[1] : '\0', '\0', '\0' };
            XmString xm_kort = opret_utf8_xmstring(kort); XtVaSetValues(knap, XmNlabelString, xm_kort, NULL); XmStringFree(xm_kort);
        }
        XtAddCallback(knap, XmNactivateCallback, start_applikation, (XtPointer)strdup(quick_items[i].exec_cmd));
    }
}

void vis_quicklaunch_dialog(Widget parent) {
    if (ql_dialog) { XtManageChild(ql_dialog); ql_dialog_genindlaes_liste(); return; }
    Arg args[10]; int n = 0;
    XtSetArg(args[n], XmNtitle, "Indstil Hurtigstart"); n++;
    XtSetArg(args[n], XmNautoUnmanage, False); n++;
    ql_dialog = XmCreateFormDialog(parent, "ql_dialog", args, n);
    n = 0; XtSetArg(args[n], XmNvisibleItemCount, 10); n++;
    ql_list_widget = XmCreateScrolledList(ql_dialog, "ql_list", args, n); XtManageChild(ql_list_widget);
    XtAddCallback(ql_list_widget, XmNsingleSelectionCallback, ql_list_valgt_cb, NULL);
    Widget list_container = XtParent(ql_list_widget);
    XtVaSetValues(list_container, XmNleftAttachment, XmATTACH_FORM, XmNleftOffset, 10, XmNtopAttachment, XmATTACH_FORM, XmNtopOffset, 10, XmNbottomAttachment, XmATTACH_FORM, XmNbottomOffset, 50, XmNwidth, 150, NULL);
    Widget lbl_navn = XtVaCreateManagedWidget("Navn:", xmLabelWidgetClass, ql_dialog, XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, list_container, XmNleftOffset, 15, XmNtopAttachment, XmATTACH_FORM, XmNtopOffset, 15, NULL);
    ql_text_navn = XtVaCreateManagedWidget("txt_navn", xmTextFieldWidgetClass, ql_dialog, XmNleftAttachment, XmATTACH_OPPOSITE_WIDGET, XmNleftWidget, lbl_navn, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, lbl_navn, XmNwidth, 250, NULL);
    Widget lbl_exec = XtVaCreateManagedWidget("Kommando (Exec):", xmLabelWidgetClass, ql_dialog, XmNleftAttachment, XmATTACH_OPPOSITE_WIDGET, XmNleftWidget, ql_text_navn, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, ql_text_navn, XmNtopOffset, 10, NULL);
    ql_text_exec = XtVaCreateManagedWidget("txt_exec", xmTextFieldWidgetClass, ql_dialog, XmNleftAttachment, XmATTACH_OPPOSITE_WIDGET, XmNleftWidget, lbl_exec, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, lbl_exec, XmNwidth, 250, NULL);
    Widget lbl_ikon = XtVaCreateManagedWidget("Ikon-sti (PNG/SVG/XPM):", xmLabelWidgetClass, ql_dialog, XmNleftAttachment, XmATTACH_OPPOSITE_WIDGET, XmNleftWidget, ql_text_exec, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, ql_text_exec, XmNtopOffset, 10, NULL);
    ql_text_ikon = XtVaCreateManagedWidget("txt_ikon", xmTextFieldWidgetClass, ql_dialog, XmNleftAttachment, XmATTACH_OPPOSITE_WIDGET, XmNleftWidget, lbl_ikon, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, lbl_ikon, XmNwidth, 250, NULL);
    Widget btn_tilfoej = XtVaCreateManagedWidget("Tilføj", xmPushButtonWidgetClass, ql_dialog, XmNleftAttachment, XmATTACH_FORM, XmNleftOffset, 10, XmNbottomAttachment, XmATTACH_FORM, XmNbottomOffset, 10, NULL); XtAddCallback(btn_tilfoej, XmNactivateCallback, ql_knap_tilfoej_cb, NULL);
    Widget btn_opdater = XtVaCreateManagedWidget("Opdater", xmPushButtonWidgetClass, ql_dialog, XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, btn_tilfoej, XmNleftOffset, 10, XmNbottomAttachment, XmATTACH_FORM, XmNbottomOffset, 10, NULL); XtAddCallback(btn_opdater, XmNactivateCallback, ql_knap_opdater_cb, NULL);
    Widget btn_slet = XtVaCreateManagedWidget("Slet", xmPushButtonWidgetClass, ql_dialog, XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, btn_opdater, XmNleftOffset, 10, XmNbottomAttachment, XmATTACH_FORM, XmNbottomOffset, 10, NULL); XtAddCallback(btn_slet, XmNactivateCallback, ql_knap_slet_cb, NULL);
    XtManageChild(ql_dialog); ql_dialog_genindlaes_liste();
}

int main(int argc, char **argv) {
    Widget main_panel, toplevel; int skaer_bredde, skaer_hojde, skaerm;
    setlocale(LC_ALL, ""); XtSetLanguageProc(NULL, NULL, NULL);
    
    laes_konfiguration();
    toplevel = XtAppInitialize(&app_context, "MotifPanel", NULL, 0, &argc, argv, NULL, NULL, 0);
    display = XtDisplay(toplevel); skaerm = DefaultScreen(display);
    skaer_bredde = DisplayWidth(display, skaerm); skaer_hojde = DisplayHeight(display, skaerm);

    int panel_y = 0;
    if (strcmp(panel_position, "bottom") == 0) panel_y = skaer_hojde - panel_hojde;

    XtVaSetValues(toplevel, XmNwidth, skaer_bredde, XmNheight, panel_hojde, XmNx, 0, XmNy, panel_y, XmNmwmDecorations, 0, XmNmwmFunctions, 0, NULL);
    XtRealizeWidget(toplevel);

    XtVaGetValues(toplevel, XmNrenderTable, &minXftTable, NULL);
    motif_grafik_init(display, DefaultVisual(display, skaerm), DefaultColormap(display, skaerm));

    Window win = XtWindow(toplevel); Atom wm_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom type_dock = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DOCK", False);
    XChangeProperty(display, win, wm_type, XA_ATOM, 32, PropModeReplace, (unsigned char *)&type_dock, 1);

    Atom wm_strut = XInternAtom(display, "_NET_WM_STRUT_PARTIAL", False); unsigned long struts[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    if (strcmp(panel_position, "top") == 0) { struts[2] = panel_hojde; struts[8] = 0; struts[9] = skaer_bredde; }
    else { struts[3] = panel_hojde; struts[10] = 0; struts[11] = skaer_bredde; }
    XChangeProperty(display, win, wm_strut, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)struts, 12);

    char tray_atom_navn[32]; snprintf(tray_atom_navn, sizeof(tray_atom_navn), "_NET_SYSTEM_TRAY_S%d", skaerm);
    Atom tray_selection_atom = XInternAtom(display, tray_atom_navn, False); XSetSelectionOwner(display, tray_selection_atom, win, CurrentTime);
    if (XGetSelectionOwner(display, tray_selection_atom) == win) {
        XEvent xev; Atom manager_atom = XInternAtom(display, "MANAGER", False); memset(&xev, 0, sizeof(xev));
        xev.xclient.type = ClientMessage; xev.xclient.window = DefaultRootWindow(display);
        xev.xclient.message_type = manager_atom; xev.xclient.format = 32;
        xev.xclient.data.l[0] = CurrentTime; xev.xclient.data.l[1] = tray_selection_atom; xev.xclient.data.l[2] = win;
        XSendEvent(display, DefaultRootWindow(display), False, StructureNotifyMask, &xev); XFlush(display);
        printf("MotifPanel har overtaget %s ejerskab.\n", tray_atom_navn);
    }
    XtAddEventHandler(toplevel, SubstructureNotifyMask, False, systray_xembed_handler, NULL);
g_bus_own_name(G_BUS_TYPE_SESSION, "org.freedesktop.StatusNotifierWatcher", G_BUS_NAME_OWNER_FLAGS_REPLACE, on_bus_acquired, on_name_acquired, on_name_lost, NULL, NULL);
g_bus_own_name(G_BUS_TYPE_SESSION, "org.kde.StatusNotifierWatcher", G_BUS_NAME_OWNER_FLAGS_REPLACE, on_bus_acquired, on_name_acquired, on_name_lost, NULL, NULL);
main_panel = XtVaCreateManagedWidget("main_panel", xmFormWidgetClass, toplevel, XmNorientation, XmHORIZONTAL, NULL);
Widget menu_bar = XmCreateMenuBar(main_panel, "menu_bar", NULL, 0);
XtVaSetValues(menu_bar, XmNleftAttachment, XmATTACH_FORM, XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, XmNshadowThickness, 2, XmNrenderTable, minXftTable, NULL);
XtManageChild(menu_bar);
Widget menu_skal = XmCreatePulldownMenu(menu_bar, "menu_skal", NULL, 0);
byg_xdg_applikations_menu(menu_skal);
XmString xm_ql_str = opret_utf8_xmstring("Indstil Hurtigstart...");
Widget ql_item = XtVaCreateManagedWidget("menu_ql_setup", xmPushButtonWidgetClass, menu_skal, XmNlabelString, xm_ql_str, NULL);
XmStringFree(xm_ql_str); XtAddCallback(ql_item, XmNactivateCallback, (XtCallbackProc)vis_quicklaunch_dialog, NULL);
XtVaCreateManagedWidget("separator", xmSeparatorWidgetClass, menu_skal, NULL);
XmString xm_exit_str = opret_utf8_xmstring("Afslut Panel");
Widget exit_punkt = XtVaCreateManagedWidget("menu_exit_punkt", xmPushButtonWidgetClass, menu_skal, XmNlabelString, xm_exit_str, NULL);
XmStringFree(xm_exit_str); XtAddCallback(exit_punkt, XmNactivateCallback, afslut_panel_cb, NULL);
XmString menu_tekst = opret_utf8_xmstring(" Menu ");
menu_knap = XtVaCreateManagedWidget("menu_knap", xmCascadeButtonWidgetClass, menu_bar, XmNlabelString, menu_tekst, XmNsubMenuId, menu_skal, NULL);
XmStringFree(menu_tekst);
pager_list = XtVaCreateManagedWidget("pager_list", xmRowColumnWidgetClass, main_panel, XmNorientation, XmHORIZONTAL, XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, menu_bar, XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, NULL);
total_skriveborde = hent_total_skriveborde();
for (long i = 0; i < total_skriveborde; i++) {
char pager_navn[16]; snprintf(pager_navn, sizeof(pager_navn), "%ld", i + 1); XmString xm_pnavn = opret_utf8_xmstring(pager_navn);
Widget pknap = XtVaCreateManagedWidget("pager_knap", xmPushButtonWidgetClass, pager_list, XmNlabelString, xm_pnavn, XmNwidth, 24, XmNrecomputeSize, False, NULL);
XmStringFree(xm_pnavn); XtAddCallback(pknap, XmNactivateCallback, skift_skrivebord_cb, (XtPointer)i);
}
quick_launch = XtVaCreateManagedWidget("quick_launch", xmRowColumnWidgetClass, main_panel, XmNorientation, XmHORIZONTAL, XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, pager_list, XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, NULL);
XmString initial_tid = opret_utf8_xmstring("--:--:--");
ur_label = XtVaCreateManagedWidget("ur_label", xmLabelWidgetClass, main_panel, XmNrightAttachment, XmATTACH_FORM, XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, XmNlabelString, initial_tid, NULL);
XmStringFree(initial_tid);
tray_area = XtVaCreateManagedWidget("tray_area", xmRowColumnWidgetClass, main_panel, XmNorientation, XmHORIZONTAL, XmNrightAttachment, XmATTACH_WIDGET, XmNrightWidget, ur_label, XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, XmNwidth, 40, NULL);
task_list = XtVaCreateManagedWidget("task_list", xmRowColumnWidgetClass, main_panel, XmNorientation, XmHORIZONTAL, XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, quick_launch, XmNrightAttachment, XmATTACH_WIDGET, XmNrightWidget, tray_area, XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, NULL);
XSelectInput(display, DefaultRootWindow(display), PropertyChangeMask);
Atom client_list_atom_real = XInternAtom(display, "_NET_CLIENT_LIST", False);
Atom current_desktop_atom = XInternAtom(display, "_NET_CURRENT_DESKTOP", False);
Atom wm_name_atom = XInternAtom(display, "_NET_WM_NAME", False);
Atom wm_desktop_atom = XInternAtom(display, "_NET_WM_DESKTOP", False);
opdater_vindues_liste(); opdater_quick_launch_bar(); XtAppAddTimeOut(app_context, 1000, opdater_ur, NULL);
XEvent event;
while (1) {
while (g_main_context_iteration(NULL, FALSE));
if (XtAppPending(app_context)) {
XtAppNextEvent(app_context, &event);
if (event.type == PropertyNotify) {
if (event.xproperty.atom == client_list_atom_real ||
event.xproperty.atom == current_desktop_atom ||
event.xproperty.atom == wm_name_atom ||
event.xproperty.atom == wm_desktop_atom) {
opdater_vindues_liste();
}
}
XtDispatchEvent(&event);
} else { usleep(1000); }
}
return 0;
}
