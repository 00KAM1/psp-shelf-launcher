/* Draws the "Starting game" screen for one game of the picture stick: once as it is,
 * and once with the game's file out of reach, which is the half-size copy from the cache. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/common.h"
#include "../../src/scan.h"
#include "../../src/data.h"
#include "../../src/ui.h"
u8 msx[2048];
static u32 g_fb[512 * 272];
static ShSurf g_s = { g_fb, 512 };
static void shot(const char *name)
{
    char p[128]; snprintf(p, sizeof(p), "out/%s.rgba", name);
    FILE *f = fopen(p, "wb");
    for (int y = 0; y < 272; y++) fwrite(g_fb + y * 512, 4, 480, f);
    fclose(f);
}
int main(int argc, char **argv)
{
    FILE *f = fopen("msx.bin", "rb"); fread(msx, 1, 2048, f); fclose(f);
    ShLibrary lib = { 0 };
    u32 now = sh_minutes(2026, 11, 24, 14, 5);
    scan_library(&lib, "ms0:/PSP/GAME/SHELF/EBOOT.PBP", now, 0, NULL);
    ShInfo *info = calloc(lib.n, sizeof(ShInfo));
    data_load_all(&lib, info, now);
    ShConfig cfg; config_load(&cfg);
    static ShUi ui;
    ui.lib = &lib; ui.info = info; ui.cfg = &cfg; ui.now = now;
    ui_init(&ui);
    int g = -1;
    for (int i = 0; i < lib.n; i++) if (strstr(lib.g[i].path, argv[1])) g = i;
    if (g < 0) return 2;
    ui_draw_launch(&ui, &g_s, g); shot("launch-full");
    char real[512], away[512];
    snprintf(real, sizeof(real), "ms0/%s", lib.g[g].path + 5); snprintf(away, sizeof(away), "%s.away", real);
    rename(real, away);
    ui_draw_launch(&ui, &g_s, g); shot("launch-cache");
    rename(away, real);
    return 0;
}
