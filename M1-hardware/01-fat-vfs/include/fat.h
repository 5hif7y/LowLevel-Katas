#ifndef FAT_H
#define FAT_H

/*
 * FAT12 / FAT16 / FAT32 sobre un dispositivo de bloques abstracto.
 *
 * Pensado para emuladores y microcontroladores (Cortex-M y similares):
 *   - C99, sin malloc, sin stdio, sin errno. Sólo <stdint.h>, <stddef.h> y
 *     memcpy/memset/memcmp de <string.h>.
 *   - Toda la memoria la pone quien llama: fat_fs (≈ 60 bytes + un sector de
 *     caché), fat_file y fat_dir (decenas de bytes cada uno).
 *   - El acceso al medio es un par de callbacks read/write por sector, así que
 *     sirve igual para una imagen en disco, una SD por SPI o un buffer en RAM.
 *
 * Nombres 8.3 solamente (sin VFAT/LFN): ver README.md, "Límites".
 * No es reentrante: un fat_fs se usa desde un solo hilo (o bajo un mutex).
 */

#include <stddef.h>
#include <stdint.h>

#include "fs_types.h"

/*
 * Tamaño del sector de caché. Fija el sector más grande que se puede montar.
 * Tiene que ser el mismo en todas las unidades de compilación: definilo con
 * -DFAT_MAX_SECTOR_SIZE=4096 en el build, no antes de un #include suelto.
 */
#ifndef FAT_MAX_SECTOR_SIZE
#define FAT_MAX_SECTOR_SIZE 512
#endif

#define FAT_ATTR_RDONLY		0x01
#define FAT_ATTR_HIDDEN		0x02
#define FAT_ATTR_SYSTEM		0x04
#define FAT_ATTR_VOLID		0x08
#define FAT_ATTR_DIR		0x10
#define FAT_ATTR_ARCHIVE	0x20
#define FAT_ATTR_LFN		0x0F

/* Dispositivo de bloques. read/write devuelven 0 si salió bien. */
typedef struct fat_blockdev {
	void		*ctx;
	uint32_t	 sector_size;	/* 512, 1024, 2048 o 4096 */
	uint32_t	 sector_count;
	int		(*read)(void *ctx, uint32_t lba, void *buf, uint32_t count);
	int		(*write)(void *ctx, uint32_t lba, const void *buf, uint32_t count);
} fat_blockdev;

typedef struct fat_fs {
	fat_blockdev	*dev;
	uint32_t	 part_lba;	/* inicio del volumen dentro del dispositivo */
	uint8_t		 type;		/* 12, 16 o 32 */
	uint8_t		 num_fats;
	uint16_t	 bytes_per_sec;
	uint32_t	 sec_per_clus;
	uint32_t	 fat_lba;	/* LBAs relativos al volumen */
	uint32_t	 fat_size;	/* sectores por copia de la FAT */
	uint32_t	 root_lba;	/* raíz fija (FAT12/16) */
	uint32_t	 root_secs;
	uint32_t	 data_lba;
	uint32_t	 cluster_count;
	uint32_t	 root_clus;	/* raíz en cadena (FAT32) */
	uint32_t	 next_free;	/* pista para el asignador */
	/* Hora actual empaquetada con fat_pack_time(); NULL = 1980-01-01.
	 * Se asigna después de fat_mount(), que limpia la estructura. */
	uint32_t	(*clock)(void);
	uint32_t	 cache_lba;
	int		 cache_dirty;
	uint8_t		 cache[FAT_MAX_SECTOR_SIZE];
} fat_fs;

typedef struct fat_file {
	fat_fs		*fs;
	uint32_t	 first_clus;
	uint32_t	 size;
	uint32_t	 pos;
	uint32_t	 cur_clus;	/* cluster que contiene cur_idx */
	uint32_t	 cur_idx;	/* índice de cur_clus dentro de la cadena */
	uint32_t	 de_lba;	/* dónde vive la entrada de directorio */
	uint16_t	 de_off;
	uint8_t		 flags;
	uint8_t		 dirty;
} fat_file;

/* Iterador de directorio. También se usa por dentro de fat.c. */
typedef struct fat_dir {
	fat_fs		*fs;
	uint32_t	 clus;
	uint32_t	 lba;
	uint32_t	 sec_left;
	uint16_t	 off;
	uint8_t		 fixed;		/* raíz fija de FAT12/16 */
	uint8_t		 done;
} fat_dir;

typedef struct fat_dirent {
	char		 name[13];	/* "NOMBRE.EXT\0" */
	uint8_t		 attr;
	uint32_t	 size;
	uint32_t	 first_clus;
	uint32_t	 mtime;		/* (fecha << 16) | hora, formato FAT */
} fat_dirent;

typedef struct fat_info {
	uint32_t	 size;
	uint8_t		 attr;
	uint32_t	 first_clus;
	uint32_t	 mtime;
} fat_info;

typedef struct fat_volinfo {
	uint8_t		 type;
	uint32_t	 cluster_size;	/* en bytes */
	uint32_t	 total_clusters;
	uint32_t	 free_clusters;
} fat_volinfo;

typedef struct fat_mkfs_opts {
	uint8_t		 type;		/* 12, 16, 32 o 0 = elegir por tamaño */
	uint8_t		 sec_per_clus;	/* 0 = automático */
	uint32_t	 part_lba;	/* dónde empieza el volumen */
	uint32_t	 sectors;	/* 0 = hasta el final del dispositivo */
	const char	*label;		/* NULL = "NO NAME" */
	uint32_t	 volume_id;
} fat_mkfs_opts;

/* Volumen. fat_mkfs usa `scratch` como buffer y lo deja sin montar. */
int	 fat_mkfs(fat_fs *scratch, fat_blockdev *dev, const fat_mkfs_opts *opts);
int	 fat_mount(fat_fs *fs, fat_blockdev *dev, uint32_t part_lba);
int	 fat_mount_first(fat_fs *fs, fat_blockdev *dev);	/* volumen suelto o 1ª partición MBR */
int	 fat_sync(fat_fs *fs);
int	 fat_unmount(fat_fs *fs);
int	 fat_volume_info(fat_fs *fs, fat_volinfo *vi);

/* Archivos. Las rutas usan '/' y se resuelven desde la raíz del volumen. */
int	 fat_open(fat_fs *fs, fat_file *f, const char *path, int flags);
int32_t	 fat_read(fat_file *f, void *buf, uint32_t n);
int32_t	 fat_write(fat_file *f, const void *buf, uint32_t n);
int64_t	 fat_seek(fat_file *f, int64_t off, int whence);
int	 fat_fsync(fat_file *f);
int	 fat_close(fat_file *f);

/* Nombres y directorios. */
int	 fat_stat(fat_fs *fs, const char *path, fat_info *st);
int	 fat_mkdir(fat_fs *fs, const char *path);
int	 fat_unlink(fat_fs *fs, const char *path);
int	 fat_rmdir(fat_fs *fs, const char *path);
int	 fat_opendir(fat_fs *fs, fat_dir *d, const char *path);
int	 fat_readdir(fat_dir *d, fat_dirent *ent);	/* 1 = entrada, 0 = fin, <0 = error */

uint32_t fat_pack_time(int year, int mon, int day, int hour, int min, int sec);

#endif /* FAT_H */
