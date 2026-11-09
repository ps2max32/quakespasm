/* dirent.h of the PS2 build: host_cmd.c's lists (maps, mods, demos) read
   directories through the IOP's file server (mx_fio_dopen) */

#ifndef PS2_DIRENT_H
#define PS2_DIRENT_H

#include <stdlib.h>
#include <mx/sif.h>

struct dirent {
	char d_name[256];
};

typedef struct {
	int fd;
	struct dirent ent;
} DIR;

static inline DIR *opendir(const char *path)
{
	while (path[0] == '.' && path[1] == '/')
		path += 2;
	int fd = mx_fio_dopen(*path ? path : ".");
	if (fd < 0)
		return NULL;
	DIR *d = (DIR *)calloc(1, sizeof *d);
	if (!d) {
		mx_fio_dclose(fd);
		return NULL;
	}
	d->fd = fd;
	return d;
}

static inline struct dirent *readdir(DIR *d)
{
	return mx_fio_dread(d->fd, d->ent.d_name, sizeof d->ent.d_name, 0) > 0 ? &d->ent : NULL;
}

static inline int closedir(DIR *d)
{
	mx_fio_dclose(d->fd);
	free(d);
	return 0;
}

#endif
