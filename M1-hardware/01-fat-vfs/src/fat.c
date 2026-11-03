/*
 * FAT12 / FAT16 / FAT32.
 *
 * Referencia: Microsoft, "FAT: General Overview of On-Disk Format" (fatgen103).
 * Todos los campos en disco son little-endian y se leen byte a byte, así que el
 * código no depende del endianness ni de la alineación del procesador.
 *
 * Mapa de un volumen:
 *
 *   | reservados | FAT #1 | FAT #2 | raíz fija (12/16) | datos: cluster 2, 3, ... |
 *   0            fat_lba                root_lba          data_lba
 *
 * En FAT32 no hay raíz fija: la raíz es una cadena de clusters más.
 */

#include <string.h>

#include "fat.h"

#define CACHE_NONE	0xFFFFFFFFu

/* Valores de FAT normalizados: los tres tipos se traducen a éstos. */
#define CLUS_FREE	0x00000000u
#define CLUS_BAD	0x0FFFFFF7u
#define CLUS_EOC	0x0FFFFFFFu

#define DIRENT_SIZE	32
#define DE_FREE		0xE5
#define DE_END		0x00

typedef struct fat_node {
	uint32_t	clus;
	uint32_t	size;
	uint32_t	mtime;
	uint32_t	lba;		/* entrada propia; válido si has_loc */
	uint16_t	off;
	uint8_t		attr;
	uint8_t		is_root;
	uint8_t		has_loc;
} fat_node;

/* --- Little-endian ----------------------------------------------------------- */

static uint16_t
rd16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t
rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void
wr16(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void
wr32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

/* --- Caché de un sector -------------------------------------------------------
 *
 * Un solo sector, write-back. Es lo mínimo que hace falta: las entradas de
 * FAT12 ocupan 1,5 bytes y pueden quedar partidas entre dos sectores, y una
 * entrada de directorio se modifica in situ. Cuando el sector sucio cae dentro
 * de la FAT #1, el flush lo escribe también en las demás copias: así las FATs
 * quedan siempre idénticas sin que el resto del código tenga que acordarse.
 */

static int
dev_rw(fat_fs *fs, uint32_t lba, int write)
{
	fat_blockdev *d = fs->dev;

	if (write)
		return d->write(d->ctx, fs->part_lba + lba, fs->cache, 1) ? FS_EIO : FS_OK;
	return d->read(d->ctx, fs->part_lba + lba, fs->cache, 1) ? FS_EIO : FS_OK;
}

static int
cache_flush(fat_fs *fs)
{
	uint32_t i;

	if (!fs->cache_dirty)
		return FS_OK;
	if (dev_rw(fs, fs->cache_lba, 1))
		return FS_EIO;
	if (fs->fat_size && fs->cache_lba >= fs->fat_lba &&
	    fs->cache_lba < fs->fat_lba + fs->fat_size) {
		for (i = 1; i < fs->num_fats; i++)
			if (dev_rw(fs, fs->cache_lba + i * fs->fat_size, 1))
				return FS_EIO;
	}
	fs->cache_dirty = 0;
	return FS_OK;
}

static uint8_t *
cache_get(fat_fs *fs, uint32_t lba)
{
	if (fs->cache_lba == lba)
		return fs->cache;
	if (cache_flush(fs))
		return NULL;
	fs->cache_lba = CACHE_NONE;
	if (dev_rw(fs, lba, 0))
		return NULL;
	fs->cache_lba = lba;
	return fs->cache;
}

/*
 * Toma el sector sin leerlo, para sobrescribirlo entero. No toca el contenido
 * del buffer: fat_mkfs lo aprovecha para escribir el mismo sector en dos LBAs.
 */
static uint8_t *
cache_claim(fat_fs *fs, uint32_t lba)
{
	if (fs->cache_lba != lba) {
		if (cache_flush(fs))
			return NULL;
		fs->cache_lba = lba;
	}
	fs->cache_dirty = 1;
	return fs->cache;
}

/* --- Tabla FAT ---------------------------------------------------------------- */

static int
clus_valid(const fat_fs *fs, uint32_t c)
{
	return c >= 2 && c < fs->cluster_count + 2;
}

static uint32_t
clus_lba(const fat_fs *fs, uint32_t c)
{
	return fs->data_lba + (c - 2) * fs->sec_per_clus;
}

/* Lee un byte de la FAT #1 en el offset dado (FAT12 los necesita sueltos). */
static int
fat_byte(fat_fs *fs, uint32_t off, uint8_t **out)
{
	uint8_t *p = cache_get(fs, fs->fat_lba + off / fs->bytes_per_sec);

	if (!p)
		return FS_EIO;
	*out = p + off % fs->bytes_per_sec;
	return FS_OK;
}

static int
fat_get(fat_fs *fs, uint32_t c, uint32_t *out)
{
	uint8_t *p;
	uint32_t v, off;
	int r;

	if (!clus_valid(fs, c))
		return FS_EIO;

	switch (fs->type) {
	case 12:
		/* 12 bits: el cluster c empieza en el byte c * 1,5. */
		off = c + c / 2;
		if ((r = fat_byte(fs, off, &p)))
			return r;
		v = *p;
		if ((r = fat_byte(fs, off + 1, &p)))
			return r;
		v |= (uint32_t)*p << 8;
		v = (c & 1) ? v >> 4 : v & 0xFFF;
		if (v >= 0xFF8)
			v = CLUS_EOC;
		else if (v == 0xFF7)
			v = CLUS_BAD;
		break;
	case 16:
		if ((r = fat_byte(fs, c * 2, &p)))
			return r;
		v = rd16(p);
		if (v >= 0xFFF8)
			v = CLUS_EOC;
		else if (v == 0xFFF7)
			v = CLUS_BAD;
		break;
	default:
		/* Los 4 bits altos de FAT32 están reservados y se ignoran. */
		if ((r = fat_byte(fs, c * 4, &p)))
			return r;
		v = rd32(p) & 0x0FFFFFFF;
		if (v >= 0x0FFFFFF8)
			v = CLUS_EOC;
		break;
	}
	*out = v;
	return FS_OK;
}

static int
fat_set(fat_fs *fs, uint32_t c, uint32_t v)
{
	uint8_t *p;
	uint32_t off;
	int r;

	if (!clus_valid(fs, c))
		return FS_EIO;

	switch (fs->type) {
	case 12:
		v &= 0xFFF;
		off = c + c / 2;
		/* Cada mitad del byte compartido pertenece a un cluster distinto:
		 * hay que preservar la otra, y el segundo byte puede estar en el
		 * sector siguiente, por eso se pide de nuevo a la caché. */
		if ((r = fat_byte(fs, off, &p)))
			return r;
		*p = (c & 1) ? (uint8_t)((*p & 0x0F) | (v << 4)) : (uint8_t)v;
		fs->cache_dirty = 1;
		if ((r = fat_byte(fs, off + 1, &p)))
			return r;
		*p = (c & 1) ? (uint8_t)(v >> 4) : (uint8_t)((*p & 0xF0) | (v >> 8));
		break;
	case 16:
		if ((r = fat_byte(fs, c * 2, &p)))
			return r;
		wr16(p, v);
		break;
	default:
		if ((r = fat_byte(fs, c * 4, &p)))
			return r;
		wr32(p, (rd32(p) & 0xF0000000) | (v & 0x0FFFFFFF));
		break;
	}
	fs->cache_dirty = 1;
	return FS_OK;
}

/* Reserva un cluster libre, lo marca fin de cadena y lo engancha tras `prev`. */
static int
clus_alloc(fat_fs *fs, uint32_t prev, uint32_t *out)
{
	uint32_t i, c, v;
	int r;

	c = fs->next_free;
	for (i = 0; i < fs->cluster_count; i++, c++) {
		if (!clus_valid(fs, c))
			c = 2;
		if ((r = fat_get(fs, c, &v)))
			return r;
		if (v != CLUS_FREE)
			continue;
		if ((r = fat_set(fs, c, CLUS_EOC)))
			return r;
		if (prev && (r = fat_set(fs, prev, c)))
			return r;
		fs->next_free = c + 1;
		*out = c;
		return FS_OK;
	}
	return FS_ENOSPC;
}

static int
chain_free(fat_fs *fs, uint32_t c)
{
	uint32_t i, next;
	int r;

	/* El límite de vueltas corta una cadena circular en un disco corrupto. */
	for (i = 0; i <= fs->cluster_count; i++) {
		if (!clus_valid(fs, c))
			return FS_EIO;
		if ((r = fat_get(fs, c, &next)))
			return r;
		if ((r = fat_set(fs, c, CLUS_FREE)))
			return r;
		if (c < fs->next_free)
			fs->next_free = c;
		if (next >= CLUS_BAD)
			return FS_OK;
		c = next;
	}
	return FS_EIO;
}

static int
clus_zero(fat_fs *fs, uint32_t c)
{
	uint32_t i;
	uint8_t *p;

	for (i = 0; i < fs->sec_per_clus; i++) {
		if (!(p = cache_claim(fs, clus_lba(fs, c) + i)))
			return FS_EIO;
		memset(p, 0, fs->bytes_per_sec);
	}
	return FS_OK;
}

/* --- Tiempo y nombres ---------------------------------------------------------- */

uint32_t
fat_pack_time(int year, int mon, int day, int hour, int min, int sec)
{
	uint32_t date = ((uint32_t)(year - 1980) << 9) | ((uint32_t)mon << 5) | (uint32_t)day;
	uint32_t time = ((uint32_t)hour << 11) | ((uint32_t)min << 5) | (uint32_t)(sec / 2);

	return (date << 16) | time;
}

static uint32_t
now(const fat_fs *fs)
{
	return fs->clock ? fs->clock() : fat_pack_time(1980, 1, 1, 0, 0, 0);
}

static int
valid83(char c)
{
	if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
		return 1;
	return c != '\0' && strchr("!#$%&'()-@^_`{}~", c) != NULL;
}

/* "readme.txt" -> "README  TXT". Sin LFN, cualquier otra cosa se rechaza. */
static int
make_83(const char *s, size_t len, uint8_t nm[11])
{
	size_t i, k, dot = len;

	memset(nm, ' ', 11);
	for (i = len; i > 0; i--) {
		if (s[i - 1] == '.') {
			dot = i - 1;
			break;
		}
	}
	if (dot == 0)
		return FS_EINVAL;	/* "" o ".oculto" */
	if (dot > 8 || (dot < len && len - dot - 1 > 3))
		return FS_ENAMETOOLONG;
	for (i = 0, k = 0; i < len; i++) {
		if (i == dot) {
			k = 8;
			continue;
		}
		if (!valid83(s[i]))
			return FS_EINVAL;
		nm[k++] = (uint8_t)((s[i] >= 'a' && s[i] <= 'z') ? s[i] - 32 : s[i]);
	}
	return FS_OK;
}

static void
name_from_83(const uint8_t *e, char out[13])
{
	int i, k = 0;

	for (i = 0; i < 8 && e[i] != ' '; i++)
		out[k++] = (i == 0 && e[0] == 0x05) ? (char)0xE5 : (char)e[i];
	if (e[8] != ' ') {
		out[k++] = '.';
		for (i = 8; i < 11 && e[i] != ' '; i++)
			out[k++] = (char)e[i];
	}
	out[k] = '\0';
}

/* --- Entradas de directorio --------------------------------------------------- */

static uint32_t
entry_clus(const fat_fs *fs, const uint8_t *e)
{
	uint32_t c = rd16(e + 26);

	if (fs->type == 32)
		c |= (uint32_t)rd16(e + 20) << 16;
	return c;
}

static void
entry_set_clus(const fat_fs *fs, uint8_t *e, uint32_t c)
{
	wr16(e + 26, c & 0xFFFF);
	wr16(e + 20, fs->type == 32 ? c >> 16 : 0);
}

static void
entry_init(const fat_fs *fs, uint8_t e[DIRENT_SIZE], const uint8_t nm[11],
    uint8_t attr, uint32_t clus)
{
	uint32_t t = now(fs);

	memset(e, 0, DIRENT_SIZE);
	memcpy(e, nm, 11);
	e[11] = attr;
	wr16(e + 14, t);	/* creación */
	wr16(e + 16, t >> 16);
	wr16(e + 18, t >> 16);	/* último acceso (sólo fecha) */
	wr16(e + 22, t);	/* última escritura */
	wr16(e + 24, t >> 16);
	entry_set_clus(fs, e, clus);
}

static int
entry_write(fat_fs *fs, uint32_t lba, uint16_t off, const uint8_t *e)
{
	uint8_t *p = cache_get(fs, lba);

	if (!p)
		return FS_EIO;
	memcpy(p + off, e, DIRENT_SIZE);
	fs->cache_dirty = 1;
	return FS_OK;
}

static int
entry_delete(fat_fs *fs, const fat_node *n)
{
	uint8_t *p = cache_get(fs, n->lba);

	if (!p)
		return FS_EIO;
	p[n->off] = DE_FREE;
	fs->cache_dirty = 1;
	return FS_OK;
}

static void
node_from_entry(const fat_fs *fs, fat_node *n, const uint8_t *e, uint32_t lba, uint16_t off)
{
	n->clus = entry_clus(fs, e);
	n->size = rd32(e + 28);
	n->mtime = ((uint32_t)rd16(e + 24) << 16) | rd16(e + 22);
	n->attr = e[11];
	n->lba = lba;
	n->off = off;
	n->is_root = 0;
	n->has_loc = 1;
}

static void
node_root(fat_node *n)
{
	memset(n, 0, sizeof(*n));
	n->attr = FAT_ATTR_DIR;
	n->is_root = 1;
}

/* Entrada viva y con nombre: ni borrada, ni LFN, ni etiqueta de volumen. */
static int
entry_live(const uint8_t *e)
{
	return e[0] != DE_FREE && e[11] != FAT_ATTR_LFN && !(e[11] & FAT_ATTR_VOLID);
}

static int
is_dot_name(const uint8_t *e)
{
	return e[0] == '.';	/* en 8.3 sólo "." y ".." empiezan con punto */
}

/* --- Iteración de directorios -------------------------------------------------- */

/* start == 0 es la raíz, en los tres tipos (así lo guarda ".." en disco). */
static int
dir_begin(fat_fs *fs, fat_dir *d, uint32_t start)
{
	d->fs = fs;
	d->off = 0;
	d->done = 0;
	if (start == 0 && fs->type != 32) {
		d->fixed = 1;
		d->clus = 0;
		d->lba = fs->root_lba;
		d->sec_left = fs->root_secs;
		return FS_OK;
	}
	if (start == 0)
		start = fs->root_clus;
	if (!clus_valid(fs, start))
		return FS_EIO;
	d->fixed = 0;
	d->clus = start;
	d->lba = clus_lba(fs, start);
	d->sec_left = fs->sec_per_clus;
	return FS_OK;
}

static uint8_t *
dir_entry(fat_dir *d)
{
	uint8_t *p = cache_get(d->fs, d->lba);

	return p ? p + d->off : NULL;
}

/* 1 = avanzó, 0 = no hay más entradas (d->clus queda en el último cluster). */
static int
dir_advance(fat_dir *d)
{
	fat_fs *fs = d->fs;
	uint32_t next;
	int r;

	d->off += DIRENT_SIZE;
	if (d->off < fs->bytes_per_sec)
		return 1;
	d->off = 0;
	if (--d->sec_left) {
		d->lba++;
		return 1;
	}
	if (d->fixed) {
		d->done = 1;
		return 0;
	}
	if ((r = fat_get(fs, d->clus, &next)))
		return r;
	if (next >= CLUS_BAD) {
		d->done = 1;
		return 0;
	}
	if (!clus_valid(fs, next))
		return FS_EIO;
	d->clus = next;
	d->lba = clus_lba(fs, next);
	d->sec_left = fs->sec_per_clus;
	return 1;
}

static int
dir_find(fat_fs *fs, uint32_t dclus, const uint8_t nm[11], fat_node *n)
{
	fat_dir d;
	uint8_t *e;
	int r;

	if ((r = dir_begin(fs, &d, dclus)))
		return r;
	for (;;) {
		if (!(e = dir_entry(&d)))
			return FS_EIO;
		if (e[0] == DE_END)
			return FS_ENOENT;
		if (entry_live(e) && memcmp(e, nm, 11) == 0) {
			node_from_entry(fs, n, e, d.lba, d.off);
			return FS_OK;
		}
		if ((r = dir_advance(&d)) <= 0)
			return r < 0 ? r : FS_ENOENT;
	}
}

/* Busca un hueco; si el directorio está lleno lo agranda un cluster. */
static int
dir_alloc_slot(fat_fs *fs, uint32_t dclus, uint32_t *lba, uint16_t *off)
{
	fat_dir d;
	uint8_t *e;
	uint32_t nc;
	int r;

	if ((r = dir_begin(fs, &d, dclus)))
		return r;
	for (;;) {
		if (!(e = dir_entry(&d)))
			return FS_EIO;
		if (e[0] == DE_END || e[0] == DE_FREE) {
			*lba = d.lba;
			*off = d.off;
			return FS_OK;
		}
		if ((r = dir_advance(&d)) < 0)
			return r;
		if (r == 0)
			break;
	}
	if (d.fixed)
		return FS_ENOSPC;	/* la raíz de FAT12/16 tiene tamaño fijo */
	if ((r = clus_alloc(fs, d.clus, &nc)) || (r = clus_zero(fs, nc)))
		return r;
	*lba = clus_lba(fs, nc);
	*off = 0;
	return FS_OK;
}

static int
dir_create(fat_fs *fs, const fat_node *dir, const uint8_t nm[11], uint8_t attr,
    uint32_t clus, fat_node *out)
{
	uint8_t e[DIRENT_SIZE];
	uint32_t lba;
	uint16_t off;
	int r;

	if ((r = dir_alloc_slot(fs, dir->clus, &lba, &off)))
		return r;
	entry_init(fs, e, nm, attr, clus);
	if ((r = entry_write(fs, lba, off, e)))
		return r;
	node_from_entry(fs, out, e, lba, off);
	return FS_OK;
}

/* --- Rutas ----------------------------------------------------------------------- */

static size_t
next_comp(const char **p)
{
	const char *s;

	while (**p == '/')
		(*p)++;
	s = *p;
	while (**p && **p != '/')
		(*p)++;
	return (size_t)(*p - s);
}

static int
is_dot(const char *s, size_t len)
{
	return (len == 1 && s[0] == '.') || (len == 2 && s[0] == '.' && s[1] == '.');
}

static int
node_child(fat_fs *fs, const fat_node *dir, const char *s, size_t len, fat_node *out)
{
	uint8_t nm[11];
	int r;

	if (!(dir->attr & FAT_ATTR_DIR))
		return FS_ENOTDIR;
	if (len == 1 && s[0] == '.') {
		*out = *dir;
		return FS_OK;
	}
	if (len == 2 && s[0] == '.' && s[1] == '.') {
		if (dir->is_root) {
			*out = *dir;
			return FS_OK;
		}
		memset(nm, ' ', 11);
		nm[0] = nm[1] = '.';
		if ((r = dir_find(fs, dir->clus, nm, out)))
			return r == FS_ENOENT ? FS_EIO : r;
		if (out->clus == 0 || (fs->type == 32 && out->clus == fs->root_clus))
			node_root(out);
		else
			out->has_loc = 0;	/* es la entrada ".." del hijo, no la propia */
		return FS_OK;
	}
	if ((r = make_83(s, len, nm)))
		return r;
	return dir_find(fs, dir->clus, nm, out);
}

static int
lookup(fat_fs *fs, const char *path, fat_node *n)
{
	fat_node c;
	size_t len;
	int r;

	node_root(n);
	while ((len = next_comp(&path)) != 0) {
		if ((r = node_child(fs, n, path - len, len, &c)))
			return r;
		*n = c;
	}
	return FS_OK;
}

/* Resuelve todo menos el último componente; *leaflen == 0 si path es la raíz. */
static int
lookup_parent(fat_fs *fs, const char *path, fat_node *dir, const char **leaf, size_t *leaflen)
{
	const char *q;
	fat_node c;
	size_t len, nlen;
	int r;

	node_root(dir);
	len = next_comp(&path);
	*leaf = path - len;
	*leaflen = len;
	while (len) {
		q = path;
		if ((nlen = next_comp(&q)) == 0)
			break;
		if ((r = node_child(fs, dir, *leaf, len, &c)))
			return r;
		*dir = c;
		path = q;
		*leaf = q - nlen;
		*leaflen = len = nlen;
	}
	return (dir->attr & FAT_ATTR_DIR) ? FS_OK : FS_ENOTDIR;
}

/* --- Formato --------------------------------------------------------------------- */

typedef struct geom {
	uint32_t	spc, rsvd, root_ent, root_secs, fat_size, clusters;
} geom;

/*
 * El tipo de FAT no se elige: se deduce de la cantidad de clusters
 * (< 4085 es FAT12, < 65525 es FAT16, el resto FAT32). Formatear "como FAT16"
 * significa entonces encontrar una geometría que caiga en ese rango.
 */
static int
geom_try(uint8_t type, uint32_t tot, uint32_t bps, uint32_t spc, geom *g)
{
	uint64_t clus, bytes;
	uint32_t meta;

	g->spc = spc;
	g->rsvd = type == 32 ? 32 : 1;
	if (type == 32)
		g->root_ent = 0;
	else if (type == 12 && (uint64_t)tot * bps <= 1474560)
		g->root_ent = 224;	/* lo que usa un disquete de 1,44 MB */
	else
		g->root_ent = 512;
	g->root_secs = (g->root_ent * DIRENT_SIZE + bps - 1) / bps;
	meta = g->rsvd + g->root_secs;
	if (tot <= meta + spc)
		return 0;

	/* Calcular la FAT como si ocupara 0 sectores sobreestima los clusters,
	 * y por lo tanto la FAT: el resultado siempre alcanza. */
	clus = (tot - meta) / spc;
	bytes = type == 12 ? ((clus + 2) * 3 + 1) / 2 : (clus + 2) * (type / 8);
	g->fat_size = (uint32_t)((bytes + bps - 1) / bps);
	if (meta + 2ull * g->fat_size + spc > tot)
		return 0;
	g->clusters = (tot - meta - 2 * g->fat_size) / spc;

	switch (type) {
	case 12: return g->clusters >= 1 && g->clusters < 4085;
	case 16: return g->clusters >= 4085 && g->clusters < 65525;
	default: return g->clusters >= 65525 && g->clusters < 0x0FFFFFF5;
	}
}

static int
geom_pick(uint8_t type, uint32_t tot, uint32_t bps, uint8_t want, geom *g)
{
	uint32_t spc, pref;

	if (want)
		return geom_try(type, tot, bps, want, g) ? FS_OK : FS_EINVAL;
	if (type == 32) {
		/* Clusters de 4 KiB como Windows; más chicos si no alcanzan. */
		pref = bps >= 4096 ? 1 : 4096 / bps;
		for (spc = pref; spc >= 1; spc >>= 1)
			if (geom_try(type, tot, bps, spc, g))
				return FS_OK;
		for (spc = pref * 2; spc <= 128 && spc * bps <= 32768; spc <<= 1)
			if (geom_try(type, tot, bps, spc, g))
				return FS_OK;
		return FS_EINVAL;
	}
	for (spc = 1; spc <= 128 && spc * bps <= 32768; spc <<= 1)
		if (geom_try(type, tot, bps, spc, g))
			return FS_OK;
	return FS_EINVAL;
}

static int
sector_size_ok(uint32_t bps)
{
	return (bps == 512 || bps == 1024 || bps == 2048 || bps == 4096) &&
	    bps <= FAT_MAX_SECTOR_SIZE;
}

static void
write_label(uint8_t *dst, const char *label)
{
	size_t i;

	memcpy(dst, "NO NAME    ", 11);
	if (!label)
		return;
	memset(dst, ' ', 11);
	for (i = 0; i < 11 && label[i]; i++)
		dst[i] = (uint8_t)((label[i] >= 'a' && label[i] <= 'z') ? label[i] - 32 : label[i]);
}

int
fat_mkfs(fat_fs *fs, fat_blockdev *dev, const fat_mkfs_opts *o)
{
	static const uint8_t order_small[] = { 12, 16, 32 };
	static const uint8_t order_mid[] = { 16, 32, 12 };
	static const uint8_t order_big[] = { 32, 16, 12 };
	const uint8_t *order;
	uint32_t bps = dev->sector_size, tot, lba, zero_end, i;
	uint64_t bytes;
	uint8_t type = 0, *p, *b;
	geom g;
	int r = FS_EINVAL;

	if (!sector_size_ok(bps) || o->part_lba >= dev->sector_count)
		return FS_EINVAL;
	tot = o->sectors ? o->sectors : dev->sector_count - o->part_lba;
	if ((uint64_t)o->part_lba + tot > dev->sector_count)
		return FS_EINVAL;

	if (o->type) {
		if (o->type != 12 && o->type != 16 && o->type != 32)
			return FS_EINVAL;
		type = o->type;
		r = geom_pick(type, tot, bps, o->sec_per_clus, &g);
	} else {
		bytes = (uint64_t)tot * bps;
		order = bytes < (16u << 20) ? order_small :
		    bytes < (512u << 20) ? order_mid : order_big;
		for (i = 0; i < 3 && r; i++)
			r = geom_pick(type = order[i], tot, bps, o->sec_per_clus, &g);
	}
	if (r)
		return r;

	memset(fs, 0, sizeof(*fs));
	fs->dev = dev;
	fs->part_lba = o->part_lba;
	fs->bytes_per_sec = (uint16_t)bps;
	fs->cache_lba = CACHE_NONE;	/* fat_size == 0: la caché no duplica FATs */

	/* Reservados, las dos FATs, la raíz fija y, en FAT32, el cluster raíz. */
	zero_end = g.rsvd + 2 * g.fat_size + g.root_secs + (type == 32 ? g.spc : 0);
	for (lba = 0; lba < zero_end; lba++) {
		if (!(p = cache_claim(fs, lba)))
			return FS_EIO;
		memset(p, 0, bps);
	}

	/* Sector de arranque con el BPB. */
	if (!(b = cache_claim(fs, 0)))
		return FS_EIO;
	memset(b, 0, bps);
	b[0] = 0xEB;
	b[1] = type == 32 ? 0x58 : 0x3C;
	b[2] = 0x90;
	memcpy(b + 3, "MSWIN4.1", 8);	/* el OEM que recomienda fatgen103 */
	wr16(b + 11, bps);
	b[13] = (uint8_t)g.spc;
	wr16(b + 14, g.rsvd);
	b[16] = 2;
	wr16(b + 17, g.root_ent);
	if (type != 32 && tot < 0x10000)
		wr16(b + 19, tot);
	else
		wr32(b + 32, tot);
	b[21] = 0xF8;			/* medio fijo */
	wr16(b + 24, 63);		/* sectores por pista y cabezas: sólo BIOS */
	wr16(b + 26, 255);
	wr32(b + 28, o->part_lba);	/* sectores ocultos */
	if (type == 32) {
		wr32(b + 36, g.fat_size);
		wr32(b + 44, 2);	/* cluster raíz */
		wr16(b + 48, 1);	/* FSInfo */
		wr16(b + 50, 6);	/* copia del arranque */
		b += 28;		/* el resto del BPB extendido va corrido 28 bytes */
	} else {
		wr16(b + 22, g.fat_size);
	}
	b[36] = 0x80;
	b[38] = 0x29;
	wr32(b + 39, o->volume_id ? o->volume_id : 0x4B415441u);
	write_label(b + 43, o->label);
	memcpy(b + 54, type == 12 ? "FAT12   " : type == 16 ? "FAT16   " : "FAT32   ", 8);
	fs->cache[510] = 0x55;
	fs->cache[511] = 0xAA;

	if (type == 32) {
		/* Mismo buffer, otro LBA: cache_claim no lo toca. */
		if (!cache_claim(fs, 6))
			return FS_EIO;
		/* FSInfo con contadores "desconocido": siempre válido. */
		if (!(p = cache_claim(fs, 1)))
			return FS_EIO;
		memset(p, 0, bps);
		wr32(p, 0x41615252);
		wr32(p + 484, 0x61417272);
		wr32(p + 488, 0xFFFFFFFF);
		wr32(p + 492, 0xFFFFFFFF);
		wr32(p + 508, 0xAA550000);
		if (!cache_claim(fs, 7))
			return FS_EIO;
	}

	/* FAT[0] lleva el byte de medio, FAT[1] fin de cadena. */
	for (i = 0; i < 2; i++) {
		if (!(p = cache_claim(fs, g.rsvd + i * g.fat_size)))
			return FS_EIO;
		memset(p, 0, bps);
		if (type == 12) {
			p[0] = 0xF8; p[1] = 0xFF; p[2] = 0xFF;
		} else if (type == 16) {
			wr16(p, 0xFFF8);
			wr16(p + 2, 0xFFFF);
		} else {
			wr32(p, 0x0FFFFFF8);
			wr32(p + 4, 0x0FFFFFFF);
			wr32(p + 8, 0x0FFFFFFF);	/* cluster 2: la raíz */
		}
	}
	return cache_flush(fs);
}

/* --- Montaje ----------------------------------------------------------------------- */

int
fat_mount(fat_fs *fs, fat_blockdev *dev, uint32_t part_lba)
{
	uint32_t bps, spc, rsvd, nfats, root_ent, tot, fatsz, need;
	uint8_t *p;

	memset(fs, 0, sizeof(*fs));
	fs->dev = dev;
	fs->part_lba = part_lba;
	fs->cache_lba = CACHE_NONE;
	if (!sector_size_ok(dev->sector_size))
		return FS_EINVAL;
	fs->bytes_per_sec = (uint16_t)dev->sector_size;

	if (!(p = cache_get(fs, 0)))
		return FS_EIO;
	if (rd16(p + 510) != 0xAA55 || (p[0] != 0xEB && p[0] != 0xE9))
		return FS_ENOFS;

	bps = rd16(p + 11);
	spc = p[13];
	rsvd = rd16(p + 14);
	nfats = p[16];
	root_ent = rd16(p + 17);
	tot = rd16(p + 19) ? rd16(p + 19) : rd32(p + 32);
	fatsz = rd16(p + 22) ? rd16(p + 22) : rd32(p + 36);
	if (bps != dev->sector_size || spc == 0 || (spc & (spc - 1)) || rsvd == 0 ||
	    nfats == 0 || fatsz == 0)
		return FS_ENOFS;

	fs->sec_per_clus = spc;
	fs->num_fats = (uint8_t)nfats;
	fs->fat_lba = rsvd;
	fs->fat_size = fatsz;
	fs->root_secs = (root_ent * DIRENT_SIZE + bps - 1) / bps;
	fs->root_lba = rsvd + nfats * fatsz;
	fs->data_lba = fs->root_lba + fs->root_secs;
	if (tot <= fs->data_lba || (uint64_t)part_lba + tot > dev->sector_count)
		return FS_ENOFS;
	fs->cluster_count = (tot - fs->data_lba) / spc;

	if (fs->cluster_count < 4085)
		fs->type = 12;
	else if (fs->cluster_count < 65525)
		fs->type = 16;
	else
		fs->type = 32;

	if (fs->type == 32) {
		fs->root_clus = rd32(p + 44);
		if (root_ent != 0 || rd16(p + 22) != 0 || !clus_valid(fs, fs->root_clus))
			return FS_ENOFS;
	} else if (root_ent == 0) {
		return FS_ENOFS;
	}

	need = fs->type == 12 ? ((fs->cluster_count + 2) * 3 + 1) / 2 :
	    (fs->cluster_count + 2) * (fs->type / 8);
	if ((uint64_t)fatsz * bps < need)
		return FS_ENOFS;

	fs->next_free = 2;
	return FS_OK;
}

int
fat_mount_first(fat_fs *fs, fat_blockdev *dev)
{
	uint32_t lba[4];
	uint8_t kind[4], *p;
	int i, r;

	if ((r = fat_mount(fs, dev, 0)) != FS_ENOFS)
		return r;

	/* No hay BPB en el sector 0: probar la tabla de particiones MBR. */
	if (!(p = cache_get(fs, 0)))
		return FS_EIO;
	if (rd16(p + 510) != 0xAA55)
		return FS_ENOFS;
	for (i = 0; i < 4; i++) {
		kind[i] = p[446 + 16 * i + 4];
		lba[i] = rd32(p + 446 + 16 * i + 8);
	}
	for (i = 0; i < 4; i++) {
		switch (kind[i]) {
		case 0x01: case 0x04: case 0x06: case 0x0B: case 0x0C: case 0x0E:
			if (lba[i] && fat_mount(fs, dev, lba[i]) == FS_OK)
				return FS_OK;
			break;
		}
	}
	return FS_ENOFS;
}

int
fat_sync(fat_fs *fs)
{
	return cache_flush(fs);
}

int
fat_unmount(fat_fs *fs)
{
	int r = cache_flush(fs);

	fs->dev = NULL;
	return r;
}

int
fat_volume_info(fat_fs *fs, fat_volinfo *vi)
{
	uint32_t c, v;
	int r;

	vi->type = fs->type;
	vi->cluster_size = fs->sec_per_clus * fs->bytes_per_sec;
	vi->total_clusters = fs->cluster_count;
	vi->free_clusters = 0;
	for (c = 2; c < fs->cluster_count + 2; c++) {
		if ((r = fat_get(fs, c, &v)))
			return r;
		if (v == CLUS_FREE)
			vi->free_clusters++;
	}
	return FS_OK;
}

/* --- Archivos ------------------------------------------------------------------- */

int
fat_open(fat_fs *fs, fat_file *f, const char *path, int flags)
{
	const char *leaf;
	fat_node dir, n;
	uint8_t nm[11];
	size_t len;
	int acc = flags & FS_O_ACCMODE, wr, r;

	if (acc == FS_O_ACCMODE)
		return FS_EINVAL;
	wr = acc != FS_O_RDONLY;
	if ((flags & FS_O_TRUNC) && !wr)
		return FS_EINVAL;

	if ((r = lookup_parent(fs, path, &dir, &leaf, &len)))
		return r;
	if (len == 0 || is_dot(leaf, len))
		return FS_EISDIR;
	if ((r = make_83(leaf, len, nm)))
		return r;

	r = dir_find(fs, dir.clus, nm, &n);
	if (r == FS_OK) {
		if ((flags & FS_O_CREAT) && (flags & FS_O_EXCL))
			return FS_EEXIST;
		if (n.attr & FAT_ATTR_DIR)
			return FS_EISDIR;
		if (wr && (n.attr & FAT_ATTR_RDONLY))
			return FS_EACCES;
	} else if (r == FS_ENOENT && (flags & FS_O_CREAT)) {
		if ((r = dir_create(fs, &dir, nm, FAT_ATTR_ARCHIVE, 0, &n)))
			return r;
	} else {
		return r;
	}

	memset(f, 0, sizeof(*f));
	f->fs = fs;
	f->first_clus = n.clus;
	f->size = n.size;
	f->de_lba = n.lba;
	f->de_off = n.off;
	f->flags = (uint8_t)flags;
	if ((flags & FS_O_TRUNC) && (f->size || f->first_clus)) {
		if (f->first_clus && (r = chain_free(fs, f->first_clus)))
			return r;
		f->first_clus = 0;
		f->size = 0;
		f->dirty = 1;
	}
	return FS_OK;
}

/* Deja f->cur_clus en el cluster número `idx` de la cadena; con grow la alarga. */
static int
file_seek_clus(fat_file *f, uint32_t idx, int grow)
{
	fat_fs *fs = f->fs;
	uint32_t next;
	int r;

	if (f->first_clus == 0) {
		if (!grow)
			return FS_EIO;
		if ((r = clus_alloc(fs, 0, &f->first_clus)))
			return r;
		f->dirty = 1;
		f->cur_clus = 0;
	}
	if (f->cur_clus == 0 || idx < f->cur_idx) {
		f->cur_clus = f->first_clus;
		f->cur_idx = 0;
	}
	while (f->cur_idx < idx) {
		if ((r = fat_get(fs, f->cur_clus, &next)))
			return r;
		if (next == CLUS_EOC && grow) {
			if ((r = clus_alloc(fs, f->cur_clus, &next)))
				return r;
		} else if (!clus_valid(fs, next)) {
			return FS_EIO;
		}
		f->cur_clus = next;
		f->cur_idx++;
	}
	return FS_OK;
}

/* Lee (wbuf == NULL, !writing), escribe wbuf, o escribe ceros (wbuf == NULL). */
static int32_t
file_io(fat_file *f, void *rbuf, const void *wbuf, uint32_t n, int writing)
{
	fat_fs *fs = f->fs;
	uint32_t bps = fs->bytes_per_sec, cbytes = bps * fs->sec_per_clus;
	uint32_t done = 0, in, sec, so, chunk, lba;
	uint8_t *p;
	int r;

	while (done < n) {
		if ((r = file_seek_clus(f, f->pos / cbytes, writing)))
			return done ? (int32_t)done : r;
		in = f->pos % cbytes;
		sec = in / bps;
		so = in % bps;
		chunk = bps - so;
		if (chunk > n - done)
			chunk = n - done;
		lba = clus_lba(fs, f->cur_clus) + sec;

		/* Un sector entero que se pisa no hace falta leerlo antes. */
		p = (writing && chunk == bps) ? cache_claim(fs, lba) : cache_get(fs, lba);
		if (!p)
			return done ? (int32_t)done : FS_EIO;
		if (writing) {
			if (wbuf)
				memcpy(p + so, (const uint8_t *)wbuf + done, chunk);
			else
				memset(p + so, 0, chunk);
			fs->cache_dirty = 1;
			f->dirty = 1;
		} else {
			memcpy((uint8_t *)rbuf + done, p + so, chunk);
		}
		done += chunk;
		f->pos += chunk;
		if (f->pos > f->size)
			f->size = f->pos;
	}
	return (int32_t)done;
}

int32_t
fat_read(fat_file *f, void *buf, uint32_t n)
{
	if (!f->fs)
		return FS_EBADF;
	if ((f->flags & FS_O_ACCMODE) == FS_O_WRONLY)
		return FS_EBADF;
	if (f->pos >= f->size)
		return 0;
	if (n > f->size - f->pos)
		n = f->size - f->pos;
	if (n > INT32_MAX)
		n = INT32_MAX;
	return file_io(f, buf, NULL, n, 0);
}

int32_t
fat_write(fat_file *f, const void *buf, uint32_t n)
{
	uint32_t gap, step;
	int32_t r;

	if (!f->fs)
		return FS_EBADF;
	if ((f->flags & FS_O_ACCMODE) == FS_O_RDONLY)
		return FS_EBADF;
	if (f->flags & FS_O_APPEND)
		f->pos = f->size;
	if (n > INT32_MAX)
		n = INT32_MAX;
	if ((uint64_t)f->pos + n > 0xFFFFFFFFu) {
		n = 0xFFFFFFFFu - f->pos;
		if (n == 0)
			return FS_EFBIG;
	}

	/* Escribir más allá del final: el hueco tiene que leerse como ceros,
	 * pero los clusters nuevos traen basura, así que se rellena a mano. */
	if (f->pos > f->size) {
		gap = f->pos;
		f->pos = f->size;
		while (f->pos < gap) {
			step = gap - f->pos > 0x40000000u ? 0x40000000u : gap - f->pos;
			r = file_io(f, NULL, NULL, step, 1);
			if (r < 0)
				return r;
			if ((uint32_t)r < step)
				return FS_ENOSPC;
		}
	}
	return file_io(f, NULL, buf, n, 1);
}

int64_t
fat_seek(fat_file *f, int64_t off, int whence)
{
	int64_t base;

	if (!f->fs)
		return FS_EBADF;
	switch (whence) {
	case FS_SEEK_SET: base = 0; break;
	case FS_SEEK_CUR: base = f->pos; break;
	case FS_SEEK_END: base = f->size; break;
	default: return FS_EINVAL;
	}
	if (base + off < 0 || base + off > 0xFFFFFFFFll)
		return FS_EINVAL;
	f->pos = (uint32_t)(base + off);
	return f->pos;
}

int
fat_fsync(fat_file *f)
{
	fat_fs *fs = f->fs;
	uint32_t t;
	uint8_t *p, *e;

	if (!fs)
		return FS_EBADF;
	if (f->dirty) {
		if (!(p = cache_get(fs, f->de_lba)))
			return FS_EIO;
		e = p + f->de_off;
		t = now(fs);
		entry_set_clus(fs, e, f->first_clus);
		wr32(e + 28, f->size);
		wr16(e + 18, t >> 16);
		wr16(e + 22, t);
		wr16(e + 24, t >> 16);
		e[11] |= FAT_ATTR_ARCHIVE;
		fs->cache_dirty = 1;
		f->dirty = 0;
	}
	return cache_flush(fs);
}

int
fat_close(fat_file *f)
{
	int r = fat_fsync(f);

	f->fs = NULL;
	return r;
}

/* --- Nombres y directorios --------------------------------------------------------- */

int
fat_stat(fat_fs *fs, const char *path, fat_info *st)
{
	fat_node n;
	int r;

	if ((r = lookup(fs, path, &n)))
		return r;
	st->size = n.size;
	st->attr = n.attr;
	st->first_clus = n.is_root ? (fs->type == 32 ? fs->root_clus : 0) : n.clus;
	st->mtime = n.mtime;
	return FS_OK;
}

int
fat_mkdir(fat_fs *fs, const char *path)
{
	static const uint8_t dot[11] = { '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' };
	static const uint8_t dotdot[11] = { '.', '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' };
	const char *leaf;
	fat_node dir, n;
	uint8_t nm[11], e[DIRENT_SIZE];
	uint32_t c;
	size_t len;
	int r;

	if ((r = lookup_parent(fs, path, &dir, &leaf, &len)))
		return r;
	if (len == 0 || is_dot(leaf, len))
		return FS_EEXIST;
	if ((r = make_83(leaf, len, nm)))
		return r;
	if ((r = dir_find(fs, dir.clus, nm, &n)) != FS_ENOENT)
		return r == FS_OK ? FS_EEXIST : r;

	if ((r = clus_alloc(fs, 0, &c)) || (r = clus_zero(fs, c)))
		return r;
	entry_init(fs, e, dot, FAT_ATTR_DIR, c);
	if ((r = entry_write(fs, clus_lba(fs, c), 0, e)))
		return r;
	/* ".." apunta al padre, y a 0 cuando el padre es la raíz (incluso en FAT32). */
	entry_init(fs, e, dotdot, FAT_ATTR_DIR, dir.is_root ? 0 : dir.clus);
	if ((r = entry_write(fs, clus_lba(fs, c), DIRENT_SIZE, e)))
		return r;
	if ((r = dir_create(fs, &dir, nm, FAT_ATTR_DIR, c, &n))) {
		chain_free(fs, c);
		return r;
	}
	return cache_flush(fs);
}

int
fat_unlink(fat_fs *fs, const char *path)
{
	fat_node n;
	int r;

	if ((r = lookup(fs, path, &n)))
		return r;
	if (n.attr & FAT_ATTR_DIR)
		return FS_EISDIR;
	if (!n.has_loc)
		return FS_EINVAL;
	if (n.attr & FAT_ATTR_RDONLY)
		return FS_EACCES;
	if (n.clus && (r = chain_free(fs, n.clus)))
		return r;
	if ((r = entry_delete(fs, &n)))
		return r;
	return cache_flush(fs);
}

int
fat_rmdir(fat_fs *fs, const char *path)
{
	fat_node n;
	fat_dir d;
	uint8_t *e;
	int r;

	if ((r = lookup(fs, path, &n)))
		return r;
	if (!(n.attr & FAT_ATTR_DIR))
		return FS_ENOTDIR;
	if (n.is_root || !n.has_loc)
		return FS_EINVAL;

	if ((r = dir_begin(fs, &d, n.clus)))
		return r;
	for (;;) {
		if (!(e = dir_entry(&d)))
			return FS_EIO;
		if (e[0] == DE_END)
			break;
		if (entry_live(e) && !is_dot_name(e))
			return FS_ENOTEMPTY;
		if ((r = dir_advance(&d)) < 0)
			return r;
		if (r == 0)
			break;
	}
	if ((r = chain_free(fs, n.clus)) || (r = entry_delete(fs, &n)))
		return r;
	return cache_flush(fs);
}

int
fat_opendir(fat_fs *fs, fat_dir *d, const char *path)
{
	fat_node n;
	int r;

	if ((r = lookup(fs, path, &n)))
		return r;
	if (!(n.attr & FAT_ATTR_DIR))
		return FS_ENOTDIR;
	return dir_begin(fs, d, n.clus);
}

int
fat_readdir(fat_dir *d, fat_dirent *ent)
{
	uint8_t *e;
	int keep, r;

	while (!d->done) {
		if (!(e = dir_entry(d)))
			return FS_EIO;
		if (e[0] == DE_END) {
			d->done = 1;
			break;
		}
		/* "." y ".." se omiten: quien lista casi nunca los quiere. */
		keep = entry_live(e) && !is_dot_name(e);
		if (keep) {
			name_from_83(e, ent->name);
			ent->attr = e[11];
			ent->size = rd32(e + 28);
			ent->first_clus = entry_clus(d->fs, e);
			ent->mtime = ((uint32_t)rd16(e + 24) << 16) | rd16(e + 22);
		}
		if ((r = dir_advance(d)) < 0)
			return r;
		if (keep)
			return 1;
	}
	return 0;
}
