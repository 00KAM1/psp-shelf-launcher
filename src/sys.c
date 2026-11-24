/*
 * Shelf - what only the PSP can do: start a game, USB mode, power, the XMB.
 * Games are started the way ARK-5's own launcher (arkMenu) starts them:
 * the same apitypes, keys and ISO driver, so updates (PBOOT.PBP), patched
 * ISOs (EBOOT.OLD / plain BOOT.BIN), PSN and PS1 games all behave as there.
 */
#include <pspkernel.h>
#include <psppower.h>
#include <pspge.h>
#include <pspiofilemgr.h>
#include <pspiofilemgr_devctl.h>
#include <pspumd.h>
#include <pspusb.h>
#include <pspusbstor.h>
#include <pspctrl.h>
#include <psploadexec_kernel.h>
#include <kubridge.h>
#include <systemctrl.h>
#include <systemctrl_se.h>
#include <systemctrl_ark.h>
#include <string.h>
#include "common.h"
#include "iso.h"
#include "sys.h"

int scePowerRequestColdReset(int unk);
int scePowerRequestStandby(void);
int scePowerRequestSuspend(void);

#define RL_HOMEBREW   0x141
#define RL_ISO        0x123
#define RL_ISO_PBOOT  0x124
#define RL_POPS       0x144
#define DRV_PSN       0          /* MODE_UMD, as arkMenu */
#define DRV_ISO       3          /* as arkMenu (ARK picks Inferno) */

static char s_path[256], s_argp[256];

int sys_has_ark(void) { return sctrlSEGetVersion() > 0 || sctrlHENGetVersion() > 0; }

void sys_ark_dir(char *out, int size)
{
    ARKConfig c;
    memset(&c, 0, sizeof(c));
    sh_strcpy(out, size, "ms0:/PSP/SAVEDATA/ARK_01234/");
    if (!sys_has_ark()) return;
    sctrlArkGetConfig(&c);
    if (IS_ARK_CONFIG(&c) && c.arkpath[0] && !strstr(c.arkpath, "SEPLUGINS")) sh_strcpy(out, size, c.arkpath);
}

static int exists(const char *p) { SceIoStat st; memset(&st, 0, sizeof(st)); return sceIoGetstat(p, &st) >= 0; }

static void param_init(struct SceKernelLoadExecVSHParam *p, const char *key, const char *argp)
{
    memset(p, 0, sizeof(*p));
    p->size = sizeof(*p);
    p->key = (char *)key;
    p->argp = (void *)argp;
    p->args = strlen(argp) + 1;
}

int sys_launch(const ShGame *g)
{
    struct SceKernelLoadExecVSHParam param;
    if (!sys_has_ark()) return -1;
    sh_strcpy(s_path, sizeof(s_path), g->path);
    sh_log("start: %s (%s, type %d)", g->path, g->id, g->type);
    if (g->type == GT_ISO) {
        int apitype = RL_ISO;
        char pboot[96];
        sh_snprintf(pboot, sizeof(pboot), "ms0:/PSP/GAME/%s/PBOOT.PBP", g->id);
        if (g->id[0] && exists(pboot)) { sh_strcpy(s_argp, sizeof(s_argp), pboot); apitype = RL_ISO_PBOOT; }
        else {
            sh_strcpy(s_argp, sizeof(s_argp), "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN");
            ShIso iso;
            if (iso_open(&iso, s_path) == 0) {
                u32 off, size, magic = 0;
                if (iso_find(&iso, "PSP_GAME/SYSDIR/EBOOT.OLD", &off, &size) == 0)
                    sh_strcpy(s_argp, sizeof(s_argp), "disc0:/PSP_GAME/SYSDIR/EBOOT.OLD");
                else if (iso_find(&iso, "PSP_GAME/SYSDIR/BOOT.BIN", &off, &size) == 0 && size >= 4 &&
                         iso_read(&iso, off, &magic, 4) == 0 && magic != 0)
                    sh_strcpy(s_argp, sizeof(s_argp), "disc0:/PSP_GAME/SYSDIR/BOOT.BIN");
                iso_close(&iso);
            }
        }
        param_init(&param, "umdemu", s_argp);
        sctrlSESetDiscType(PSP_UMD_TYPE_GAME);
        sctrlSESetBootConfFileIndex(DRV_ISO);
        sctrlSESetUmdFile(s_path);
        return sctrlKernelLoadExecVSHWithApitype(apitype, s_path, &param);
    }
    if (g->type == GT_PSN) {
        int apitype = RL_ISO;
        sh_strcpy(s_argp, sizeof(s_argp), "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN");
        char pboot[256];
        sh_strcpy(pboot, sizeof(pboot), s_path);
        char *sl = strrchr(pboot, '/');
        if (sl) { strcpy(sl + 1, "PBOOT.PBP"); if (exists(pboot)) { sh_strcpy(s_argp, sizeof(s_argp), pboot); apitype = RL_ISO_PBOOT; } }
        param_init(&param, "umdemu", s_argp);
        sctrlSESetBootConfFileIndex(DRV_PSN);
        sctrlSESetUmdFile("");
        return sctrlKernelLoadExecVSHWithApitype(apitype, s_path, &param);
    }
    if (g->type == GT_PS1) {
        param_init(&param, "pops", s_path);
        return sctrlKernelLoadExecVSHWithApitype(RL_POPS, s_path, &param);
    }
    param_init(&param, "game", s_path);
    return sctrlKernelLoadExecVSHWithApitype(RL_HOMEBREW, s_path, &param);
}

/* ---- speed, space, screen memory ------------------------------------------------ */
void sys_cpu_fast(int on)
{
    static int cpu, bus;
    if (on) {
        if (cpu) return;
        cpu = scePowerGetCpuClockFrequencyInt();
        bus = scePowerGetBusClockFrequencyInt();
        if (cpu >= 333 || cpu <= 0) { cpu = 0; return; }       /* already fast (ARK's own setting) */
        scePowerSetClockFrequency(333, 333, 166);
    } else if (cpu) {
        scePowerSetClockFrequency(cpu, cpu, bus > 0 ? bus : cpu / 2);
        cpu = 0;
    }
}

int sys_ms_space(u32 *free_mb, u32 *total_mb)
{
    SceDevInf inf;
    SceDevctlCmd cmd = { &inf };
    memset(&inf, 0, sizeof(inf));
    if (sceIoDevctl("ms0:", SCE_PR_GETDEV, &cmd, sizeof(cmd), NULL, 0) < 0) return -1;
    u64 cluster = (u64)(u32)inf.sectorSize * (u32)inf.sectorCount;      /* bytes in a cluster */
    if (!cluster) return -1;
    *free_mb = (u32)(((u64)inf.freeClusters * cluster) >> 20);
    *total_mb = (u32)(((u64)inf.maxClusters * cluster) >> 20);
    return 0;
}

u32 *sys_vram(int page)
{
    return (u32 *)(0x40000000u | ((u32)sceGeEdramGetAddr() + (u32)page * 512 * 272 * 4));
}

/* ---- the PS button ------------------------------------------------------------------
 * In user mode the system hides this button: its bit only says the system's own "quit
 * the game?" screen is showing. psp5 and ARK's own menu get at it the same way, and so
 * does Shelf: the kernel's button read and the kernel's switch for that screen are found
 * with sctrlHENFindFunction and called in kernel mode with kuKernelCall.
 * If Shelf ever stops reading buttons with the system screen switched off, a watchdog
 * thread switches it back on, so the PS button can always get you out. */
static void *s_peek, *s_popup;
static volatile int s_popup_off;                  /* 1 = the system's quit screen is off, the button is ours */
static volatile u32 s_heartbeat;

static int kcall(void *f, u32 a1, u32 a2)
{
    struct KernelCallArg a;
    memset(&a, 0, sizeof(a));
    a.arg1 = a1;
    a.arg2 = a2;
    return kuKernelCall(f, &a) < 0 ? -1 : (int)a.ret1;
}

static void set_popup(int on)
{
    if (s_popup && kcall(s_popup, (u32)on, 0) >= 0) s_popup_off = !on;
}

static int home_watchdog(SceSize args, void *argp)
{
    (void)args; (void)argp;
    for (;;) {
        sceKernelDelayThread(500 * 1000);
        if (s_popup_off && sceKernelGetSystemTimeLow() - s_heartbeat > 3 * 1000 * 1000) set_popup(1);
    }
    return 0;
}

int sys_home_init(void)
{
    if (!sys_has_ark()) return 0;
    s_peek = (void *)sctrlHENFindFunction("sceController_Service", "sceCtrl_driver", 0x3A622550);   /* sceCtrlPeekBufferPositive */
    s_popup = (void *)sctrlHENFindFunction("sceImpose_Driver", "sceImpose_driver", 0x5595A71A);     /* sceImposeSetHomePopup */
    sh_log("PS button: kernel read %s, quit-screen switch %s", s_peek ? "found" : "NOT found", s_popup ? "found" : "NOT found");
    if (!s_peek || !s_popup) { s_peek = s_popup = NULL; return 0; }
    s_heartbeat = sceKernelGetSystemTimeLow();
    SceUID th = sceKernelCreateThread("ShelfHome", home_watchdog, 0x10, 0x1000, PSP_THREAD_ATTR_USER, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    return 1;
}

/* 1 while the PS button is down. Takes the button over the first time it is found up
 * (taking it mid-press would open the system's screen and Shelf's menu together). */
int sys_home_down(void)
{
    SceCtrlData pad;
    if (!s_peek) return 0;
    s_heartbeat = sceKernelGetSystemTimeLow();
    memset(&pad, 0, sizeof(pad));
    if (kcall(s_peek, (u32)&pad, 1) < 0) return 0;
    if (!s_popup_off) {
        if (!(pad.Buttons & 0x10000)) set_popup(0);
        return 0;
    }
    return (pad.Buttons & 0x10000) != 0;
}

/* Hands the button back to the system: before a game, the XMB, USB mode and sleep. */
void sys_home_release(void)
{
    if (s_popup_off) set_popup(1);
}

/* ---- leaving -------------------------------------------------------------- */
void sys_exit_xmb(int as_launcher)
{
    if (as_launcher && sys_has_ark()) {
        /* also for this session: ARK must not start the launcher again on the way out */
        SEConfigARK se;
        memset(&se, 0, sizeof(se));
        if (sctrlSEGetConfigEx((SEConfig *)&se, sizeof(se)) >= 0 && se.magic == ARK_CONFIG_MAGIC) {
            se.launcher_mode = 0;
            sctrlSESetConfigEx((SEConfig *)&se, sizeof(se));
        }
        ARKConfig c;
        memset(&c, 0, sizeof(c));
        sctrlArkGetConfig(&c);
        if (IS_ARK_CONFIG(&c)) { c.launcher[0] = 0; sctrlArkSetConfig(&c); }
    }
    sceKernelDelayThread(50 * 1000);
    if (sys_has_ark()) sctrlKernelExitVSH(NULL);
    sceKernelExitGame();
}

void sys_restart(void)  { sceKernelDelayThread(50 * 1000); scePowerRequestColdReset(0); }
void sys_shutdown(void) { sceKernelDelayThread(50 * 1000); scePowerRequestStandby(); }
void sys_sleep(void)    { scePowerRequestSuspend(); }

/* Starts another homebrew program (Shelf's own launcher copy). Only returns if that failed. */
int sys_run_program(const char *path)
{
    static char p[192];
    struct SceKernelLoadExecVSHParam param;
    if (!sys_has_ark()) return -1;
    sh_strcpy(p, sizeof(p), path);
    param_init(&param, "game", p);
    return sctrlKernelLoadExecVSHWithApitype(RL_HOMEBREW, p, &param);
}

void sys_recovery(const char *ark_dir)
{
    char p[192];
    sh_snprintf(p, sizeof(p), "%sRECOVERY.PBP", ark_dir);
    if (exists(p)) {
        struct SceKernelLoadExecVSHParam param;
        param_init(&param, "game", p);
        sctrlKernelLoadExecVSHWithApitype(RL_HOMEBREW, p, &param);
        return;
    }
    sh_snprintf(p, sizeof(p), "%sRECOVERY.PRX", ark_dir);
    if (!exists(p)) sh_strcpy(p, sizeof(p), "flash0:/vsh/module/ark_recovery.prx");
    SceUID mod = kuKernelLoadModule(p, 0, NULL);
    if (mod >= 0 && sceKernelStartModule(mod, strlen(p) + 1, p, NULL, NULL) >= 0)
        for (;;) sceKernelDelayThread(1000 * 1000);     /* the recovery menu takes over */
}

/* ---- USB mode (the drivers the XMB uses) ------------------------------------- */
static int s_usb;

static u32 s_cable_since;                         /* when the cable was first seen without the computer connecting */
static int s_usb_retried, s_usb_last;

int sys_usb_start(void)
{
    static const char *mods[] = { "flash0:/kd/semawm.prx", "flash0:/kd/usbstor.prx", "flash0:/kd/usbstormgr.prx",
                                  "flash0:/kd/usbstorms.prx", "flash0:/kd/usbstorboot.prx" };
    static int loaded;
    if (s_usb) return 0;
    if (!loaded) {
        for (unsigned i = 0; i < sizeof(mods) / sizeof(mods[0]); i++) {
            SceUID m = kuKernelLoadModule(mods[i], 0, NULL);
            int r = m >= 0 ? sceKernelStartModule(m, 0, NULL, NULL, NULL) : m;
            sh_log("usb: %s %08X", mods[i] + 10, r);
        }
        loaded = 1;
    }
    sceIoSync("ms0:", 0);
    int st0 = sceUsbGetState(), d = 0;
    if (st0 & PSP_USB_ACTIVATED) d = sceUsbDeactivate(0x1c8);     /* something else had USB switched on (psp5 does this too) */
    int a = sceUsbStart(PSP_USBBUS_DRIVERNAME, 0, 0);
    int b = sceUsbStart(PSP_USBSTOR_DRIVERNAME, 0, 0);
    int c = sceUsbActivate(0x1c8);
    sh_log("usb: state before %03X (deactivate %08X), start %08X %08X, activate %08X, state %03X", st0, d, a, b, c, sceUsbGetState());
    s_usb = 1;
    s_cable_since = 0;
    s_usb_retried = 0;
    return 0;
}

void sys_usb_stop(void)
{
    if (!s_usb) return;
    sceUsbDeactivate(0x1c8);
    sceUsbStop(PSP_USBSTOR_DRIVERNAME, 0, 0);
    sceUsbStop(PSP_USBBUS_DRIVERNAME, 0, 0);
    s_usb = 0;
    /* the computer may have changed the stick: forget what the file system remembers */
    sceIoDevctl("fatms0:", 0x0240D81E, NULL, 0, NULL, 0);
    sh_log("usb: stopped; last state %03X%s", s_usb_last, s_usb_retried ? ", switched off and on once because the computer did not connect" : "");
}

int sys_usb_state(void)
{
    if (!s_usb) return 0;
    int st = sceUsbGetState();
    u32 now = sceKernelGetSystemTimeLow();
    s_usb_last = st;
    /* The cable is there but the computer never connects: switch USB off and on once.
     * It is what connecting in the XMB and coming back does, and what made USB work
     * for psp5 in the same situation. */
    if ((st & PSP_USB_CABLE_CONNECTED) && !(st & PSP_USB_CONNECTION_ESTABLISHED)) {
        if (!s_cable_since) s_cable_since = now ? now : 1;
        else if (!s_usb_retried && now - s_cable_since > 5 * 1000 * 1000) {
            s_usb_retried = 1;
            sceUsbDeactivate(0x1c8);
            sceKernelDelayThread(50 * 1000);
            sceUsbActivate(0x1c8);
        }
    } else s_cable_since = 0;
    return (st & PSP_USB_CONNECTION_ESTABLISHED) ? 2 : 1;
}
