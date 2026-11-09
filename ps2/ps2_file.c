/* ps2_file.c: stdio files on the IOP file server (ps2_stdio.h). Paths are
   the engine's OS paths, relative to the directory of the ELF. Reads go
   through a buffer per file, as the pk3 reader asks for a few bytes at a
   time and each mx_fio_read is a SIF RPC. */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <mx/mx.h>
#include <mx/sif.h>

#include "ps2_stdio.h"

#define BUF_BYTES (16 * 1024)

struct pfile {
  int fd;
  int write;
  int eof, err;
  long pos;      /* the file position the engine sees */
  long size;     /* for reading */
  long buf_pos;  /* file position of buf[0] */
  int buf_len;   /* bytes in buf */
  char *buf;
};

static int is_std(FILE *f) { return f == stdout || f == stderr || f == stdin; }

/* "host:/a//b" or "host:./a" -> "a/b": in the directory of the ELF */
static void host_path(char *out, size_t room, const char *path) {
  const char *p = path;
  if (!strncmp(p, "host:", 5))
    p += 5;
  size_t n = 0;
  while (*p == '/' || (p[0] == '.' && p[1] == '/'))
    p += *p == '/' ? 1 : 2;
  for (; *p && n + 1 < room; p++) {
    if (*p == '/' && (p[1] == '/' || (p[1] == '.' && p[2] == '/'))) {
      if (p[1] == '.')
        p++;
      continue;
    }
    out[n++] = *p;
  }
  out[n] = 0;
}

FILE *ps2_fopen(const char *path, const char *mode) {
  char hp[512];
  host_path(hp, sizeof hp, path);
  int w = strchr(mode, 'w') || strchr(mode, 'a');
  int flags = w ? MX_FIO_WRONLY | MX_FIO_CREAT | (strchr(mode, 'a') ? 0 : MX_FIO_TRUNC)
                : MX_FIO_RDONLY;
  int fd = mx_fio_open(hp, flags);
  if (fd < 0) {
    errno = ENOENT;
    return NULL;
  }
  struct pfile *f = calloc(1, sizeof *f);
  if (!f) {
    mx_fio_close(fd);
    errno = ENOMEM;
    return NULL;
  }
  f->fd = fd;
  f->write = w;
  if (!w) {
    f->size = mx_fio_lseek(fd, 0, MX_FIO_SEEK_END);
    mx_fio_lseek(fd, 0, MX_FIO_SEEK_SET);
    f->buf = malloc(BUF_BYTES);
    if (!f->buf) {
      mx_printf("ps2_fopen %s: no memory\n", hp);
      mx_fio_close(fd);
      free(f);
      errno = ENOMEM;
      return NULL;
    }
  } else if (strchr(mode, 'a')) {
    f->pos = mx_fio_lseek(fd, 0, MX_FIO_SEEK_END);
  }
  return (FILE *)f;
}

int ps2_fclose(FILE *ff) {
  if (!ff || is_std(ff))
    return 0;
  struct pfile *f = (struct pfile *)ff;
  mx_fio_close(f->fd);
  free(f->buf);
  free(f);
  return 0;
}

/* read n bytes at file position pos (the file's own position follows) */
static int raw_read(struct pfile *f, long pos, void *dst, int n) {
  static long at = -1; /* not tracked per file: seek every time */
  (void)at;
  if (mx_fio_lseek(f->fd, pos, MX_FIO_SEEK_SET) != pos)
    return -1;
  return mx_fio_read(f->fd, dst, (u32)n);
}

size_t ps2_fread(void *p, size_t size, size_t n, FILE *ff) {
  if (is_std(ff) || !size || !n)
    return 0;
  struct pfile *f = (struct pfile *)ff;
  if (f->write) {
    f->err = 1;
    return 0;
  }
  size_t want = size * n, got = 0;
  char *dst = p;
  while (got < want) {
    if (f->pos >= f->size) {
      f->eof = 1;
      break;
    }
    long off = f->pos - f->buf_pos;
    if (off >= 0 && off < f->buf_len) {
      size_t k = (size_t)(f->buf_len - off);
      if (k > want - got)
        k = want - got;
      memcpy(dst + got, f->buf + off, k);
      got += k;
      f->pos += (long)k;
      continue;
    }
    size_t left = want - got;
    if (left >= BUF_BYTES) {
      /* large: straight into the destination */
      long rem = f->size - f->pos;
      int k = (int)(left < (size_t)rem ? left : (size_t)rem);
      int r = raw_read(f, f->pos, dst + got, k);
      if (r <= 0) {
        f->err = 1;
        break;
      }
      got += (size_t)r;
      f->pos += r;
      continue;
    }
    int r = raw_read(f, f->pos, f->buf, BUF_BYTES);
    if (r <= 0) {
      f->err = r < 0;
      f->eof = r == 0;
      break;
    }
    f->buf_pos = f->pos;
    f->buf_len = r;
  }
  return got / size;
}

size_t ps2_fwrite(const void *p, size_t size, size_t n, FILE *ff) {
  if (is_std(ff)) {
    fflush(ff);
    extern ssize_t write(int, const void *, size_t);
    write(1, p, size * n);
    return n;
  }
  struct pfile *f = (struct pfile *)ff;
  if (!f->write || !size)
    return 0;
  /* mx_fio_write returns 0 when all was written */
  if (mx_fio_write(f->fd, p, (u32)(size * n)) < 0) {
    f->err = 1;
    return 0;
  }
  f->pos += (long)(size * n);
  return n;
}

int ps2_fseek(FILE *ff, long off, int whence) {
  if (is_std(ff))
    return -1;
  struct pfile *f = (struct pfile *)ff;
  if (f->write) {
    int r = mx_fio_lseek(f->fd, off, whence);
    if (r < 0)
      return -1;
    f->pos = r;
    return 0;
  }
  long np = whence == SEEK_SET ? off : whence == SEEK_CUR ? f->pos + off : f->size + off;
  if (np < 0)
    return -1;
  f->pos = np;
  f->eof = 0;
  return 0;
}

long ps2_ftell(FILE *ff) { return is_std(ff) ? -1 : ((struct pfile *)ff)->pos; }
int ps2_fflush(FILE *ff) {
#undef fflush
  if (!ff || is_std(ff))
    return fflush(ff);
  return 0;
}
int ps2_feof(FILE *ff) { return is_std(ff) ? 0 : ((struct pfile *)ff)->eof; }
int ps2_ferror(FILE *ff) { return is_std(ff) ? 0 : ((struct pfile *)ff)->err; }
void ps2_rewind(FILE *ff) { ps2_fseek(ff, 0, SEEK_SET); }

int ps2_vfprintf(FILE *ff, const char *fmt, va_list ap) {
#undef vfprintf
  if (is_std(ff))
    return vfprintf(ff, fmt, ap);
  char tmp[2048];
  int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
  if (n > (int)sizeof tmp - 1)
    n = sizeof tmp - 1;
  return (int)ps2_fwrite(tmp, 1, (size_t)n, ff);
}

int ps2_fprintf(FILE *f, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = ps2_vfprintf(f, fmt, ap);
  va_end(ap);
  return r;
}

int ps2_fputs(const char *s, FILE *f) { return ps2_fwrite(s, 1, strlen(s), f) ? 0 : -1; }
int ps2_fputc(int c, FILE *f) {
  char ch = (char)c;
  return ps2_fwrite(&ch, 1, 1, f) ? c : -1;
}
int ps2_fgetc(FILE *f) {
  unsigned char ch;
  return ps2_fread(&ch, 1, 1, f) ? ch : -1;
}
char *ps2_fgets(char *s, int n, FILE *f) {
  int i = 0;
  while (i < n - 1) {
    int c = ps2_fgetc(f);
    if (c < 0)
      break;
    s[i++] = (char)c;
    if (c == '\n')
      break;
  }
  if (!i)
    return NULL;
  s[i] = 0;
  return s;
}

/* The IOP file server has remove and rename, but maxsdk's client does not:
   the engine only uses them for its own files (configs, demos). */
int ps2_remove(const char *path) {
  (void)path;
  return -1;
}
int ps2_rename(const char *from, const char *to) {
  (void)from, (void)to;
  return -1;
}
int ps2_setvbuf(FILE *f, char *buf, int mode, size_t size) {
  (void)f, (void)buf, (void)mode, (void)size;
  return 0;
}

/* Directory listing for Sys_ListFiles: calls fn for each entry of dir. */
int ps2_listdir(const char *dir, void (*fn)(const char *name, int is_dir, void *ctx), void *ctx) {
  char hp[512];
  host_path(hp, sizeof hp, dir);
  int fd = mx_fio_dopen(hp);
  if (fd < 0)
    return -1;
  char name[256];
  u32 size;
  while (mx_fio_dread(fd, name, sizeof name, &size) > 0) {
    /* no type from mx_fio_dread: a name without a dot is taken as a directory */
    fn(name, strchr(name, '.') == NULL, ctx);
  }
  mx_fio_dclose(fd);
  return 0;
}

/* fscanf: picolibc's vfscanf on a stream whose characters are the file's
   (ps2_fgetc); the character it reads past the last field and keeps
   (ungetc) goes back to the file */
static FILE *scan_src;
static int scan_get(FILE *s) {
  (void)s;
  int c = ps2_fgetc(scan_src);
  return c < 0 ? _FDEV_EOF : c;
}
int ps2_vfscanf(FILE *ff, const char *fmt, va_list ap) {
#undef vfscanf
  if (is_std(ff))
    return vfscanf(ff, fmt, ap);
  struct __file s = FDEV_SETUP_STREAM(NULL, scan_get, NULL, __SRD);
  scan_src = ff;
  int r = vfscanf(&s, fmt, ap);
  if (s.unget)
    ps2_ungetc(0, ff);
  return r;
}
int ps2_fscanf(FILE *ff, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = ps2_vfscanf(ff, fmt, ap);
  va_end(ap);
  return r;
}

/* ungetc of the character just read: the file's position one back (the
   character itself is the file's) */
int ps2_ungetc(int c, FILE *ff) {
#undef ungetc
  if (is_std(ff))
    return ungetc(c, ff);
  struct pfile *f = (struct pfile *)ff;
  if (c < 0 || f->pos <= 0 || f->write)
    return -1;
  f->pos--;
  f->eof = 0;
  return c;
}
