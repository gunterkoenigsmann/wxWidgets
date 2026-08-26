// Drive a pointer under KWin well enough to perform a drag.
//
//     kwdrag move 360 340  down  move 400 380  move 500 450  up
//
// The wlroots virtual-pointer protocol that wldrag.c uses does not exist on
// KWin, which offers org_kde_kwin_fake_input instead. The two are close
// enough in shape to keep the same command line, so a driver script can pick
// one by compositor and change nothing else.
//
// Coordinates here are absolute global pixels and are sent as-is: unlike the
// wlroots protocol there is no extent to get wrong, because the compositor
// interprets them in its own global space.
#include <wayland-client.h>
#include "fake-input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BTN_LEFT 0x110

static struct org_kde_kwin_fake_input* fake;

static void reg_global(void*, struct wl_registry* r, uint32_t name,
                       const char* iface, uint32_t ver)
{
    if ( !strcmp(iface, org_kde_kwin_fake_input_interface.name) )
        fake = static_cast<struct org_kde_kwin_fake_input*>(
                wl_registry_bind(r, name, &org_kde_kwin_fake_input_interface,
                                 ver < 3 ? ver : 3));
}
static void reg_remove(void*, struct wl_registry*, uint32_t) {}
static const struct wl_registry_listener reg_l = { reg_global, reg_remove };

int main(int argc, char** argv)
{
    struct wl_display* d = wl_display_connect(NULL);
    if ( !d ) { fprintf(stderr, "no display\n"); return 1; }

    struct wl_registry* r = wl_display_get_registry(d);
    wl_registry_add_listener(r, &reg_l, NULL);
    wl_display_roundtrip(d);

    if ( !fake )
    {
        fprintf(stderr, "compositor has no org_kde_kwin_fake_input\n");
        return 1;
    }

    // KWin will not act on anything until this is sent.
    org_kde_kwin_fake_input_authenticate(fake, "wx docking test",
                                         "drive a drag for an automated test");
    wl_display_roundtrip(d);

    for ( int i = 1; i < argc; ++i )
    {
        if ( !strcmp(argv[i], "move") && i + 2 < argc )
        {
            const int x = atoi(argv[i+1]), y = atoi(argv[i+2]); i += 2;
            org_kde_kwin_fake_input_pointer_motion_absolute(
                    fake, wl_fixed_from_int(x), wl_fixed_from_int(y));
            fprintf(stderr, "move %d,%d\n", x, y);
        }
        else if ( !strcmp(argv[i], "down") || !strcmp(argv[i], "up") )
        {
            const int st = argv[i][0] == 'd' ? 1 : 0;
            org_kde_kwin_fake_input_button(fake, BTN_LEFT, st);
            fprintf(stderr, "%s\n", argv[i]);
        }
        else if ( !strcmp(argv[i], "sleep") && i + 1 < argc )
        {
            wl_display_flush(d);
            const int ms = atoi(argv[++i]);
            struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
            nanosleep(&ts, NULL);
            continue;
        }
        else { fprintf(stderr, "bad command: %s\n", argv[i]); return 2; }

        wl_display_flush(d);
    }

    wl_display_roundtrip(d);
    return 0;
}
