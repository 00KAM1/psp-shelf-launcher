/* sceIo* on POSIX: "ms0:/X" is ./ms0/X */
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include "shim/pspiofilemgr.h"
int g_fail_write;
static void map(const char *p, char *out){ snprintf(out, 600, "./%s", p); char *c = strchr(out, ':'); if (c) memmove(c, c + 1, strlen(c)); }
SceUID sceIoOpen(const char *p, int fl, int m){ char q[600]; map(p,q); int f;
  if ((fl&3)==1) f=O_RDONLY; else if ((fl&3)==2) f=O_WRONLY; else f=O_RDWR;
  if (fl&PSP_O_CREAT) f|=O_CREAT; if (fl&PSP_O_TRUNC) f|=O_TRUNC; if (fl&PSP_O_APPEND) f|=O_APPEND;
  int fd=open(q,f,0644); return fd<0? -1: fd; }
int sceIoRead(SceUID fd, void *b, SceSize n){ return read(fd,b,n); }
int sceIoWrite(SceUID fd, const void *b, SceSize n){ if (g_fail_write) return n/2; return write(fd,b,n); }
int sceIoClose(SceUID fd){ return close(fd); }
int sceIoLseek32(SceUID fd, int o, int w){ return lseek(fd,o,w); }
int sceIoRemove(const char *p){ char q[600]; map(p,q); return unlink(q); }
int sceIoRename(const char *a, const char *b){ char x[600],y[600]; map(a,x); map(b,y); return rename(x,y); }
int sceIoMkdir(const char *p, int m){ char q[600]; map(p,q); return mkdir(q,0755); }
int sceIoRmdir(const char *p){ char q[600]; map(p,q); return rmdir(q); }
static void fill(struct stat *st, SceIoStat *s){ memset(s,0,sizeof(*s)); s->st_mode = S_ISDIR(st->st_mode) ? FIO_S_IFDIR : 0x2000; s->st_size = st->st_size;
  struct tm tm; gmtime_r(&st->st_mtime, &tm); s->sce_st_mtime.year = tm.tm_year + 1900; s->sce_st_mtime.month = tm.tm_mon + 1; s->sce_st_mtime.day = tm.tm_mday;
  s->sce_st_mtime.hour = tm.tm_hour; s->sce_st_mtime.minute = tm.tm_min; s->sce_st_mtime.second = tm.tm_sec; }
int sceIoGetstat(const char *p, SceIoStat *s){ char q[600]; map(p,q); struct stat st; if (stat(q,&st)<0) return -1; fill(&st,s); return 0; }
typedef struct { DIR *d; char path[600]; } HD;
static HD g_dirs[16];
SceUID sceIoDopen(const char *p){ char q[600]; map(p,q); for (int i=0;i<16;i++) if (!g_dirs[i].d){ g_dirs[i].d=opendir(q); if(!g_dirs[i].d) return -1; strcpy(g_dirs[i].path,q); return 1000+i; } return -1; }
int sceIoDread(SceUID fd, SceIoDirent *e){ HD *h=&g_dirs[fd-1000]; struct dirent *de; while ((de=readdir(h->d))) { if (!strcmp(de->d_name,".")||!strcmp(de->d_name,"..")) continue;
  char f[900]; snprintf(f,sizeof f,"%s/%s",h->path,de->d_name); struct stat st; stat(f,&st); fill(&st,&e->d_stat); snprintf(e->d_name,256,"%s",de->d_name); return 1; } return 0; }
int sceIoDclose(SceUID fd){ HD *h=&g_dirs[fd-1000]; closedir(h->d); h->d=NULL; return 0; }
