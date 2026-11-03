#ifndef VFS_H
#define VFS_H

/*
 * VFS mínimo: una tabla de puntos de montaje y una vtable por sistema de
 * archivos. Resuelve "/sd/logs/a.txt" al backend montado en "/sd" (el prefijo
 * más largo gana) y le pasa "/logs/a.txt".
 *
 * Igual que fat.c: sin malloc. vfs_file y vfs_dir llevan adentro el estado del
 * backend en un buffer de VFS_PRIV_SIZE bytes, alineado para cualquier tipo.
 * Cada backend comprueba en compilación que su estado entra (ver fat_vfs.c).
 */

#include <stddef.h>
#include <stdint.h>

#include "fs_types.h"

#ifndef VFS_MAX_MOUNTS
#define VFS_MAX_MOUNTS	4
#endif
#ifndef VFS_PREFIX_MAX
#define VFS_PREFIX_MAX	16
#endif
#ifndef VFS_PRIV_SIZE
#define VFS_PRIV_SIZE	64
#endif
#define VFS_NAME_MAX	64

typedef union vfs_priv {
	uint64_t	 align_u64;
	void		*align_ptr;
	unsigned char	 bytes[VFS_PRIV_SIZE];
} vfs_priv;

typedef struct vfs_info {
	uint32_t	 size;
	uint32_t	 mtime;		/* formato del backend; en FAT, fat_pack_time() */
	uint8_t		 is_dir;
	uint8_t		 readonly;
} vfs_info;

typedef struct vfs_dirent {
	char		 name[VFS_NAME_MAX];
	uint32_t	 size;
	uint8_t		 is_dir;
} vfs_dirent;

/* `fs` es el contexto que se pasó a vfs_mount; `fh`/`dh`, el buffer privado. */
typedef struct vfs_ops {
	const char	*name;
	int		(*open)(void *fs, void *fh, const char *path, int flags);
	int32_t		(*read)(void *fh, void *buf, uint32_t n);
	int32_t		(*write)(void *fh, const void *buf, uint32_t n);
	int64_t		(*seek)(void *fh, int64_t off, int whence);
	int		(*close)(void *fh);
	int		(*stat)(void *fs, const char *path, vfs_info *st);
	int		(*mkdir)(void *fs, const char *path);
	int		(*unlink)(void *fs, const char *path);
	int		(*rmdir)(void *fs, const char *path);
	int		(*opendir)(void *fs, void *dh, const char *path);
	int		(*readdir)(void *dh, vfs_dirent *ent);	/* 1, 0 o <0 */
	int		(*sync)(void *fs);
} vfs_ops;

typedef struct vfs_file {
	const vfs_ops	*ops;
	vfs_priv	 priv;
} vfs_file;

typedef struct vfs_dir {
	const vfs_ops	*ops;
	vfs_priv	 priv;
} vfs_dir;

/* El prefijo empieza con '/', sin '/' final; "/" monta en la raíz. */
int	 vfs_mount(const char *prefix, const vfs_ops *ops, void *fs);
int	 vfs_unmount(const char *prefix);	/* sincroniza antes de soltar */
int	 vfs_sync_all(void);

int	 vfs_open(vfs_file *f, const char *path, int flags);
int32_t	 vfs_read(vfs_file *f, void *buf, uint32_t n);
int32_t	 vfs_write(vfs_file *f, const void *buf, uint32_t n);
int64_t	 vfs_seek(vfs_file *f, int64_t off, int whence);
int	 vfs_close(vfs_file *f);

int	 vfs_stat(const char *path, vfs_info *st);
int	 vfs_mkdir(const char *path);
int	 vfs_unlink(const char *path);
int	 vfs_rmdir(const char *path);
int	 vfs_opendir(vfs_dir *d, const char *path);
int	 vfs_readdir(vfs_dir *d, vfs_dirent *ent);

#endif /* VFS_H */
