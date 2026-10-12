# Procedencia del código upstream

Este kata **no modifica ni forkearía** el código de OpenBSD ni de BusyBox. El porteo es un
*overlay out-of-tree*: `CMakeLists.txt` compila los `.c` originales directamente desde
`openbsd-src/` sin tocarlos, y toda la contribución propia vive en `compat/` y `Docs/`.

Por eso los árboles upstream **no se versionan en este repositorio**. Se reconstruyen bajo
demanda, pinneados a los commits exactos de abajo, con `fetch-upstream.ps1` / `fetch-upstream.sh`.

---

## Commits base congelados

### OpenBSD `src`

| Campo | Valor |
| :--- | :--- |
| Repositorio | <https://github.com/openbsd/src> (mirror oficial en GitHub) |
| Commit | `9477e7ac084034d747d8a0505752dd7fa48b4392` |
| Fecha | 2026-09-03 17:55:47 +0000 |
| Asunto | `vmm(4): support variable length instructions in VMX paths.` |
| Rama | `master` |
| Profundidad | `--depth 1` (shallow) |
| Tipo | **Sparse checkout** — solo `bin/`, `usr.bin/`, `include/` |

**Rutas efectivamente usadas por el build** (21 utilidades):

```
bin/cat/cat.c                    bin/ls/{cmp,ls,main,print,utf8,util}.c
bin/echo/echo.c                  bin/mkdir/mkdir.c
bin/pwd/pwd.c                    bin/rmdir/rmdir.c
bin/sleep/sleep.c

usr.bin/basename/basename.c      usr.bin/tee/tee.c
usr.bin/cut/cut.c                usr.bin/touch/touch.c
usr.bin/dirname/dirname.c        usr.bin/tr/{str,tr}.c
usr.bin/false/false.c            usr.bin/true/true.c
usr.bin/head/head.c              usr.bin/uniq/uniq.c
usr.bin/printf/printf.c          usr.bin/wc/wc.c
usr.bin/tail/{forward,misc,read,reverse,tail}.c
usr.bin/yes/yes.c
```

`usr.bin/` y `bin/` se traen completos (35 MB + 3.2 MB) para poder sumar utilidades nuevas al
kata sin volver a clonar. `include/` (722 KB) se trae porque es la fuente de verdad de las
cabeceras BSD que `compat/` emula.

### BusyBox

| Campo | Valor |
| :--- | :--- |
| Repositorio | <https://git.busybox.net/busybox/> |
| Commit | `74ac096e895acd6b02976bb010e9b3511234e899` |
| Fecha | 2026-08-17 23:16:38 +0000 |
| Asunto | `sort: compute -k END.ENDCHAR from the end field, not the line start` |
| Rama | `master` |
| Profundidad | `--depth 1` (shallow) |
| Tipo | Clon completo del árbol (20 MB) |

**No participa del build.** Se usa exclusivamente como material de análisis comparativo en
[`Docs/porter-guide/09-estudios-de-caso-utilidad-por-utilidad.md`](Docs/porter-guide/09-estudios-de-caso-utilidad-por-utilidad.md),
contrastando tamaño, modularidad y estrategia de `libbb` contra la implementación de OpenBSD.

---

## Verificar la procedencia de un árbol ya bajado

```sh
./fetch-upstream.sh --verify          # Linux / macOS / Git Bash
pwsh ./fetch-upstream.ps1 -Verify     # Windows
```

Compara el `HEAD` real de cada árbol contra los SHA de arriba y avisa si hay divergencia.
También reporta si el árbol trae modificaciones locales (lo esperado es **cero**).

> **Nota histórica (2026-09-03):** el clon original de `openbsd-src` apareció con dos archivos
> sucios en `regress/` — `usr.bin/awk/T/Makefile` y `usr.bin/rcs/rcs-Aflag.out`. Ninguno es
> trabajo propio: el primero es una revisión anterior del archivo que quedó en el working tree,
> y el segundo es la salida de un test de regress que se reescribió a sí mismo al ejecutarse.
> Ambos quedan fuera del sparse checkout, así que no vuelven a aparecer.

---

## Actualizar a un commit más nuevo

1. Editar los SHA en `UPSTREAM.md` **y** en `fetch-upstream.ps1` / `fetch-upstream.sh`
   (las constantes `$OpenBsdSha` / `$BusyboxSha` al inicio de cada script).
2. Borrar los árboles y rehidratar:
   ```sh
   rm -rf openbsd-src busybox-src
   ./fetch-upstream.sh
   ```
3. Recompilar y correr `tests/run-suite.*` para detectar cambios de comportamiento:
   ```sh
   cmake -B build -S . && cmake --build build --config Release
   ```
4. Anotar el salto de commit en la tabla de arriba (agregar una fila "anterior" si hubo
   cambios de comportamiento que afecten a la capa `compat/`).
