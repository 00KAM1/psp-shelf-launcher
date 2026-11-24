/*
 * Shelf - main loop on the PSP: screen, buttons, and the actions the screens ask for.
 */
#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <psppower.h>
#include <psprtc.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "scan.h"
#include "data.h"
#include "ui.h"
#include "sys.h"

PSP_MODULE_INFO("Shelf", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

/* ---- HOME button and power ---------------------------------------------------- */
static volatile int g_quit;
static int exit_cb(int a, int b, void *c) { g_quit = 1; return 0; }
static int cb_thread(SceSize args, void *argp)
{
    int id = sceKernelCreateCallback("Exit Callback", exit_cb, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}

/* ---- the screen: drawn in main RAM, then copied to the video-memory page that is
 * not on show, and the two pages swapped at the next frame. Video memory is where
 * every PSP program shows pictures from, and copying a finished frame means a
 * half-drawn one is never seen. ------------------------------------------------- */
static u32 g_draw[512 * 272] __attribute__((aligned(64)));
static int g_back;

static ShSurf surface(void) { ShSurf s = { g_draw, 512 }; return s; }

static void flip(void)
{
    u32 *page = sys_vram(g_back);
    memcpy(page, g_draw, sizeof(g_draw));
    sceDisplaySetFrameBuf(page, 512, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
    sceDisplayWaitVblankStart();
    g_back ^= 1;
}

static u32 now_minutes(void)
{
    ScePspDateTime t;
    memset(&t, 0, sizeof(t));
    sceRtcGetCurrentClockLocalTime(&t);
    return sh_minutes(t.year, t.month, t.day, t.hour, t.minute);
}

static void clock_text(char *out, int size)
{
    ScePspDateTime t;
    memset(&t, 0, sizeof(t));
    sceRtcGetCurrentClockLocalTime(&t);
    int b = scePowerGetBatteryLifePercent();
    if (b >= 0) sh_snprintf(out, size, "%02d:%02d  %sBATT %d%%", t.hour, t.minute, scePowerIsBatteryCharging() ? "+" : "", b);
    else sh_snprintf(out, size, "%02d:%02d", t.hour, t.minute);
}

/* ---- since the PSP was powered on: time and battery (the PS-button menu shows them) ---- */
static ShUptime g_up;
static ShUi g_ui;                              /* defined with the rest of the state, below */

static void uptime_show(void)
{
    u32 now = now_minutes();
    g_ui.up_hide = g_up.bad;
    g_ui.up_mins = now > g_up.start ? now - g_up.start : 0;
    g_ui.up_sleep = g_up.plugin && g_up.asleep < g_ui.up_mins ? g_up.asleep : 0;
    g_ui.up_mins -= g_ui.up_sleep;
    g_ui.up_pct0 = g_up.start_pct;
    g_ui.up_pct = scePowerGetBatteryLifePercent();
    if (g_ui.up_pct < 0 || g_ui.up_pct > 100) g_ui.up_pct = -1;
}

/* coming_back = 1: Shelf hands over and expects to be started again in this power-on */
static void uptime_save(int coming_back)
{
    uptime_show();
    uptime_mark(&g_up, now_minutes(), g_ui.up_pct, coming_back);
}

/* ---- buttons: presses, plus repeats for held directions ---------------------------- */
#define DIRS (B_UP | B_DOWN | B_LEFT | B_RIGHT | B_AUP | B_ADOWN | B_ALEFT | B_ARIGHT)
static u32 read_input(void)
{
    static u32 prev, since, last;
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(&pad, 1);
    u32 now = pad.Buttons & 0xF3F9, t = sceKernelGetSystemTimeLow();
    if (sys_home_down()) now |= B_HOME;
    /* the analog stick, as its own four directions */
    if (pad.Lx < 40) now |= B_ALEFT; else if (pad.Lx > 215) now |= B_ARIGHT;
    if (pad.Ly < 40) now |= B_AUP; else if (pad.Ly > 215) now |= B_ADOWN;
    u32 edge = now & ~prev;
    prev = now;
    if (edge) { since = last = t; return edge; }
    /* held: repeats after 0.4 s, and twice as fast once held for 1.5 s */
    u32 rep = now & DIRS;
    u32 gap = t - since > 1500 * 1000 ? 35 * 1000 : 70 * 1000;
    if (rep && t - since > 400 * 1000 && t - last > gap) { last = t; return rep; }
    return 0;
}

static void wait_release(void)
{
    SceCtrlData pad;
    do { sceKernelDelayThread(16 * 1000); sceCtrlPeekBufferPositive(&pad, 1); } while ((pad.Buttons & 0xF3F9) || sys_home_down());
}

/* ---- loading ------------------------------------------------------------------------ */
static ShUi g_ui;
static ShLibrary g_lib;
static ShInfo *g_info;
static ShConfig g_cfg;
static const char *g_prog_title = "Finding your games";

static void progress(int done, int total, const char *name)
{
    static u32 last;
    u32 t = sceKernelGetSystemTimeLow();
    if (done && done < total && t - last < 100 * 1000) return;   /* the screen at most 10 times a second */
    last = t;
    ShSurf s = surface();
    ui_draw_progress(&g_ui, &s, g_prog_title, done, total, name);
    flip();
}

static void load_library(int force)
{
    scan_free(&g_lib);
    g_prog_title = force ? "Reading every game's pictures" : "Finding your games";
    sys_cpu_fast(1);
    scan_library(&g_lib, g_ui.self, now_minutes(), force, progress);
    free(g_info);
    g_info = calloc(g_lib.n + 1, sizeof(ShInfo));
    data_load_all(&g_lib, g_info, now_minutes());
    sys_cpu_fast(0);
    g_ui.lib = &g_lib;
    g_ui.info = g_info;
    if (sys_ms_space(&g_ui.ms_free_mb, &g_ui.ms_total_mb) < 0) g_ui.ms_free_mb = g_ui.ms_total_mb = 0;
}

/* Where you are goes into config.txt, so Shelf can open there again after a game. */
static void save_place(void)
{
    ui_remember(&g_ui);
    config_save(&g_cfg);
}

static void usb_mode(void)
{
    sys_home_release();                          /* in USB mode the PS button is the system's */
    sys_usb_start();
    g_ui.overlay = OV_USB;
    int last = -1;
    for (;;) {
        g_ui.usb_state = sys_usb_state();
        if (g_ui.usb_state != last) { ShSurf s = surface(); ui_draw(&g_ui, &s); flip(); last = g_ui.usb_state; }
        sceKernelDelayThread(50 * 1000);
        if (read_input() & B_CIRCLE) break;
        if (g_quit) break;
    }
    sys_usb_stop();
    g_ui.overlay = g_ui.usb_from_quick ? OV_NONE : OV_SETTINGS;
    load_library(0);
    ui_rebuild(&g_ui);
}

int main(int argc, char **argv)
{
    SceUID th = sceKernelCreateThread("ShelfCallbacks", cb_thread, 0x11, 0xFA0, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    sys_home_init();
    sceDisplaySetMode(0, 480, 272);

    sh_mkdirs();
    SceIoStat st;
    if (sceIoGetstat(SH_LOG_PATH, &st) >= 0 && st.st_size > 32 * 1024) sceIoRemove(SH_LOG_PATH);
    memset(&g_ui, 0, sizeof(g_ui));
    sh_strcpy(g_ui.self, sizeof(g_ui.self), argc > 0 && argv[0] ? argv[0] : "ms0:/PSP/GAME/SHELF/EBOOT.PBP");
    g_ui.has_ark = sys_has_ark();
    sys_ark_dir(g_ui.ark_dir, sizeof(g_ui.ark_dir));
    int as_launcher = sh_ends_with(g_ui.self, "/VBOOT.PBP");
    sh_log("Shelf " SH_VERSION_STR " (built " SH_BUILD_STAMP ") from %s, ARK %s at %s%s", g_ui.self,
           g_ui.has_ark ? "found" : "NOT found", g_ui.ark_dir, as_launcher ? ", as the launcher" : "");
    sq_resume_request(NULL);                     /* a request no game picked up must not wait for the next one */
    if (g_ui.has_ark && launcher_return_done(g_ui.ark_dir)) sh_log("back from a game: ARK's launcher setting is off again");
    if (g_ui.has_ark && launcher_resume_if_paused(g_ui.ark_dir)) sh_log("launcher mode turned back on");
    g_ui.as_launcher = as_launcher;
    if (g_ui.has_ark && !as_launcher && launcher_takeover(g_ui.ark_dir, g_ui.self)) sh_log("ARK's launcher was another program: Shelf is VBOOT.PBP now");
    if (g_ui.has_ark && !as_launcher && launcher_update(g_ui.ark_dir, g_ui.self)) sh_log("the start-up copy (VBOOT.PBP) was brought up to date");

    if (g_ui.has_ark && !as_launcher) {
        /* Opened from Game > Memory Stick: carry on as the launcher copy. Holding L while
         * Shelf starts skips this, in case that copy ever fails to come up. */
        SceCtrlData pad;
        char vb[160];
        memset(&pad, 0, sizeof(pad));
        sceCtrlPeekBufferPositive(&pad, 1);
        if (pad.Buttons & PSP_CTRL_LTRIGGER) sh_log("L held: staying in the Game-menu copy");
        else if (launcher_ready(g_ui.ark_dir, g_ui.self, vb, sizeof(vb))) {
            sh_log("opened from the Game menu: handing over to the launcher copy");
            sceKernelDelayThread(50 * 1000);
            int r = sys_run_program(vb);
            sh_log("hand-over failed (%08X): staying in the Game-menu copy", r);
        } else sh_log("the launcher copy could not be put in place: staying in the Game-menu copy");
    }

    config_load(&g_cfg);
    g_ui.cfg = &g_cfg;
    g_ui.now = now_minutes();
    g_ui.rng = sceKernelGetSystemTimeLow();
    clock_text(g_ui.clock, sizeof(g_ui.clock));
    { int p = scePowerGetBatteryLifePercent(); uptime_begin(&g_up, now_minutes(), p >= 0 && p <= 100 ? p : -1); uptime_show(); }
    load_library(0);
    ui_init(&g_ui);

    u32 last_clock = 0, last_up = sceKernelGetSystemTimeLow();
    u32 last_runtime = sceKernelGetSystemTimeLow();
    int redraw = 1;
    while (!g_quit) {
        u32 t = sceKernelGetSystemTimeLow();
        if (t - last_clock > 5 * 1000 * 1000) {          /* clock and battery */
            last_clock = t;
            char c[24];
            clock_text(c, sizeof(c));
            if (strcmp(c, g_ui.clock)) { strcpy(g_ui.clock, c); redraw = 1; }
            g_ui.now = now_minutes();
            if (t - last_runtime > 60 * 1000 * 1000u) {      /* is the battery plugin keeping the record? then follow it */
                last_runtime = t;
                int was = g_up.plugin;
                if (!uptime_refresh(&g_up, g_ui.now) && was) g_up.asleep = 0;
            }
            uptime_show();
            if (g_ui.overlay == OV_QUICK) redraw = 1;
            if (t - last_up > 5 * 60 * 1000 * 1000u) { last_up = t; uptime_save(0); }     /* every 5 minutes, so a power-off loses little */
        }
        u32 b = read_input();
        int act = ui_input(&g_ui, b);
        if (act != ACT_NONE || g_ui.dirty) redraw = 1;
        switch (act) {
        case ACT_LAUNCH: {
            int g = g_ui.act_game;
            save_place();
            g_info[g].launched = now_minutes();
            data_finish(&g_lib.g[g], &g_info[g]);
            state_save(&g_lib, g_info);
            ShSurf s = surface();
            ui_draw_launch(&g_ui, &s, g);
            flip();
            wait_release();
            sceKernelDelayThread(300 * 1000);
            /* From Playing, a game with a Sidequest slot goes straight back to it; from
             * All and Finished Sidequest asks first, as it does when started anywhere else. */
            int straight = g_ui.tab == 0 && g_info[g].sq_newest >= 0 && g_lib.g[g].type != GT_PS1;
            int from = g_ui.act_from, slot = g_ui.act_slot;         /* "Start from..." chose a slot, or none */
            g_ui.act_from = 0;
            if (from) straight = 0;
            sq_resume_request(straight ? g_lib.g[g].path : NULL);
            if (from) { sq_start_request(g_lib.g[g].path, from == 1 ? slot : -1); sh_log("start from: %s, %s", g_lib.g[g].path, from == 1 ? (slot ? "a state" : "suspend") : "without loading"); }
            int armed = g_cfg.return_to_shelf && g_ui.has_ark ? launcher_return_arm(g_ui.ark_dir, g_ui.self) : 0;
            sh_log("start: %s%s%s", g_lib.g[g].path, straight ? " (resume without asking)" : "", armed > 0 ? " (comes back to Shelf)" : "");
            uptime_save(1);
            sys_home_release();
            int r = sys_launch(&g_lib.g[g]);
            sq_resume_request(NULL);
            if (armed > 0) launcher_return_done(g_ui.ark_dir);
            sh_log("start failed: %08X", r);
            ui_message(&g_ui, "The game could not be started.", g_ui.has_ark ? "See ms0:/SHELF/shelf.log" : "ARK-5 was not found.", NULL);
            break;
        }
        case ACT_XMB:
            save_place();
            uptime_save(1);
            sys_home_release();
            if (as_launcher && g_ui.has_ark) launcher_pause(g_ui.ark_dir);
            sys_exit_xmb(as_launcher);
            break;
        case ACT_USB: usb_mode(); break;
        case ACT_SLEEP: uptime_save(0); sys_home_release(); sys_sleep(); break;
        case ACT_RESTART: save_place(); uptime_save(1); sys_home_release(); sys_restart(); break;
        case ACT_SHUTDOWN: save_place(); uptime_save(0); sys_home_release(); sys_shutdown(); break;
        case ACT_RECOVERY: sys_recovery(g_ui.ark_dir); break;
        case ACT_RESCAN:
        case ACT_REBUILD:
            load_library(act == ACT_REBUILD);
            g_ui.overlay = OV_SETTINGS;
            ui_rebuild(&g_ui);
            break;
        }
        if (redraw) {
            ShSurf s = surface();
            ui_draw(&g_ui, &s);
            flip();
            redraw = 0;
        } else if (!b && ui_idle(&g_ui)) sceKernelDelayThread(2 * 1000);   /* got a picture ready for the next page */
        else sceKernelDelayThread(16 * 1000);
    }
    save_place();
    sceKernelExitGame();
    return 0;
}
