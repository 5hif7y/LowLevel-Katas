#ifndef FAT_VFS_H
#define FAT_VFS_H

/*
 * Adaptador FAT -> VFS. Uso:
 *
 *     static fat_fs sd;
 *     fat_mount_first(&sd, &sd_blockdev);
 *     vfs_mount("/sd", &fat_vfs_ops, &sd);
 */

#include "fat.h"
#include "vfs.h"

extern const vfs_ops fat_vfs_ops;

#endif /* FAT_VFS_H */
