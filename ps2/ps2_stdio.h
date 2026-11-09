/* ps2_stdio.h: included before every source of the port (-include). The
   files of the game (stdio's fopen and friends) go to the IOP file server
   (host:, <mx/sif.h>) through ps2_file.c; stdout and stderr stay picolibc's
   (the debug output). A ps2 file is returned as a FILE * but is not one:
   every stdio call that can get it is redirected here. */

#ifndef PS2_STDIO_H
#define PS2_STDIO_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

FILE *ps2_fopen(const char *path, const char *mode);
int ps2_fclose(FILE *f);
size_t ps2_fread(void *p, size_t size, size_t n, FILE *f);
size_t ps2_fwrite(const void *p, size_t size, size_t n, FILE *f);
int ps2_fseek(FILE *f, long off, int whence);
long ps2_ftell(FILE *f);
int ps2_fflush(FILE *f);
int ps2_feof(FILE *f);
int ps2_ferror(FILE *f);
int ps2_fprintf(FILE *f, const char *fmt, ...);
int ps2_vfprintf(FILE *f, const char *fmt, va_list ap);
int ps2_fputs(const char *s, FILE *f);
int ps2_fputc(int c, FILE *f);
int ps2_fgetc(FILE *f);
char *ps2_fgets(char *s, int n, FILE *f);
int ps2_remove(const char *path);
int ps2_rename(const char *from, const char *to);
int ps2_setvbuf(FILE *f, char *buf, int mode, size_t size);
void ps2_rewind(FILE *f);
int ps2_fscanf(FILE *f, const char *fmt, ...);
int ps2_vfscanf(FILE *f, const char *fmt, va_list ap);
int ps2_ungetc(int c, FILE *f);

#define fopen ps2_fopen
#define fclose ps2_fclose
#define fread ps2_fread
#define fwrite ps2_fwrite
#define fseek ps2_fseek
#define ftell ps2_ftell
#define fflush ps2_fflush
#undef feof
#define feof ps2_feof
#undef ferror
#define ferror ps2_ferror
#define fprintf ps2_fprintf
#define vfprintf ps2_vfprintf
#define fputs ps2_fputs
#undef fputc
#define fputc ps2_fputc
#undef putc
#define putc ps2_fputc
#undef fgetc
#define fgetc ps2_fgetc
#undef getc
#define getc ps2_fgetc
#define fgets ps2_fgets
#define remove ps2_remove
#define rename ps2_rename
#define setvbuf ps2_setvbuf
#define rewind ps2_rewind
#define fscanf ps2_fscanf
#define vfscanf ps2_vfscanf
#undef ungetc
#define ungetc ps2_ungetc

#endif
