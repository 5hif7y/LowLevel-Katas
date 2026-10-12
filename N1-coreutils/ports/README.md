# Ports: las mismas coreutils en Odin y C++17

El código de `openbsd-src/` está en C de 1993 y corre en Windows gracias a la capa `compat/`
(24 cabeceras + 23 fuentes que emulan POSIX/BSD sobre el CRT). Estos ports responden otra pregunta:

> ¿Cuánto de ese andamiaje hacía falta de verdad, y cuánto era consecuencia de escribir C sobre
> una plataforma que no es Unix?

La respuesta corta: **casi todo el andamiaje desaparece**, y lo que queda no es código de
compatibilidad sino dos decisiones explícitas de E/S.

---

## Qué hay

12 utilidades, implementadas dos veces: **24 programas**, todos multiplataforma (Windows y
Unix), todos compilados y verificados.

| Utilidad | Odin | C++17 | Qué ejercita |
| :--- | :---: | :---: | :--- |
| `true` / `false` | ✅ | ✅ | Contrato de código de salida. Nada más. |
| `echo` | ✅ | ✅ | Argumentos, `-n`, separación sin espacio sobrante. |
| `yes` | ✅ | ✅ | **El caso de `SIGPIPE`.** Productor infinito. |
| `basename` | ✅ | ✅ | POSIX sobre texto, no sobre el filesystem. |
| `dirname` | ✅ | ✅ | Idem, con la regla `"//"` y el `"."` final. |
| `cat` | ✅ | ✅ | **E/S byte-exacta.** |
| `tee` | ✅ | ✅ | **Round-trip byte-exacto** + escritura a archivos. |
| `wc` | ✅ | ✅ | Conteo sobre bytes crudos; alineación a 7 columnas. |
| `head` | ✅ | ✅ | `-n N`, encabezado sólo con varios archivos. |
| `cut` | ✅ | ✅ | `-d`/`-f`/`-c`, parser de rangos `1,3-5,8-`. |
| `uniq` | ✅ | ✅ | Sólo adyacentes; `-c`/`-d`/`-u`. |

Pendientes (mismo patrón, sin sorpresas nuevas): `ls`, `tail`, `tr`, `printf`, `sleep`, `mkdir`,
`rmdir`, `touch`, `pwd`, `true`/`false` ya están.

> **Son reimplementaciones idiomáticas, no transliteraciones línea a línea.** El original se lee
> para copiar el *comportamiento* (qué hace `cut` con `-f 3,1`, cuándo `head` imprime encabezado,
> qué cuenta `wc` como palabra), no para copiar la estructura del C. Eso es justamente lo que
> hace visible la diferencia entre "portear" y "traducir".

---

## Compilar

### Odin

```sh
odin build ports/odin/cat -out:bin/cat.exe -o:speed
```

Un directorio por utilidad, `package main` en cada uno. Probado con `dev-2026-10-nightly`.

### C++17

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Iports/cpp17 ports/cpp17/cat/main.cpp -o bin/cat.exe
```

Probado con MinGW-w64 UCRT g++ 13.2.0. `common.hpp` es compartido; el `-Iports/cpp17` es
necesario.

Ver [`build.sh`](build.sh) para construir todo de una.

---

## Los tres bugs del port a C, revisitados

El port a C necesitó `compat/` para tres problemas concretos, todos documentados en
[`../Docs/porter-guide/10-estudio-de-casos-de-depuracion.md`](../Docs/porter-guide/10-estudio-de-casos-de-depuracion.md).
Vale la pena ver cómo se comporta cada lenguaje frente a ellos, porque la comparación es el
verdadero contenido de este directorio.

### 1. `SIGPIPE` no existe en Windows

En **C** hubo que escribir `compat/src/sigpipe.c`: envolver ocho funciones de stdio y, cuando una
escritura falla sobre un pipe, terminar el proceso con `_exit(141)` para imitar la acción por
defecto de la señal. El código upstream de `yes.c` no se puede tocar, así que la emulación va
por debajo.

En **Odin** y en **C++** el problema simplemente **no existe**: ambos devuelven un error de
escritura y ninguno tiene una señal que mate al proceso por detrás. Chequear la escritura no es
una precaución defensiva, es la única forma de que el bucle termine:

```odin
if _, err := os.write(os.stdout, buf); err != os.ERROR_NONE { os.exit(1) }
```
```cpp
if (!cu::out(line)) return 1;
```

Tres líneas contra 150. La lección: **`compat/sigpipe.c` no emula Unix, emula la ausencia de un
`errno` que el C original decidió ignorar.**

### 2. El modo texto del CRT

En **C**, `cat` abría con `open(2)` y el CRT convertía `0x1A` en EOF y `\r\n` en `\n`; `tee`
escribía 9 bytes donde había leído 8. Hubo que escribir `compat/src/binary_mode.c` y además
descubrir que la biblioteca estática se comía el inicializador `.CRT$XCU`.

En **Odin** no hay capa que traducir: `os.read_entire_file` devuelve bytes crudos y `os.write`
los escribe. Cero líneas.

En **C++17** la trampa **vuelve a aparecer**: `std::cout` y `std::ifstream` pasan por el mismo
CRT. Y acá hay dos hallazgos medidos con MinGW-w64 UCRT g++ 13.2.0:

- `_setmode(_O_BINARY)` sobre `stdout` **no afecta a `std::cout`**. Ni llamándolo desde un
  inicializador estático antes de `main()`. El camino de stdio de C sí lo respeta.
- `std::ifstream` sobre un archivo hace **segfault** al compilar con `-O1`/`-O2`/`-O3` (a `-O0`
  anda; cambiar la construcción mueve el síntoma pero no lo elimina).

Por eso `common.hpp` hace la **E/S de bytes con stdio de C** (`fopen`/`fread`/`fwrite`) y deja
C++17 para el resto. No es una concesión estética: es la diferencia entre un port que reproduce
los bytes y uno que no.

### 3. Lo que no hizo falta

`pledge`/`unveil` (stubs vacíos en C), `fts`, `strmode`, `fmt_scaled`, `setmode`, `getopt`,
`getline`, `strptime`, `strsep`, `strlcpy`/`strlcat`, `strtonum`, `err.h`, `sys/queue.h`,
`sys/cdefs.h`… **nada de eso tiene equivalente en estos ports**, porque ninguno se usa en las 12
utilidades elegidas. La capa `compat/` está sobredimensionada respecto de lo que el conjunto
actual de utilidades realmente necesita — dato útil para decidir si vale la pena portear `ls` a
Odin (usaría `fts`) o no.

---

## Diferencias de comportamiento que quedaron anotadas

Portear no es sólo traducir sintaxis. Estas son deliberadas y están marcadas en el código:

| Tema | Original (OpenBSD C) | Port | Por qué |
| :--- | :--- | :--- | :--- |
| `echo` escapes | Interpreta `\n`, `\t`, `\c` | Sólo `-n` | Recorte de alcance; el README lo anota como ejercicio. |
| `cat`/`tee` memoria | Buffer de 64 KB en streaming | Archivo entero en memoria | Alcanza para el tamaño de archivo de un kata. |
| `tee` + `SIGINT` | Ignora `SIGINT` mientras escribe | No aplica | Odin no tiene `SIGINT`; asimetría real, no descuido. |
| Alineación numérica | `%7lld` (espacios) | `pad7()` a mano | El `%7d` de Odin rellena con **ceros**, no espacios. |
| Errores | `err(3)` con `__progname` | Mensaje literal por utilidad | `err.h` no existe en Odin; en C++ sí, pero se prefirió no depender de `compat/`. |

---

## Por qué Odin sale más corto

No es sólo sintaxis. `compat/` existe porque C sobre Windows obliga a decidir, función por
función, si el CRT se comporta como Unix. Odin y C++17 no eliminan esa pregunta — el caso de
C++17 muestra que reaparece — pero sí eliminan la necesidad de **mantener una biblioteca que la
responda para 21 utilidades a la vez**.
