#!/usr/bin/env bash
# Compila y prueba la kata FAT/VFS.
#
#   1. Biblioteca con -ffreestanding y advertencias estrictas: lo que tiene que
#      compilar en un toolchain arm-none-eabi.
#   2. Tests en RAM (tests/test_fat.c).
#   3. Validación cruzada: fatimg escribe imágenes FAT12/16/32 y 7-Zip, que
#      tiene su propio lector de FAT, las extrae y se comparan byte a byte.
#
# Uso: ./tests/run-tests.sh        (CC=clang o CC=arm-none-eabi-gcc también sirven
#                                   para el paso 1)

set -u

# MSYS reescribe "/DOCS" como "C:/.../DOCS" al llamar a un .exe nativo. Se
# apaga, y por eso ROOT va en forma C:/... (pwd -W), que entienden todos.
export MSYS2_ARG_CONV_EXCL="*"

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && { pwd -W 2>/dev/null || pwd; })
OUT="$ROOT/build"
TMP="$OUT/tmp"
CC="${CC:-gcc}"
CFLAGS="-std=c99 -O2 -Wall -Wextra -pedantic -Wshadow -Wcast-align -I$ROOT/include"

EXE=''
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) EXE='.exe' ;;
esac

fail=0
step() { printf '\n==> %s\n' "$1"; }
ok()   { printf '  OK   %s\n' "$1"; }
bad()  { printf '  FAIL %s\n' "$1"; fail=$((fail + 1)); }

rm -rf "$TMP"
mkdir -p "$OUT" "$TMP"

step "Biblioteca freestanding ($CC)"
for f in fat vfs fat_vfs; do
    if "$CC" $CFLAGS -Werror -ffreestanding -c "$ROOT/src/$f.c" -o "$OUT/$f.o"; then
        ok "src/$f.c"
    else
        bad "src/$f.c"
    fi
done

# Lo que sigue corre en el host.
HOSTCC="${HOSTCC:-gcc}"
HOSTFLAGS="-std=c99 -O2 -Wall -Wextra -I$ROOT/include -DFAT_MAX_SECTOR_SIZE=4096"

step "Tests en RAM"
if "$HOSTCC" $HOSTFLAGS "$ROOT"/src/*.c "$ROOT/tests/test_fat.c" -o "$OUT/test_fat$EXE"; then
    if "$OUT/test_fat$EXE" > "$TMP/test_fat.log" 2>&1; then
        ok "$(tail -1 "$TMP/test_fat.log")"
    else
        cat "$TMP/test_fat.log"
        bad "test_fat"
    fi
else
    bad "compilar test_fat"
fi

step "fatimg contra 7-Zip"
if ! "$HOSTCC" $HOSTFLAGS "$ROOT/src/fat.c" "$ROOT/tools/fatimg.c" -o "$OUT/fatimg$EXE"; then
    bad "compilar fatimg"
elif ! command -v 7z >/dev/null 2>&1; then
    printf '  7z no está en el PATH; se saltea la validación cruzada.\n'
else
    FATIMG="$OUT/fatimg$EXE"
    SRC="$TMP/src"
    mkdir -p "$SRC/DOCS/VIEJO"
    head -c 300000  /dev/urandom > "$SRC/RANDOM.BIN"
    head -c 0       /dev/urandom > "$SRC/VACIO.TXT"
    head -c 4096    /dev/urandom > "$SRC/DOCS/SECTOR.BIN"
    printf 'hola desde la kata\n' > "$SRC/DOCS/VIEJO/LEEME.TXT"

    for spec in "12:1440K" "16:32M" "32:64M"; do
        t=${spec%%:*}
        size=${spec#*:}
        img="$TMP/fat$t.img"
        out="$TMP/x$t"

        "$FATIMG" mkfs "$img" "$size" "$t" KATA$t > /dev/null &&
        "$FATIMG" mkdir "$img" /DOCS &&
        "$FATIMG" mkdir "$img" /DOCS/VIEJO &&
        "$FATIMG" put "$img" "$SRC/RANDOM.BIN" /RANDOM.BIN &&
        "$FATIMG" put "$img" "$SRC/VACIO.TXT" /VACIO.TXT &&
        "$FATIMG" put "$img" "$SRC/DOCS/SECTOR.BIN" /DOCS/SECTOR.BIN &&
        "$FATIMG" put "$img" "$SRC/DOCS/VIEJO/LEEME.TXT" /DOCS/VIEJO/LEEME.TXT ||
            { bad "FAT$t: armar la imagen"; continue; }

        # Escribir, borrar y volver a escribir deja huecos en la FAT y en el
        # directorio: 7-Zip tiene que seguir viendo exactamente lo vivo.
        "$FATIMG" put "$img" "$SRC/RANDOM.BIN" /TEMP.BIN && "$FATIMG" rm "$img" /TEMP.BIN

        if ! 7z t -tFAT "$img" > "$TMP/7z-t$t.log" 2>&1; then
            bad "FAT$t: 7z t rechaza la imagen"
            sed 's/^/        /' "$TMP/7z-t$t.log" | tail -8
            continue
        fi
        7z x -tFAT -y -o"$out" "$img" > /dev/null 2>&1
        if diff -r "$SRC" "$out" > "$TMP/diff$t.log" 2>&1; then
            ok "FAT$t: 7-Zip extrae el mismo árbol, byte a byte"
        else
            bad "FAT$t: el árbol extraído difiere"
            sed 's/^/        /' "$TMP/diff$t.log" | head -8
        fi

        # Ida y vuelta por la propia herramienta.
        "$FATIMG" get "$img" /RANDOM.BIN "$TMP/back$t.bin"
        if cmp -s "$SRC/RANDOM.BIN" "$TMP/back$t.bin"; then
            ok "FAT$t: fatimg get devuelve lo mismo"
        else
            bad "FAT$t: fatimg get"
        fi
    done
fi

printf '\n'
if [ "$fail" -eq 0 ]; then
    printf 'Todo OK.\n'
    exit 0
fi
printf '%d fallas.\n' "$fail"
exit 1
