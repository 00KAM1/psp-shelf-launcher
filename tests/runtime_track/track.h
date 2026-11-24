/*
 * Runtime - the part with no PSP calls in it: what one look at the clock and the
 * battery does to the record on the memory stick. Built for the PSP and for the PC tests.
 *
 * Nothing here is remembered between two looks. The record is read from the file,
 * changed and written back, because a Sidequest load puts a plugin's memory back
 * to what it was when the state was saved.
 */
#ifndef RUNTIME_TRACK_H
#define RUNTIME_TRACK_H

typedef unsigned int gu32;

#define GA_OWNER      2          /* fifth field of uptime.txt when Runtime keeps it (Shelf writes 0 or 1) */
#define GA_GAP        5          /* minutes without a look that still count as "running" (a restart takes well under one) */
#define GA_STEP       5          /* a line in the use log every time the battery has moved this many percent */

typedef struct {
    int  ok;                     /* a record was read */
    gu32 start;  int start_pct;  /* power-on: minutes since 2000 (local time), battery then (-1: no reading) */
    gu32 seen;   int seen_pct;   /* the last look */
    int  owner;
    gu32 asleep;                 /* minutes of this power-on spent asleep */
    int  mark_pct;               /* battery at the last use-log line (-1: none yet) */
} GaRec;

enum { GA_SAME, GA_NEW, GA_WOKE };

gu32 ga_minutes(int y, int mo, int d, int h, int mi);
void ga_date(gu32 minutes, char *out, int size);                 /* "Tue Nov 24 03:30" */
void ga_parse(const char *text, GaRec *r);
int  ga_format(const GaRec *r, char *out, int size);
/* One look. first: this is the first look since the plugin was loaded.
 * Returns GA_NEW (a new power-on; `ended` holds the line for the one before, or is empty),
 * GA_WOKE (back from sleep) or GA_SAME. */
int  ga_step(GaRec *r, gu32 now, int pct, int first, char *ended, int size);
/* Does the use log want a line now? (power on, program start, waking, every GA_STEP percent) */
int  ga_use_line(GaRec *r, gu32 now, int pct, int charging, int what, const char *program, char *out, int size);

/* tiny text helpers (the kernel has no full C library) */
int  ga_put(char *out, int size, int pos, const char *s);
int  ga_num(char *out, int size, int pos, gu32 v, int width);
#endif
