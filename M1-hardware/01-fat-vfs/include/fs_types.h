#ifndef FS_TYPES_H
#define FS_TYPES_H

/*
 * Tipos comunes a fat.c y vfs.c.
 *
 * Convención de errores: 0 es éxito, negativo es error. Es la misma idea que
 * -errno en un kernel, pero con valores propios para no depender de <errno.h>
 * (que en algunos toolchains bare-metal no existe o es global y no reentrante).
 */

enum {
	FS_OK           =   0,
	FS_EIO          =  -1,	/* el dispositivo falló o la estructura está corrupta */
	FS_ENOENT       =  -2,
	FS_EEXIST       =  -3,
	FS_ENOSPC       =  -4,
	FS_EINVAL       =  -5,
	FS_ENOTDIR      =  -6,
	FS_EISDIR       =  -7,
	FS_ENOTEMPTY    =  -8,
	FS_ENAMETOOLONG =  -9,
	FS_ENOFS        = -10,	/* no hay un sistema de archivos reconocible */
	FS_EACCES       = -11,
	FS_EBADF        = -12,
	FS_EFBIG        = -13
};

/* Flags de apertura: mismo significado que O_* de POSIX. */
#define FS_O_RDONLY	0x00
#define FS_O_WRONLY	0x01
#define FS_O_RDWR	0x02
#define FS_O_ACCMODE	0x03
#define FS_O_CREAT	0x10
#define FS_O_TRUNC	0x20
#define FS_O_APPEND	0x40
#define FS_O_EXCL	0x80

#define FS_SEEK_SET	0
#define FS_SEEK_CUR	1
#define FS_SEEK_END	2

static inline const char *
fs_strerror(int err)
{
	switch (err) {
	case FS_OK:		return "ok";
	case FS_EIO:		return "error de E/S o estructura corrupta";
	case FS_ENOENT:		return "no existe";
	case FS_EEXIST:		return "ya existe";
	case FS_ENOSPC:		return "no queda espacio";
	case FS_EINVAL:		return "argumento inválido";
	case FS_ENOTDIR:	return "no es un directorio";
	case FS_EISDIR:		return "es un directorio";
	case FS_ENOTEMPTY:	return "el directorio no está vacío";
	case FS_ENAMETOOLONG:	return "nombre demasiado largo para 8.3";
	case FS_ENOFS:		return "no hay un sistema de archivos FAT";
	case FS_EACCES:		return "permiso denegado";
	case FS_EBADF:		return "descriptor inválido";
	case FS_EFBIG:		return "archivo demasiado grande";
	default:		return "error desconocido";
	}
}

#endif /* FS_TYPES_H */
