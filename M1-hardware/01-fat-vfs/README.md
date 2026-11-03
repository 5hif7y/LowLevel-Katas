# 01 — FAT12 / FAT16 / FAT32 + VFS

Un sistema de archivos FAT completo (los tres tipos) y una capa VFS de montaje,
pensados para dos usos concretos:

- **Emulación**: leer y escribir imágenes de disquetes, discos duros y tarjetas
  SD (volumen suelto o primera partición de un MBR).
- **Embebidos ARM** (Cortex-M o similares): C99 sin `malloc`, sin `stdio` y sin
  `errno`. Toda la memoria la pone quien llama y el medio se abstrae en dos
  callbacks por sector.

```
include/
  fs_types.h   errores FS_E*, flags FS_O_*, FS_SEEK_*  (compartidos)
  fat.h        API FAT
  vfs.h        puntos de montaje + vtable de backend
  fat_vfs.h    FAT como backend del VFS
src/           fat.c, vfs.c, fat_vfs.c       ← lo único que va al firmware
tools/fatimg.c herramienta de host para imágenes
tests/         test_fat.c (en RAM) y run-tests.sh (incluye validación con 7-Zip)
```

## Uso

### Conectar el medio

```c
static int sd_read(void *ctx, uint32_t lba, void *buf, uint32_t n)
{
	return sdcard_read_blocks(lba, buf, n);	/* tu driver SPI/SDIO */
}

static int sd_write(void *ctx, uint32_t lba, const void *buf, uint32_t n)
{
	return sdcard_write_blocks(lba, buf, n);
}

static fat_blockdev sd_dev = {
	.ctx = NULL, .sector_size = 512, .sector_count = 0 /* del CSD */,
	.read = sd_read, .write = sd_write,
};
```

`read`/`write` devuelven 0 si salió bien. Por ahora la biblioteca pide un sector
por llamada (`count == 1`), pero el contrato admite más.

### Montar y usar

```c
static fat_fs sd;		/* 512 B de caché + ~70 B: mejor estático que en la pila */

fat_mount_first(&sd, &sd_dev);	/* BPB en el sector 0 o 1ª partición FAT del MBR */
sd.clock = rtc_now_fat;		/* opcional; asignarlo DESPUÉS de montar */

fat_file f;
fat_mkdir(&sd, "/LOGS");
fat_open(&sd, &f, "/LOGS/BOOT.TXT", FS_O_WRONLY | FS_O_CREAT | FS_O_APPEND);
fat_write(&f, "arranque\n", 9);
fat_close(&f);			/* actualiza tamaño y fecha en el directorio y hace flush */
```

Sin `fat_close` o `fat_fsync`, el tamaño del archivo no llega al disco. Antes de
cortar la energía o expulsar el medio hay que llamar a `fat_unmount` (o
`fat_sync`).

### A través del VFS

```c
vfs_mount("/sd",  &fat_vfs_ops, &sd);
vfs_mount("/fd0", &fat_vfs_ops, &floppy);

vfs_file f;
vfs_open(&f, "/sd/LOGS/BOOT.TXT", FS_O_RDONLY);
```

Gana el prefijo más largo, siempre en un límite de componente (`/sd` no toma
`/sdcard`). Se puede montar algo en `/` y otros volúmenes encima. Para sumar
otro sistema de archivos (romfs, littlefs, un FS del emulador...) se escribe una
`vfs_ops`; el estado del backend vive dentro de `vfs_file` en `VFS_PRIV_SIZE`
bytes, y `fat_vfs.c` muestra cómo verificar en compilación que entra.

### Formatear

```c
fat_mkfs_opts o = { .type = 12, .label = "DISQUETE" };	/* 0 = elegir por tamaño */
fat_mkfs(&scratch, &dev, &o);
```

El tipo de FAT no es un campo del disco: se **deduce** de la cantidad de
clusters (< 4085 → FAT12, < 65525 → FAT16, el resto → FAT32). Formatear como
un tipo es entonces buscar un tamaño de cluster que caiga en su rango. Por eso
FAT32 necesita al menos ~33 MB con sectores de 512, y pedirlo en un disquete
devuelve `FS_EINVAL`. Un disquete de 1,44 MB sale con la geometría clásica:
cluster de 512, FAT de 9 sectores, 224 entradas en la raíz y 2847 clusters.

## Herramienta de host: `fatimg`

```bash
./tests/run-tests.sh                         # también compila build/fatimg
```

```bash
build/fatimg mkfs disk.img 1440K 12 BOOTDISK
```

```bash
build/fatimg put disk.img kernel.bin /KERNEL.BIN
```

Otros comandos: `info`, `ls`, `get`, `cat`, `mkdir`, `rm`, `rmdir`, y `-s N`
antes del comando para sectores que no sean de 512.

## Huella

Medido en x86-64 (`gcc -Os -ffreestanding`): `fat.c` ocupa ≈ 10 KB de código y
no tiene `.data` ni `.bss`. En RAM:

| Estructura | Bytes (64 bits) | Notas |
| :--- | ---: | :--- |
| `fat_fs` | 584 | 512 son la caché; crece con `FAT_MAX_SECTOR_SIZE` |
| `fat_file` | 40 | uno por archivo abierto |
| `fat_dir` | 24 | |
| `vfs_file` | 72 | `fat_file` + vtable, con `VFS_PRIV_SIZE` = 64 |

En un Cortex-M (punteros de 32 bits) cada una baja unos bytes.

`FAT_MAX_SECTOR_SIZE` (default 512) cambia el tamaño de `fat_fs`, así que tiene
que valer lo mismo en **todas** las unidades de compilación: pasalo con `-D` o
con la variable de caché de CMake, nunca con un `#define` antes del include.

## Cómo está hecho

- **Caché de un sector, write-back.** Es el mínimo indispensable: una entrada
  de FAT12 mide 1,5 bytes y puede quedar partida entre dos sectores, y las
  entradas de directorio se modifican en el lugar. Cuando el sector que se
  baja al disco pertenece a la FAT #1, el flush lo escribe también en las demás
  copias, así que las FATs quedan siempre idénticas sin que el resto del código
  tenga que acordarse.
- **Valores de FAT normalizados.** `fat_get` traduce los fin-de-cadena y los
  clusters dañados de los tres tipos a un mismo valor, y `fat_set` hace lo
  inverso. En FAT32 se preservan los 4 bits altos reservados.
- **Todo campo en disco se lee byte a byte** (`rd16`/`rd32`): no importa el
  endianness ni si el procesador tolera accesos desalineados (un Cortex-M0 no
  los tolera).
- **Huecos en cero.** Escribir más allá del final rellena con ceros, como
  POSIX: los clusters nuevos traen lo que hubiera antes en el medio.
- **Cadenas acotadas.** Recorrer o liberar una cadena corta después de
  `cluster_count` pasos, así que una FAT circular de un disco corrupto da
  `FS_EIO` en vez de colgar el firmware.

## Límites

Conscientes, para que la kata sea chica y verificable:

- **Sólo nombres 8.3.** No lee ni escribe entradas VFAT/LFN: las saltea, así
  que en una imagen hecha por Windows o Linux los archivos aparecen con su nombre
  corto (`ARCHIV~1.TXT`). Al borrar no se limpian las entradas LFN huérfanas
  (`chkdsk`/`fsck` las eliminan sin perder datos). Las minúsculas se pasan a
  mayúsculas.
- **No hay `rename`** ni truncado a un tamaño arbitrario (sólo `FS_O_TRUNC`).
- **FSInfo de FAT32** se escribe con los contadores en "desconocido" y no se
  mantiene. Es válido según la especificación; el sistema que monte la imagen
  recalcula el espacio libre.
- **Dos `fat_file` sobre el mismo archivo** no se ven entre sí: el último que
  cierra pisa tamaño y primer cluster.
- **No es reentrante.** Un `fat_fs` por hilo, o un mutex alrededor.
- **Sin journaling**: un corte de energía entre la escritura de la FAT y la del
  directorio puede dejar clusters perdidos (no datos ajenos corruptos).

## Pruebas

```bash
./tests/run-tests.sh
```

1. Compila `src/` con `-ffreestanding -Werror -pedantic`, como lo haría un
   toolchain `arm-none-eabi` (`CC=arm-none-eabi-gcc ./tests/run-tests.sh`
   cambia el compilador de este paso).
2. `test_fat.c`, en RAM: FAT12/16 con sectores de 512 y 4096, FAT32 con 512,
   entradas de FAT12 partidas entre sectores, volumen dentro de un MBR,
   formateo automático y el VFS con dos volúmenes. Después de cada etapa se
   verifica que las dos FATs sean idénticas y que borrar todo devuelva
   exactamente los clusters libres del principio.
3. **Validación cruzada con 7-Zip**, que trae su propio lector de FAT: `fatimg`
   arma imágenes de los tres tipos (con borrados para dejar huecos) y 7-Zip
   tiene que aceptarlas (`7z t`), extraer el mismo árbol byte a byte y calcular
   el mismo espacio libre.

Con CMake también funciona; ahí `FAT_MAX_SECTOR_SIZE` queda en 512 y los casos
de 4096 se saltean:

```bash
cmake -S . -B build/cm
```

```bash
cmake --build build/cm
```

Pendiente para validar más a fondo: `fsck.fat -n` (dosfstools) y `mtools` en un
Linux, que además prueban la otra dirección (imagen escrita por otro y leída
por nosotros).
