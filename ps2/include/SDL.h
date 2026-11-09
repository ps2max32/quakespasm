/* SDL.h of the PS2 build: there is no SDL. Quakespasm's engine includes it
   (quakedef.h) for the GL headers, which here are libgl's, and common.c
   reads the localization file through SDL_RWops, here stdio's FILE (the
   port's ps2_fopen, ps2_stdio.h). The platform layer is ps2/. */

#ifndef PS2_SDL_H
#define PS2_SDL_H

#include <stdint.h>
#include <stdio.h>

#include <GL/gl.h>
#include <GL/glext.h>

typedef int8_t Sint8;
typedef uint8_t Uint8;
typedef int16_t Sint16;
typedef uint16_t Uint16;
typedef int32_t Sint32;
typedef uint32_t Uint32;
typedef int64_t Sint64;
typedef uint64_t Uint64;

#define SDLCALL

typedef FILE SDL_RWops;
#define RW_SEEK_SET SEEK_SET
#define RW_SEEK_CUR SEEK_CUR
#define RW_SEEK_END SEEK_END

static inline SDL_RWops *SDL_RWFromFile(const char *path, const char *mode) { return fopen(path, mode); }
static inline Sint64 SDL_RWseek(SDL_RWops *rw, Sint64 ofs, int whence) {
	return fseek(rw, (long)ofs, whence) ? -1 : ftell(rw);
}
static inline Sint64 SDL_RWtell(SDL_RWops *rw) { return ftell(rw); }
static inline size_t SDL_RWread(SDL_RWops *rw, void *p, size_t size, size_t n) { return fread(p, size, n, rw); }
static inline Sint64 SDL_RWsize(SDL_RWops *rw) {
	long pos = ftell(rw);
	fseek(rw, 0, SEEK_END);
	long end = ftell(rw);
	fseek(rw, pos, SEEK_SET);
	return end;
}
static inline int SDL_RWclose(SDL_RWops *rw) { return fclose(rw); }

#endif
