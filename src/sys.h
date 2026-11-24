#ifndef SH_SYS_H
#define SH_SYS_H
#include "common.h"
int  sys_has_ark(void);
void sys_ark_dir(char *out, int size);
int  sys_launch(const ShGame *g);                 /* returns only on failure */
void sys_exit_xmb(int as_launcher);
void sys_restart(void);
void sys_shutdown(void);
void sys_sleep(void);
void sys_recovery(const char *ark_dir);
int  sys_usb_start(void);
void sys_usb_stop(void);
int  sys_usb_state(void);                         /* 0 off, 1 waiting, 2 connected */
void sys_cpu_fast(int on);                        /* 333 MHz while reading games, then back to what it was */
int  sys_ms_space(u32 *free_mb, u32 *total_mb);   /* 0 = ok */
u32 *sys_vram(int page);
int  sys_home_init(void);                         /* the PS button for Shelf's menu; 1 = available */
int  sys_home_down(void);                         /* 1 while it is held */
void sys_home_release(void);                      /* back to the system (its quit screen works again) */                          /* the two pages Shelf shows, 512 x 272 x 32-bit */
#endif
int  sys_run_program(const char *path);        /* another homebrew; returns only on failure */
