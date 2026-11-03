/*
 * Tests en RAM: no tocan el disco del host. Cada configuración formatea un
 * ramdisk, lo ejercita y verifica invariantes que un fsck también miraría:
 * las dos FATs idénticas y ningún cluster perdido al borrar todo.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fat_vfs.h"

static int failures, checks;

#define CHECK(cond) do {						\
	checks++;							\
	if (!(cond)) {							\
		failures++;						\
		fprintf(stderr, "  FALLA %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	}								\
} while (0)

#define CHECK_EQ(a, b) do {						\
	long long a_ = (long long)(a), b_ = (long long)(b);		\
	checks++;							\
	if (a_ != b_) {							\
		failures++;						\
		fprintf(stderr, "  FALLA %s:%d: %s == %lld, se esperaba %lld\n", \
		    __FILE__, __LINE__, #a, a_, b_);			\
	}								\
} while (0)

/* --- Ramdisk ---------------------------------------------------------------- */

typedef struct ramdisk {
	uint8_t		*data;
	uint32_t	 bps;
} ramdisk;

static int
rd_read(void *ctx, uint32_t lba, void *buf, uint32_t count)
{
	ramdisk *r = ctx;

	memcpy(buf, r->data + (size_t)lba * r->bps, (size_t)count * r->bps);
	return 0;
}

static int
rd_write(void *ctx, uint32_t lba, const void *buf, uint32_t count)
{
	ramdisk *r = ctx;

	memcpy(r->data + (size_t)lba * r->bps, buf, (size_t)count * r->bps);
	return 0;
}

static void
rd_init(ramdisk *r, fat_blockdev *dev, uint32_t bps, uint32_t sectors)
{
	r->bps = bps;
	r->data = calloc(sectors, bps);
	if (!r->data) {
		fprintf(stderr, "sin memoria\n");
		exit(2);
	}
	/* Basura en vez de ceros: un formateo que se olvide de limpiar algo
	 * tiene que notarse. */
	memset(r->data, 0xA5, (size_t)sectors * bps);
	dev->ctx = r;
	dev->sector_size = bps;
	dev->sector_count = sectors;
	dev->read = rd_read;
	dev->write = rd_write;
}

/* --- Ayudantes ---------------------------------------------------------------- */

static uint8_t
pattern(uint32_t i, uint32_t seed)
{
	uint32_t x = (i + 1) * 2654435761u ^ seed;

	return (uint8_t)(x >> 13);
}

static int32_t
write_pattern(fat_fs *fs, const char *path, uint32_t size, uint32_t seed)
{
	uint8_t buf[777];	/* tamaño raro a propósito: cruza sectores en cualquier punto */
	uint32_t done = 0, n, i;
	fat_file f;
	int32_t r;

	if ((r = fat_open(fs, &f, path, FS_O_WRONLY | FS_O_CREAT | FS_O_TRUNC)))
		return r;
	while (done < size) {
		n = size - done < sizeof(buf) ? size - done : (uint32_t)sizeof(buf);
		for (i = 0; i < n; i++)
			buf[i] = pattern(done + i, seed);
		if ((r = fat_write(&f, buf, n)) != (int32_t)n) {
			fat_close(&f);
			return r < 0 ? r : FS_EIO;
		}
		done += n;
	}
	return fat_close(&f);
}

static int
verify_pattern(fat_fs *fs, const char *path, uint32_t size, uint32_t seed)
{
	uint8_t buf[1000];
	uint32_t done = 0, i;
	fat_file f;
	int32_t r;

	if ((r = fat_open(fs, &f, path, FS_O_RDONLY)))
		return r;
	if (f.size != size) {
		fat_close(&f);
		return -100;
	}
	while ((r = fat_read(&f, buf, sizeof(buf))) > 0) {
		for (i = 0; i < (uint32_t)r; i++)
			if (buf[i] != pattern(done + i, seed)) {
				fat_close(&f);
				return -101;
			}
		done += (uint32_t)r;
	}
	fat_close(&f);
	return r < 0 ? r : (done == size ? 0 : -102);
}

static uint32_t
free_clusters(fat_fs *fs)
{
	fat_volinfo vi;

	if (fat_volume_info(fs, &vi))
		return 0;
	return vi.free_clusters;
}

static int
fats_identical(const ramdisk *rd, const fat_fs *fs)
{
	size_t len = (size_t)fs->fat_size * fs->bytes_per_sec;
	const uint8_t *a = rd->data + (size_t)(fs->part_lba + fs->fat_lba) * rd->bps;

	return memcmp(a, a + len, len) == 0;
}

static int
count_entries(fat_fs *fs, const char *path)
{
	fat_dirent e;
	fat_dir d;
	int n = 0, r;

	if ((r = fat_opendir(fs, &d, path)))
		return r;
	while ((r = fat_readdir(&d, &e)) == 1)
		n++;
	return r < 0 ? r : n;
}

/* --- Suite por configuración ----------------------------------------------------- */

static void
test_config(uint8_t type, uint32_t bps, uint32_t sectors, uint32_t big)
{
	fat_mkfs_opts o = { 0 };
	fat_blockdev dev;
	ramdisk rd;
	fat_fs fs;
	fat_file f;
	fat_info st;
	uint8_t buf[4096], zero[4096];
	uint32_t free0, i;
	int32_t r;
	char name[32];
	int n, made;

	printf("FAT%u, sector %u, %u sectores\n", type, bps, sectors);
	if (bps > FAT_MAX_SECTOR_SIZE) {
		printf("  salteado: compilado con FAT_MAX_SECTOR_SIZE=%d\n", FAT_MAX_SECTOR_SIZE);
		return;
	}
	rd_init(&rd, &dev, bps, sectors);

	o.type = type;
	o.label = "kata";
	CHECK_EQ(fat_mkfs(&fs, &dev, &o), FS_OK);
	CHECK_EQ(fat_mount(&fs, &dev, 0), FS_OK);
	CHECK_EQ(fs.type, type);
	free0 = free_clusters(&fs);
	CHECK(free0 > 0);
	CHECK_EQ(count_entries(&fs, "/"), 0);

	/* Directorios anidados y errores de nombre. */
	CHECK_EQ(fat_mkdir(&fs, "/DIR1"), FS_OK);
	CHECK_EQ(fat_mkdir(&fs, "/dir1/sub"), FS_OK);	/* sin distinguir mayúsculas */
	CHECK_EQ(fat_mkdir(&fs, "/DIR1"), FS_EEXIST);
	CHECK_EQ(fat_mkdir(&fs, "/NOPE/X"), FS_ENOENT);
	CHECK_EQ(fat_mkdir(&fs, "/nombredemasiadolargo"), FS_ENAMETOOLONG);
	CHECK_EQ(fat_mkdir(&fs, "/.oculto"), FS_EINVAL);
	CHECK_EQ(fat_mkdir(&fs, "/a b"), FS_EINVAL);
	CHECK_EQ(fat_stat(&fs, "/DIR1/SUB/..", &st), FS_OK);
	CHECK(st.attr & FAT_ATTR_DIR);
	CHECK_EQ(fat_stat(&fs, "/DIR1/SUB/../..", &st), FS_OK);	/* vuelve a la raíz */

	/* Archivo grande en chunks irregulares, leído con otro tamaño de chunk. */
	CHECK_EQ(write_pattern(&fs, "/DIR1/SUB/BIG.BIN", big, 7), FS_OK);
	CHECK_EQ(verify_pattern(&fs, "/dir1/sub/big.bin", big, 7), 0);

	/* Seek + lectura parcial a mitad de archivo. */
	CHECK_EQ(fat_open(&fs, &f, "/DIR1/SUB/BIG.BIN", FS_O_RDONLY), FS_OK);
	CHECK_EQ(fat_seek(&f, 123457, FS_SEEK_SET), 123457);
	CHECK_EQ(fat_read(&f, buf, 3000), 3000);
	for (i = 0; i < 3000; i++)
		if (buf[i] != pattern(123457 + i, 7))
			break;
	CHECK_EQ(i, 3000);
	CHECK_EQ(fat_seek(&f, -10, FS_SEEK_END), big - 10);
	CHECK_EQ(fat_read(&f, buf, sizeof(buf)), 10);
	CHECK_EQ(fat_read(&f, buf, sizeof(buf)), 0);
	CHECK_EQ(fat_write(&f, buf, 1), FS_EBADF);	/* abierto sólo lectura */
	CHECK_EQ(fat_close(&f), FS_OK);

	/* Sobrescritura en el medio y append. */
	CHECK_EQ(fat_open(&fs, &f, "/DIR1/SUB/BIG.BIN", FS_O_RDWR), FS_OK);
	CHECK_EQ(fat_seek(&f, 5000, FS_SEEK_SET), 5000);
	CHECK_EQ(fat_write(&f, "HOLA", 4), 4);
	CHECK_EQ(fat_close(&f), FS_OK);
	CHECK_EQ(fat_open(&fs, &f, "/DIR1/SUB/BIG.BIN", FS_O_WRONLY | FS_O_APPEND), FS_OK);
	CHECK_EQ(fat_write(&f, "FIN", 3), 3);
	CHECK_EQ(fat_close(&f), FS_OK);
	CHECK_EQ(fat_open(&fs, &f, "/DIR1/SUB/BIG.BIN", FS_O_RDONLY), FS_OK);
	CHECK_EQ(f.size, big + 3);
	fat_seek(&f, 4999, FS_SEEK_SET);
	CHECK_EQ(fat_read(&f, buf, 6), 6);
	CHECK(memcmp(buf + 1, "HOLA", 4) == 0 && buf[0] == pattern(4999, 7) &&
	    buf[5] == pattern(5004, 7));
	fat_seek(&f, -3, FS_SEEK_END);
	CHECK_EQ(fat_read(&f, buf, 3), 3);
	CHECK(memcmp(buf, "FIN", 3) == 0);
	fat_close(&f);

	/* Escribir más allá del final deja un hueco de ceros. */
	CHECK_EQ(fat_open(&fs, &f, "/HUECO.DAT", FS_O_RDWR | FS_O_CREAT), FS_OK);
	CHECK_EQ(fat_seek(&f, 10000, FS_SEEK_SET), 10000);
	CHECK_EQ(fat_write(&f, "X", 1), 1);
	CHECK_EQ(f.size, 10001);
	fat_seek(&f, 0, FS_SEEK_SET);
	memset(zero, 0, sizeof(zero));
	for (i = 0; i < 10000; i += (uint32_t)r) {
		r = fat_read(&f, buf, 10000 - i < sizeof(buf) ? 10000 - i : (uint32_t)sizeof(buf));
		if (r <= 0 || memcmp(buf, zero, (size_t)r) != 0)
			break;
	}
	CHECK_EQ(i, 10000);
	CHECK_EQ(fat_read(&f, buf, 1), 1);
	CHECK_EQ(buf[0], 'X');
	fat_close(&f);

	/* O_EXCL, O_TRUNC, archivo vacío. */
	CHECK_EQ(fat_open(&fs, &f, "/HUECO.DAT", FS_O_WRONLY | FS_O_CREAT | FS_O_EXCL), FS_EEXIST);
	CHECK_EQ(fat_open(&fs, &f, "/VACIO", FS_O_WRONLY | FS_O_CREAT), FS_OK);
	CHECK_EQ(fat_close(&f), FS_OK);
	CHECK_EQ(fat_stat(&fs, "/VACIO", &st), FS_OK);
	CHECK_EQ(st.size, 0);
	CHECK_EQ(st.first_clus, 0);
	CHECK_EQ(fat_open(&fs, &f, "/NOEXISTE", FS_O_RDONLY), FS_ENOENT);
	CHECK_EQ(fat_open(&fs, &f, "/DIR1", FS_O_RDONLY), FS_EISDIR);
	CHECK_EQ(fat_open(&fs, &f, "/HUECO.DAT/X", FS_O_RDONLY), FS_ENOTDIR);

	/* Remontar: todo lo anterior tiene que estar en el medio, no en caché. */
	CHECK_EQ(fat_unmount(&fs), FS_OK);
	CHECK_EQ(fat_mount(&fs, &dev, 0), FS_OK);
	CHECK(fats_identical(&rd, &fs));
	CHECK_EQ(fat_stat(&fs, "/DIR1/SUB/BIG.BIN", &st), FS_OK);
	CHECK_EQ(st.size, big + 3);
	CHECK_EQ(fat_stat(&fs, "/HUECO.DAT", &st), FS_OK);
	CHECK_EQ(st.size, 10001);
	CHECK_EQ(count_entries(&fs, "/"), 3);	/* DIR1, HUECO.DAT, VACIO */

	/* Muchos archivos en un subdirectorio: lo obliga a crecer varios clusters. */
	for (i = 0; i < 100; i++) {
		snprintf(name, sizeof(name), "/DIR1/F%03u.TXT", i);
		if (fat_open(&fs, &f, name, FS_O_WRONLY | FS_O_CREAT) || fat_write(&f, name, 4) != 4 ||
		    fat_close(&f))
			break;
	}
	CHECK_EQ(i, 100);
	CHECK_EQ(count_entries(&fs, "/DIR1"), 101);	/* + SUB */
	CHECK_EQ(verify_pattern(&fs, "/DIR1/SUB/BIG.BIN", big + 3, 7), -101);	/* el "HOLA" sigue */

	/* Llenar la raíz: fija en FAT12/16 (ENOSPC), en cadena en FAT32 (crece). */
	made = 0;
	for (i = 0; i < 600; i++) {
		snprintf(name, sizeof(name), "/R%05u", i);
		r = fat_open(&fs, &f, name, FS_O_WRONLY | FS_O_CREAT);
		if (r)
			break;
		fat_close(&f);
		made++;
	}
	if (type == 32) {
		CHECK_EQ(made, 600);
	} else {
		CHECK_EQ(r, FS_ENOSPC);
		CHECK_EQ(made + 3, bps == 512 && type == 12 && sectors <= 2880 ? 224 : 512);
	}

	/* Borrados y sus errores. */
	CHECK_EQ(fat_rmdir(&fs, "/DIR1"), FS_ENOTEMPTY);
	CHECK_EQ(fat_unlink(&fs, "/DIR1"), FS_EISDIR);
	CHECK_EQ(fat_rmdir(&fs, "/HUECO.DAT"), FS_ENOTDIR);
	CHECK_EQ(fat_rmdir(&fs, "/"), FS_EINVAL);
	for (i = 0; i < 100; i++) {
		snprintf(name, sizeof(name), "/DIR1/F%03u.TXT", i);
		if (fat_unlink(&fs, name))
			break;
	}
	CHECK_EQ(i, 100);
	for (i = 0; i < (uint32_t)made; i++) {
		snprintf(name, sizeof(name), "/R%05u", i);
		if (fat_unlink(&fs, name))
			break;
	}
	CHECK_EQ(i, (uint32_t)made);
	CHECK_EQ(fat_unlink(&fs, "/DIR1/SUB/BIG.BIN"), FS_OK);
	CHECK_EQ(fat_unlink(&fs, "/DIR1/SUB/BIG.BIN"), FS_ENOENT);
	CHECK_EQ(fat_rmdir(&fs, "/DIR1/SUB"), FS_OK);
	CHECK_EQ(fat_rmdir(&fs, "/DIR1"), FS_OK);
	CHECK_EQ(fat_unlink(&fs, "/HUECO.DAT"), FS_OK);
	CHECK_EQ(fat_unlink(&fs, "/VACIO"), FS_OK);
	CHECK_EQ(count_entries(&fs, "/"), 0);

	/* Ningún cluster perdido... salvo los que la raíz de FAT32 ganó al crecer,
	 * que se quedan con el directorio, como en cualquier implementación. */
	if (type == 32)
		CHECK(free_clusters(&fs) <= free0 && free_clusters(&fs) + 64 > free0);
	else
		CHECK_EQ(free_clusters(&fs), free0);

	/* Llenar el volumen hasta ENOSPC y vaciarlo. */
	free0 = free_clusters(&fs);
	CHECK_EQ(fat_open(&fs, &f, "/LLENO.BIN", FS_O_WRONLY | FS_O_CREAT), FS_OK);
	memset(buf, 0x5A, sizeof(buf));
	while ((r = fat_write(&f, buf, sizeof(buf))) == (int32_t)sizeof(buf))
		;
	CHECK(r == FS_ENOSPC || (r >= 0 && r < (int32_t)sizeof(buf)));
	CHECK_EQ(fat_write(&f, buf, sizeof(buf)), FS_ENOSPC);
	CHECK_EQ(fat_close(&f), FS_OK);
	CHECK_EQ(free_clusters(&fs), 0);
	CHECK_EQ(fat_mkdir(&fs, "/NOCABE"), FS_ENOSPC);
	CHECK_EQ(fat_stat(&fs, "/LLENO.BIN", &st), FS_OK);
	CHECK_EQ(st.size, (uint64_t)free0 * fs.sec_per_clus * fs.bytes_per_sec);
	CHECK_EQ(fat_unlink(&fs, "/LLENO.BIN"), FS_OK);
	CHECK_EQ(free_clusters(&fs), free0);

	CHECK_EQ(fat_unmount(&fs), FS_OK);
	CHECK_EQ(fat_mount(&fs, &dev, 0), FS_OK);
	CHECK(fats_identical(&rd, &fs));
	n = count_entries(&fs, "/");
	CHECK_EQ(n, 0);
	free(rd.data);
}

/* --- FAT12 con entradas partidas entre sectores ---------------------------------- */

static void
test_fat12_straddle(void)
{
	fat_mkfs_opts o = { 0 };
	fat_blockdev dev;
	ramdisk rd;
	fat_fs fs;
	uint32_t c;

	printf("FAT12: entradas de 12 bits que cruzan el límite de sector\n");
	rd_init(&rd, &dev, 512, 2880);
	o.type = 12;
	CHECK_EQ(fat_mkfs(&fs, &dev, &o), FS_OK);
	CHECK_EQ(fat_mount(&fs, &dev, 0), FS_OK);

	/* 512 bytes * 2 / 3: el cluster 341 empieza en el byte 511 del primer
	 * sector y termina en el segundo. Un archivo de 400 clusters lo cubre. */
	CHECK_EQ(write_pattern(&fs, "/CRUZA.BIN", 400 * 512, 3), FS_OK);
	CHECK_EQ(fat_unmount(&fs), FS_OK);
	CHECK_EQ(fat_mount(&fs, &dev, 0), FS_OK);
	CHECK_EQ(verify_pattern(&fs, "/CRUZA.BIN", 400 * 512, 3), 0);
	c = 341 + 341 / 2;
	CHECK_EQ(c, 511);
	CHECK(fats_identical(&rd, &fs));
	free(rd.data);
}

/* --- Volumen dentro de una partición MBR ---------------------------------------- */

static void
test_mbr(void)
{
	fat_mkfs_opts o = { 0 };
	fat_blockdev dev;
	ramdisk rd;
	fat_fs fs;
	uint8_t *mbr;

	printf("MBR: volumen FAT32 en la partición 1, desde el LBA 2048\n");
	rd_init(&rd, &dev, 512, 2048 + 81920);
	mbr = rd.data;
	memset(mbr, 0, 512);
	mbr[446 + 4] = 0x0C;		/* FAT32 LBA */
	mbr[446 + 8] = 0x00;		/* 2048 en little-endian */
	mbr[446 + 9] = 0x08;
	mbr[446 + 12] = 0x00;		/* 81920 sectores */
	mbr[446 + 13] = 0x40;
	mbr[446 + 14] = 0x01;
	mbr[510] = 0x55;
	mbr[511] = 0xAA;

	o.type = 32;
	o.part_lba = 2048;
	CHECK_EQ(fat_mkfs(&fs, &dev, &o), FS_OK);
	CHECK_EQ(fat_mount(&fs, &dev, 0), FS_ENOFS);
	CHECK_EQ(fat_mount_first(&fs, &dev), FS_OK);
	CHECK_EQ(fs.part_lba, 2048);
	CHECK_EQ(fs.type, 32);
	CHECK_EQ(write_pattern(&fs, "/BOOT.TXT", 1234, 9), FS_OK);
	CHECK_EQ(verify_pattern(&fs, "/BOOT.TXT", 1234, 9), 0);
	CHECK_EQ(mbr[446 + 4], 0x0C);	/* el MBR quedó intacto */
	free(rd.data);
}

/* --- Formateo automático y geometría --------------------------------------------- */

static void
test_auto_type(void)
{
	static const struct { uint32_t sectors; uint8_t want; } cases[] = {
		/* 1,44 MB y 8 MB: FAT12; 32 MB: FAT16. FAT32 automático empieza
		 * en 512 MB, demasiado para un ramdisk de test. */
		{ 2880, 12 }, { 16384, 12 }, { 65536, 16 }
	};
	fat_mkfs_opts o = { 0 };
	fat_blockdev dev;
	ramdisk rd;
	fat_fs fs;
	size_t i;

	printf("Formateo automático: el tipo sale del tamaño\n");
	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		rd_init(&rd, &dev, 512, cases[i].sectors);
		CHECK_EQ(fat_mkfs(&fs, &dev, &o), FS_OK);
		CHECK_EQ(fat_mount(&fs, &dev, 0), FS_OK);
		CHECK_EQ(fs.type, cases[i].want);
		free(rd.data);
	}

	/* 1,44 MB: la geometría clásica del disquete. */
	rd_init(&rd, &dev, 512, 2880);
	CHECK_EQ(fat_mkfs(&fs, &dev, &o), FS_OK);
	CHECK_EQ(fat_mount(&fs, &dev, 0), FS_OK);
	CHECK_EQ(fs.sec_per_clus, 1);
	CHECK_EQ(fs.fat_size, 9);
	CHECK_EQ(fs.root_secs, 14);
	CHECK_EQ(fs.cluster_count, 2847);
	free(rd.data);

	/* Pedir FAT32 en 1,44 MB es imposible: no alcanzan los clusters. */
	rd_init(&rd, &dev, 512, 2880);
	o.type = 32;
	CHECK_EQ(fat_mkfs(&fs, &dev, &o), FS_EINVAL);
	CHECK_EQ(fat_mount(&fs, &dev, 0), FS_ENOFS);	/* sin formato: basura */
	free(rd.data);
}

/* --- VFS ------------------------------------------------------------------------- */

static void
test_vfs(void)
{
	fat_mkfs_opts o = { 0 };
	fat_blockdev d0, d1;
	ramdisk r0, r1;
	fat_fs fd0, sd;
	vfs_file a, b;
	vfs_dir dir;
	vfs_dirent ent;
	vfs_info st;
	char buf[64];
	int32_t n;
	int seen = 0;

	printf("VFS: disquete en /fd0, SD en /sd\n");
	rd_init(&r0, &d0, 512, 2880);
	rd_init(&r1, &d1, 512, 81920);
	o.type = 12;
	CHECK_EQ(fat_mkfs(&fd0, &d0, &o), FS_OK);
	o.type = 32;
	CHECK_EQ(fat_mkfs(&sd, &d1, &o), FS_OK);
	CHECK_EQ(fat_mount(&fd0, &d0, 0), FS_OK);
	CHECK_EQ(fat_mount(&sd, &d1, 0), FS_OK);

	CHECK_EQ(vfs_mount("/fd0", &fat_vfs_ops, &fd0), FS_OK);
	CHECK_EQ(vfs_mount("/sd/", &fat_vfs_ops, &sd), FS_OK);	/* la '/' final se ignora */
	CHECK_EQ(vfs_mount("/sd", &fat_vfs_ops, &sd), FS_EEXIST);
	CHECK_EQ(vfs_mount("sin-barra", &fat_vfs_ops, &sd), FS_EINVAL);

	CHECK_EQ(vfs_open(&a, "/fd0/HOLA.TXT", FS_O_WRONLY | FS_O_CREAT), FS_OK);
	CHECK_EQ(vfs_write(&a, "hola, mundo", 11), 11);
	CHECK_EQ(vfs_close(&a), FS_OK);
	CHECK_EQ(vfs_close(&a), FS_EBADF);

	/* Copiar entre volúmenes de distinto tipo. */
	CHECK_EQ(vfs_mkdir("/sd/LOGS"), FS_OK);
	CHECK_EQ(vfs_open(&a, "/fd0/hola.txt", FS_O_RDONLY), FS_OK);
	CHECK_EQ(vfs_open(&b, "/sd/LOGS/HOLA.TXT", FS_O_WRONLY | FS_O_CREAT), FS_OK);
	while ((n = vfs_read(&a, buf, 4)) > 0)
		CHECK_EQ(vfs_write(&b, buf, (uint32_t)n), n);
	vfs_close(&a);
	vfs_close(&b);

	CHECK_EQ(vfs_stat("/sd/LOGS/HOLA.TXT", &st), FS_OK);
	CHECK_EQ(st.size, 11);
	CHECK_EQ(st.is_dir, 0);
	CHECK_EQ(vfs_stat("/sd", &st), FS_OK);
	CHECK_EQ(st.is_dir, 1);
	CHECK_EQ(vfs_stat("/sdcard/X", &st), FS_ENOENT);	/* no confunde prefijos */
	CHECK_EQ(vfs_stat("/otro", &st), FS_ENOENT);

	CHECK_EQ(vfs_opendir(&dir, "/sd/LOGS"), FS_OK);
	while (vfs_readdir(&dir, &ent) == 1)
		seen += strcmp(ent.name, "HOLA.TXT") == 0 && ent.size == 11;
	CHECK_EQ(seen, 1);

	CHECK_EQ(vfs_open(&a, "/sd/LOGS/HOLA.TXT", FS_O_RDONLY), FS_OK);
	CHECK_EQ(vfs_seek(&a, 6, FS_SEEK_SET), 6);
	CHECK_EQ(vfs_read(&a, buf, sizeof(buf)), 5);
	CHECK(memcmp(buf, "mundo", 5) == 0);
	vfs_close(&a);

	CHECK_EQ(vfs_rmdir("/sd/LOGS"), FS_ENOTEMPTY);
	CHECK_EQ(vfs_unlink("/sd/LOGS/HOLA.TXT"), FS_OK);
	CHECK_EQ(vfs_rmdir("/sd/LOGS"), FS_OK);

	/* Una raíz "/" convive con montajes más específicos. */
	CHECK_EQ(vfs_mount("/", &fat_vfs_ops, &sd), FS_OK);
	CHECK_EQ(vfs_stat("/fd0/HOLA.TXT", &st), FS_OK);	/* sigue yendo al disquete */
	CHECK_EQ(vfs_mkdir("/RAIZ"), FS_OK);
	CHECK_EQ(vfs_stat("/sd/RAIZ", &st), FS_OK);		/* mismo volumen */

	CHECK_EQ(vfs_unmount("/"), FS_OK);
	CHECK_EQ(vfs_unmount("/fd0"), FS_OK);
	CHECK_EQ(vfs_unmount("/sd"), FS_OK);
	CHECK_EQ(vfs_unmount("/sd"), FS_ENOENT);
	free(r0.data);
	free(r1.data);
}

int
main(void)
{
	test_config(12, 512, 2880, 300000);		/* disquete de 1,44 MB */
	test_config(12, 4096, 1024, 300000);
	test_config(16, 512, 32768, 1000000);		/* 16 MB */
	test_config(16, 4096, 16384, 1000000);		/* 64 MB */
	test_config(32, 512, 81920, 2000000);		/* 40 MB */
	test_fat12_straddle();
	test_mbr();
	test_auto_type();
	test_vfs();

	printf("\n%d/%d comprobaciones OK\n", checks - failures, checks);
	return failures ? 1 : 0;
}
