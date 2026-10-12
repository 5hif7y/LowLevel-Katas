# Módulo 03: El Sistema de I/O: La Trampa de los Modos Binario vs Texto en Windows

## 1. El Concepto de Archivo en UNIX vs Windows CRT

En el modelo original de UNIX (y en OpenBSD):
- **Un archivo es una secuencia indiferenciada de bytes.**
- No existe el concepto de "modo texto" o "modo binario" en el kernel. Un byte con valor `0x0A` (`\n`) representa el salto de línea. El byte `0x1A` (antiguo EOF de CP/M y DOS) es simplemente el valor numérico 26.
- Los descriptores de archivo estándar (`0`, `1`, `2`) leen y transmiten bytes exactos sin intervención ni alteración por parte del sistema operativo o la biblioteca de C.

En Windows y el runtime de MSVC (UCRT):
- Por defecto, los streams (`stdin`, `stdout`, `stderr`) y los archivos abiertos mediante `fopen("archivo", "r")` u `open("archivo", O_RDONLY)` operan en **modo texto (`_O_TEXT`)**.

```
[Modo Texto en Windows]
Lectura :  0x0D 0x0A (\r\n)  ===> Transforma a ===>  0x0A (\n)
Lectura :  0x1A (Ctrl-Z)      ===> Simula       ===>  EOF (Fin de archivo prematuro)
Escritura: 0x0A (\n)         ===> Transforma a ===>  0x0D 0x0A (\r\n)

[Modo Binario en Windows / Modo Único en UNIX]
Lectura  : 0x0D 0x0A          ===> Se preserva  ===>  0x0D 0x0A (Bytes exactos)
Escritura: 0x0A              ===> Se preserva  ===>  0x0A (Byte exacto)
```

---

## 2. El Peligro en las Coreutils: ¿Por qué Falla `cat` o `wc` sin Modo Binario?

1. **`cat` en modo texto**:
   Si `cat` copia un archivo ejecutable, imagen o archivo comprimido (`.tar.gz`, `.zip`, `.png`), el primer byte con valor `0x1A` causará que `read()` o `getc()` devuelva `EOF`. El archivo resultante quedará truncado y corrupto. Además, cualquier byte `0x0A` que no estuviera precedido por `0x0D` será transformado en `0x0D 0x0A`, alterando el tamaño y los checksums (`sha256`, `md5`).
2. **`wc -c` en modo texto**:
   Si se cuentan los bytes con `read()`, la traducción silenciosa de `\r\n` a `\n` hará que el conteo de bytes sea inferior al tamaño real del archivo en disco.

---

## 3. La Solución Arquitectónica: `_setmode` y Segmentos CRT

### 3.1. `_setmode` en tiempo de ejecución
Para forzar a un descriptor a operar como en UNIX, se utiliza la llamada del CRT de Microsoft:
```c
#include <io.h>
#include <fcntl.h>

_setmode(_fileno(stdin), _O_BINARY);
_setmode(_fileno(stdout), _O_BINARY);
```

### 3.2. Inicialización Automática antes de `main()` mediante `.CRT$XCU`
En OpenBSD no existe `_setmode`. Modificar cada archivo `main()` de decenas de utilidades para insertar llamadas a `_setmode` rompería la pureza del código upstream.

En MSVC, los punteros a funciones ubicados en la sección especial `.CRT$XCU` del binario PE son invocados automáticamente por el runtime de C durante la inicialización del proceso, **antes** de transferir el control a `main()`:

```c
/* compat/src/compat_init.c */
#include <windows.h>
#include <io.h>
#include <fcntl.h>

void compat_enable_vt100(void) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE && hOut != NULL) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
}

static void __cdecl _compat_auto_init(void) {
    compat_enable_vt100();
}

typedef void (__cdecl *_compat_initfn)(void);
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static _compat_initfn _p_compat_auto_init = _compat_auto_init;
```

Con esta técnica:
1. Las secuencias de escape ANSI / VT100 (para colores y estilos en `ls -G` o formateo de terminal) quedan habilitadas desde el arranque.
2. La consola de Windows 10 y Windows 11 interpreta las secuencias VT100 nativamente sin necesidad de bibliotecas intermedias pesadas.

### 3.3. La trampa: una biblioteca estática se come el inicializador

Este apartado se agregó después de que la técnica fallara **en silencio**. Vale más que todo lo anterior.

El inicializador no exporta ninguna función que el resto del programa llame: su única razón de ser es
colgar un puntero en `.CRT$XCU`. Y una biblioteca estática **sólo aporta los `.obj` que resuelven algún
símbolo pendiente**. Resultado: el linker descarta `compat_init.obj`, el inicializador nunca corre, y no
hay error ni warning que lo delate. Ni el modo binario ni el VT100 se activan.

La solución es linkear la capa de compatibilidad como **OBJECT library** y no como `STATIC`:

```cmake
add_library(bsd_compat OBJECT ${COMPAT_SOURCES})
```

Así los objetos se enlazan siempre y cualquier inicializador `.CRT$XCU` sobrevive. Ver el
[módulo 10](10-estudio-de-casos-de-depuracion.md) para la medición que lo confirmó.

### 3.4. Alcance real del `_setmode`

Pasar los streams a binario incondicionalmente deja el texto "en escalera" en consolas que no
interpretan `LF` solo. El criterio implementado en [`compat/src/binary_mode.c`](../../compat/src/binary_mode.c)
distingue por destino:

- **pipe o redirección** → binario, byte-exacto (acá está la corrección que importa);
- **consola interactiva** → se deja en modo texto, para que `\n` se siga viendo como salto de línea.

Y no alcanza con los streams estándar: `open(2)`, `fopen(3)` y `freopen(3)` también fuerzan binario,
porque si no `cat` mutila el archivo *al leerlo* y el pipe binario no salva nada. Ver los envoltorios
en `compat/src/binary_mode.c`.

---

## 4. Buffering de Streams: `setvbuf` y la Opción `-u` en `cat`

En OpenBSD `cat.c`:
```c
case 'u':
    setvbuf(stdout, NULL, _IONBF, 0);
    break;
```
La bandera `-u` desactiva el buffering de la salida estándar (`_IONBF`), forzando a que cada byte escrito sea enviado inmediatamente al kernel. Tanto en UNIX como en Windows UCRT, `setvbuf` es parte del estándar C99/C11 (`<stdio.h>`), por lo que su comportamiento es completamente portable.
