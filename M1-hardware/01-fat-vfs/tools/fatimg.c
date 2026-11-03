/*
 * fatimg: crea y manipula imágenes FAT desde el host, sin montarlas.
 * Sirve para preparar disquetes y tarjetas SD de emuladores, y para cruzar
 * la implementación con herramientas ajenas (7-Zip, mtools, fsck.fat).
 *
 *   fatimg mkfs  IMG TAMAÑO [12|16|32|auto] [ETIQUETA]   TAMAÑO: 1440K, 64M, 2G...
 *   fatimg info  IMG
 *   fatimg ls    IMG [RUTA]
 *   fatimg put   IMG ARCHIVO-HOST RUTA
 *   fatimg get   IMG RUTA ARCHIVO-HOST
 *   fatimg cat   IMG RUTA
 *   fatimg mkdir IMG RUTA
 *   fatimg rm    IMG RUTA
 *   fatimg rmdir IMG RUTA
 *
 * Opción global antes del comando: -s N para el tamaño de sector (512).
 */

#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define fseek64 _fseeki64
#else
#include <sys/types.h>
#define fseek64 fseeko
#endif

#include "fat.h"

typedef struct image {
	FILE		*fp;
	uint32_t	 bps;
} image;

static int
img_read(void *ctx, uint32_t lba, void *buf, uint32_t count)
{
	image *m = ctx;

	if (fseek64(m->fp, (long long)lba * m->bps, SEEK_SET))
		return -1;
	return fread(buf, m->bps, count, m->fp) == count ? 0 : -1;
}

static int
img_write(void *ctx, uint32_t lba, const void *buf, uint32_t count)
{
	image *m = ctx;

	if (fseek64(m->fp, (long long)lba * m->bps, SEEK_SET))
		return -1;
	return fwrite(buf, m->bps, count, m->fp) == count ? 0 : -1;
}

static uint32_t
host_clock(void)
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);

	if (!tm || tm->tm_year < 80)
		return fat_pack_time(1980, 1, 1, 0, 0, 0);
	return fat_pack_time(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
	    tm->tm_hour, tm->tm_min, tm->tm_sec);
}

static void
die(const char *what, int err)
{
	fprintf(stderr, "fatimg: %s: %s\n", what, fs_strerror(err));
	exit(1);
}

static void
usage(void)
{
	fprintf(stderr,
	    "uso: fatimg [-s SECTOR] mkfs IMG TAMAÑO [12|16|32|auto] [ETIQUETA]\n"
	    "     fatimg [-s SECTOR] info|ls IMG [RUTA]\n"
	    "     fatimg [-s SECTOR] put IMG ARCHIVO-HOST RUTA\n"
	    "     fatimg [-s SECTOR] get IMG RUTA ARCHIVO-HOST\n"
	    "     fatimg [-s SECTOR] cat|mkdir|rm|rmdir IMG RUTA\n");
	exit(2);
}

static unsigned long long
parse_size(const char *s)
{
	char *end;
	unsigned long long v = strtoull(s, &end, 10);

	switch (*end) {
	case 'k': case 'K': v <<= 10; break;
	case 'm': case 'M': v <<= 20; break;
	case 'g': case 'G': v <<= 30; break;
	case '\0': break;
	default: usage();
	}
	return v;
}

static void
open_image(image *m, fat_blockdev *dev, const char *path, uint32_t bps)
{
	long long size;

	if (!(m->fp = fopen(path, "r+b"))) {
		perror(path);
		exit(1);
	}
	fseek64(m->fp, 0, SEEK_END);
#ifdef _WIN32
	size = _ftelli64(m->fp);
#else
	size = ftello(m->fp);
#endif
	m->bps = bps;
	dev->ctx = m;
	dev->sector_size = bps;
	dev->sector_count = (uint32_t)(size / bps);
	dev->read = img_read;
	dev->write = img_write;
}

static void
mount_image(fat_fs *fs, image *m, fat_blockdev *dev, const char *path, uint32_t bps)
{
	int r;

	open_image(m, dev, path, bps);
	if ((r = fat_mount_first(fs, dev)))
		die(path, r);
	fs->clock = host_clock;
}

static void
print_time(uint32_t t)
{
	uint32_t d = t >> 16;

	printf("%04u-%02u-%02u %02u:%02u", 1980 + (d >> 9), (d >> 5) & 15, d & 31,
	    (t >> 11) & 31, (t >> 5) & 63);
}

static int
cmd_mkfs(int argc, char **argv, uint32_t bps)
{
	fat_mkfs_opts o = { 0 };
	unsigned long long bytes;
	fat_blockdev dev;
	fat_fs fs;
	image m;
	FILE *fp;
	int r;

	if (argc < 2)
		usage();
	bytes = parse_size(argv[1]);
	if (bytes < bps || bytes / bps > 0xFFFFFFFFull)
		die("tamaño", FS_EINVAL);
	if (argc > 2 && strcmp(argv[2], "auto") != 0)
		o.type = (uint8_t)atoi(argv[2]);
	if (argc > 3)
		o.label = argv[3];
	o.volume_id = (uint32_t)time(NULL);

	/* Archivo del tamaño justo: lo demás lo deja disperso el sistema. */
	if (!(fp = fopen(argv[0], "wb"))) {
		perror(argv[0]);
		return 1;
	}
	if (fseek64(fp, (long long)(bytes - 1), SEEK_SET) || fputc(0, fp) == EOF || fclose(fp)) {
		perror(argv[0]);
		return 1;
	}

	open_image(&m, &dev, argv[0], bps);
	if ((r = fat_mkfs(&fs, &dev, &o)))
		die("mkfs", r);
	if ((r = fat_mount(&fs, &dev, 0)))
		die("mount", r);
	printf("%s: FAT%u, %u clusters de %u bytes\n", argv[0], fs.type,
	    fs.cluster_count, fs.sec_per_clus * fs.bytes_per_sec);
	fclose(m.fp);
	return 0;
}

int
main(int argc, char **argv)
{
	fat_blockdev dev;
	fat_volinfo vi;
	fat_dirent e;
	fat_file f;
	fat_dir d;
	fat_fs fs;
	image m;
	uint32_t bps = 512;
	const char *cmd;
	char buf[8192];
	FILE *fp;
	int32_t n;
	int r = 0;

	if (argc > 2 && strcmp(argv[1], "-s") == 0) {
		bps = (uint32_t)atoi(argv[2]);
		argc -= 2;
		argv += 2;
	}
	if (argc < 3)
		usage();
	cmd = argv[1];
	argc -= 2;
	argv += 2;

	if (strcmp(cmd, "mkfs") == 0)
		return cmd_mkfs(argc, argv, bps);

	mount_image(&fs, &m, &dev, argv[0], bps);

	if (strcmp(cmd, "info") == 0) {
		if ((r = fat_volume_info(&fs, &vi)))
			die("info", r);
		printf("tipo        FAT%u\n", vi.type);
		printf("partición   LBA %u\n", fs.part_lba);
		printf("sector      %u bytes\n", fs.bytes_per_sec);
		printf("cluster     %u bytes\n", vi.cluster_size);
		printf("clusters    %u (%u libres)\n", vi.total_clusters, vi.free_clusters);
		printf("libre       %llu bytes\n",
		    (unsigned long long)vi.free_clusters * vi.cluster_size);
	} else if (strcmp(cmd, "ls") == 0) {
		if ((r = fat_opendir(&fs, &d, argc > 1 ? argv[1] : "/")))
			die("ls", r);
		while ((r = fat_readdir(&d, &e)) == 1) {
			print_time(e.mtime);
			if (e.attr & FAT_ATTR_DIR)
				printf("  %10s  %s/\n", "<DIR>", e.name);
			else
				printf("  %10u  %s\n", e.size, e.name);
		}
		if (r < 0)
			die("ls", r);
	} else if (strcmp(cmd, "put") == 0 && argc == 3) {
		if (!(fp = fopen(argv[1], "rb"))) {
			perror(argv[1]);
			return 1;
		}
		if ((r = fat_open(&fs, &f, argv[2], FS_O_WRONLY | FS_O_CREAT | FS_O_TRUNC)))
			die(argv[2], r);
		while ((n = (int32_t)fread(buf, 1, sizeof(buf), fp)) > 0)
			if (fat_write(&f, buf, (uint32_t)n) != n)
				die(argv[2], FS_ENOSPC);
		fclose(fp);
		if ((r = fat_close(&f)))
			die(argv[2], r);
	} else if ((strcmp(cmd, "get") == 0 && argc == 3) || (strcmp(cmd, "cat") == 0 && argc == 2)) {
		if ((r = fat_open(&fs, &f, argv[1], FS_O_RDONLY)))
			die(argv[1], r);
		if (cmd[0] == 'c') {
#ifdef _WIN32
			_setmode(_fileno(stdout), _O_BINARY);
#endif
			fp = stdout;
		} else if (!(fp = fopen(argv[2], "wb"))) {
			perror(argv[2]);
			return 1;
		}
		while ((n = fat_read(&f, buf, sizeof(buf))) > 0)
			fwrite(buf, 1, (size_t)n, fp);
		if (n < 0)
			die(argv[1], n);
		if (fp != stdout)
			fclose(fp);
		fat_close(&f);
	} else if (strcmp(cmd, "mkdir") == 0 && argc == 2) {
		if ((r = fat_mkdir(&fs, argv[1])))
			die(argv[1], r);
	} else if (strcmp(cmd, "rm") == 0 && argc == 2) {
		if ((r = fat_unlink(&fs, argv[1])))
			die(argv[1], r);
	} else if (strcmp(cmd, "rmdir") == 0 && argc == 2) {
		if ((r = fat_rmdir(&fs, argv[1])))
			die(argv[1], r);
	} else {
		usage();
	}

	if ((r = fat_unmount(&fs)))
		die("unmount", r);
	fclose(m.fp);
	return 0;
}
