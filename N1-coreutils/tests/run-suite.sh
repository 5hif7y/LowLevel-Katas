#!/usr/bin/env bash
# Suite de verificación de las coreutils porteadas.
#
# Es POSIX sh a propósito y corre igual en Linux, macOS y Git Bash (Windows).
# No hay una versión en PowerShell: su pipeline convierte la salida de los
# comandos nativos en objetos y reescribe los saltos de línea, lo que arruinaría
# justamente las comparaciones byte-exactas que son el corazón de esta suite
# (ver la sección 5, modo binario vs texto).
#
# A diferencia del viejo test_suite.bat, esto **verifica**: compara la salida real
# contra la esperada y devuelve exit != 0 si algo falla. Nada de imprimir
# "ALL TESTS PASSED" pase lo que pase.
#
# Uso:
#   ./tests/run-suite.sh                 # autodetecta el directorio de binarios
#   BIN=/ruta/a/bin ./tests/run-suite.sh # fuerza una ruta
#
# Exit: 0 = todo pasó, 1 = al menos un test falló.

set -u

REPO_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMP="$REPO_ROOT/tests/tmp"

if [ -t 1 ]; then
    C_GREEN=$(printf '\033[32m'); C_RED=$(printf '\033[31m')
    C_CYAN=$(printf '\033[36m');  C_GREY=$(printf '\033[90m')
    C_BOLD=$(printf '\033[1m');   C_OFF=$(printf '\033[0m')
else
    C_GREEN=''; C_RED=''; C_CYAN=''; C_GREY=''; C_BOLD=''; C_OFF=''
fi

PASS=0
FAIL=0
FAILED_LIST=''

# --- Localizar los binarios --------------------------------------------------
# MSVC deja los .exe en build/bin/Release; los builds Unix caen en build/bin o build.
if [ -z "${BIN:-}" ]; then
    for cand in "$REPO_ROOT/build/bin/Release" "$REPO_ROOT/build/bin" "$REPO_ROOT/build"; do
        if [ -x "$cand/echo" ] || [ -x "$cand/echo.exe" ]; then
            BIN="$cand"
            break
        fi
    done
fi

if [ -z "${BIN:-}" ] || { [ ! -x "$BIN/echo" ] && [ ! -x "$BIN/echo.exe" ]; }; then
    printf '%sNo encontré los binarios.%s Compilá primero:\n' "$C_RED" "$C_OFF" >&2
    printf '    cmake -B build -S . && cmake --build build --config Release\n\n' >&2
    exit 1
fi

if [ -x "$BIN/echo.exe" ]; then EXE='.exe'; else EXE=''; fi

# t <tool> [args...] — invoca una utilidad PORTEADA (nunca la del sistema).
t() {
    local _tool=$1; shift
    "$BIN/$_tool$EXE" "$@"
}

# --- Aserciones --------------------------------------------------------------
check() {
    local _name=$1 _expected=$2 _actual=$3
    if [ "$_expected" = "$_actual" ]; then
        PASS=$((PASS + 1))
        printf '  %sPASS%s  %s\n' "$C_GREEN" "$C_OFF" "$_name"
    else
        FAIL=$((FAIL + 1))
        FAILED_LIST="$FAILED_LIST
    - $_name"
        printf '  %sFAIL%s  %s\n' "$C_RED" "$C_OFF" "$_name"
        printf '        %sesperado:%s %s\n' "$C_GREY" "$C_OFF" "$(printf '%s' "$_expected" | sed -n '1,3p' | sed 's/^/| /')"
        printf '        %sreal:    %s %s\n' "$C_GREY" "$C_OFF" "$(printf '%s' "$_actual"   | sed -n '1,3p' | sed 's/^/| /')"
    fi
}

check_rc() {
    local _name=$1 _expected=$2 _actual=$3
    if [ "$_expected" = "$_actual" ]; then
        PASS=$((PASS + 1))
        printf '  %sPASS%s  %s\n' "$C_GREEN" "$C_OFF" "$_name"
    else
        FAIL=$((FAIL + 1))
        FAILED_LIST="$FAILED_LIST
    - $_name"
        printf '  %sFAIL%s  %s  (exit esperado %s, real %s)\n' "$C_RED" "$C_OFF" "$_name" "$_expected" "$_actual"
    fi
}

section() { printf '\n%s%s%s\n' "$C_BOLD" "$1" "$C_OFF"; }

# Normaliza la salida de `wc`, que rellena con espacios a ancho fijo.
num() { sed 's/[[:space:]]//g'; }

# --- Setup -------------------------------------------------------------------
rm -rf "$TMP"
mkdir -p "$TMP"
cd "$TMP" || exit 1

printf 'uno\ndos\ntres\n' > three.txt
printf 'a\nb\n'           > lf.txt
printf 'a\r\nb\r\n'       > crlf.txt

printf '%scoreutils — suite de verificación%s\n' "$C_BOLD" "$C_OFF"
printf '%sbinarios: %s (%s)%s\n' "$C_GREY" "$BIN" "${EXE:-sin sufijo}" "$C_OFF"

# --- 1. Flujo y shell --------------------------------------------------------
section '1. Flujo y shell'

t true;  check_rc 'true devuelve 0'  0 $?
t false; check_rc 'false devuelve 1' 1 $?

t sleep 0;   check_rc 'sleep 0 termina'   0 $?
t sleep 0.1; check_rc 'sleep 0.1 termina' 0 $?

_pwd=$(t pwd)
case "$_pwd" in
    *N1-coreutils*) check 'pwd reporta el cwd' 'ok' 'ok' ;;
    *)              check 'pwd reporta el cwd' 'ruta con N1-coreutils' "$_pwd" ;;
esac

check 'echo imprime su argumento' 'hola mundo' "$(t echo 'hola mundo')"
check 'echo -n no agrega salto'   'sin salto'   "$(t echo -n 'sin salto')"

# yes es infinito: se lo corta con head.
check 'yes emite y repetido'    'y'  "$(t yes    | head -n 3 | sed -n '3p')"
check 'yes acepta argumento'    'ok' "$(t yes ok | head -n 3 | sed -n '3p')"

# --- 2. Procesamiento de texto ----------------------------------------------
section '2. Procesamiento de texto'

check 'tr: minúsculas a mayúsculas' 'HELLO WORLD' "$(printf 'hello world\n' | t tr 'a-z' 'A-Z')"
check 'tr -d: borra caracteres'     'abc'         "$(printf 'a1b2c3\n' | t tr -d '0-9')"

check 'head -n 1' 'uno' "$(t head -n 1 three.txt)"
check 'head -n 2' "$(printf 'uno\ndos')" "$(t head -n 2 three.txt)"
check 'tail -n 1' 'tres' "$(t tail -n 1 three.txt)"
check 'tail -n 2' "$(printf 'dos\ntres')" "$(t tail -n 2 three.txt)"

check 'wc -l'       '3'  "$(t wc -l < three.txt | num)"
check 'wc -w'       '3'  "$(t wc -w < three.txt | num)"
check 'wc -c'       '13' "$(t wc -c < three.txt | num)"

check 'cut -d: -f2'   'b'   "$(printf 'a:b:c\n' | t cut -d: -f2)"
check 'cut -d: -f1,3' 'a:c' "$(printf 'a:b:c\n' | t cut -d: -f1,3)"
check 'cut -c1-3'     'abc' "$(printf 'abcdef\n' | t cut -c1-3)"

check 'uniq: colapsa repetidos'  "$(printf 'a\nb')" "$(printf 'a\na\nb\n' | t uniq)"
check 'uniq -c: cuenta'          '2 a' "$(printf 'a\na\nb\n' | t uniq -c | sed -n '1p' | sed 's/^ *//;s/  */ /')"
check 'uniq -d: solo duplicados' 'a'   "$(printf 'a\na\nb\n' | t uniq -d)"

check 'printf: entero con relleno' '00042' "$(t printf '%05d' 42)"
check 'printf: flotante'           '3.14'  "$(t printf '%.2f' 3.14159)"
check 'printf: ancho de string'    '   ab' "$(t printf '%5s' ab)"
check 'printf: octal y hex'        '10 ff' "$(t printf '%o %x' 8 255)"

# --- 3. Archivos y rutas -----------------------------------------------------
section '3. Archivos y rutas'

# Ojo: en Git Bash (MSYS2) hay que desactivar la conversión automática de rutas,
# que si no reescribe /a/b/c.txt como A:/b/c.txt antes de que lo vea el .exe.
check 'basename: ruta con barras' 'c.txt' "$(MSYS_NO_PATHCONV=1 t basename /a/b/c.txt)"
check 'basename: quita sufijo'    'c'     "$(MSYS_NO_PATHCONV=1 t basename /a/b/c.txt .txt)"
check 'basename: entrada simple'  'c.txt' "$(t basename c.txt)"
check 'dirname: ruta con barras'  '/a/b'  "$(MSYS_NO_PATHCONV=1 t dirname /a/b/c.txt)"
check 'dirname: sin barras'       '.'     "$(t dirname c.txt)"

check 'cat: contenido exacto' "$(printf 'uno\ndos\ntres')" "$(t cat three.txt)"
check 'cat -n numera'         '1' "$(t cat -n three.txt | sed -n '1p' | sed 's/^ *//' | cut -f1)"

t mkdir sub
check 'mkdir crea el directorio' 'yes' "$([ -d sub ] && echo yes || echo no)"
t touch sub/tocado.txt
check 'touch crea el archivo'    'yes' "$([ -f sub/tocado.txt ] && echo yes || echo no)"
check 'ls lista un archivo'      'tocado.txt' "$(t ls sub)"

t rmdir sub; check_rc 'rmdir falla si no está vacío' 1 $?
rm -f sub/tocado.txt
t rmdir sub
check 'rmdir elimina el directorio' 'yes' "$([ -d sub ] && echo no || echo yes)"

# ls -l ejercita strmode(): el primer carácter codifica el tipo.
mkdir -p modedir
# -d lista el directorio mismo, no su contenido, así que no hay línea "total".
check 'ls -l: tipo directorio' 'd' "$(t ls -ld modedir | sed 's/^\(.\).*/\1/')"
check 'ls -l: tipo archivo'    '-' "$(t ls -l three.txt | sed 's/^\(.\).*/\1/')"
# Al listar el contenido de un directorio, la primera línea es "total N".
check 'ls -l: encabezado total' 'total 0' "$(t ls -l modedir | sed -n '1p')"
rmdir modedir

# --- 4. tee e I/O ------------------------------------------------------------
section '4. tee e I/O'

printf 'via tee\n' | t tee tee.out > stdout.capt
check 'tee: escribe el archivo'  'via tee' "$(t cat tee.out)"
check 'tee: también va a stdout' 'via tee' "$(t cat stdout.capt)"

printf 'bin\000ary\n' > bin.in
t tee bin.out < bin.in > /dev/null 2>&1
check 'tee: round-trip byte-exacto' \
      "$(t wc -c < bin.in | num)" \
      "$(t wc -c < bin.out | num)"

# --- 5. Modo binario vs texto (cap. 03 de la guía) ---------------------------
# Esta es LA regresión clásica al portar a Windows: si el CRT abre stdout en modo
# texto, cada \n se traduce a \r\n y todos los conteos de bytes mienten.
section '5. Modo binario vs texto (CRLF)'

check 'wc -c sobre LF no agrega CR' '4' "$(t wc -c < lf.txt | num)"
check 'wc -l cuenta LF'             '2' "$(t wc -l < lf.txt | num)"
check 'wc -l cuenta CRLF'           '2' "$(t wc -l < crlf.txt | num)"
check 'wc -c conserva bytes CRLF'   '6' "$(t wc -c < crlf.txt | num)"

check 'cat preserva CRLF' '6' "$(t cat crlf.txt | t wc -c | num)"

# --- Resumen -----------------------------------------------------------------
cd "$REPO_ROOT" || exit 1
rm -rf "$TMP"

printf '\n%s%s%s\n' "$C_BOLD" '────────────────────────────────────────' "$C_OFF"
if [ "$FAIL" -eq 0 ]; then
    printf '%s%d/%d tests pasaron.%s\n\n' "$C_GREEN" "$PASS" "$((PASS + FAIL))" "$C_OFF"
    exit 0
fi

printf '%s%d/%d tests pasaron, %d fallaron:%s%s\n\n' \
       "$C_RED" "$PASS" "$((PASS + FAIL))" "$FAIL" "$FAILED_LIST" "$C_OFF"
exit 1
