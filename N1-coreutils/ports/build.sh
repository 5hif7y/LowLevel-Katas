#!/usr/bin/env bash
# Compila los 24 ports (12 en Odin, 12 en C++17) a ports/bin/.
#
# Uso:
#   ./ports/build.sh              # los dos lenguajes
#   ./ports/build.sh odin         # sólo Odin
#   ./ports/build.sh cpp17        # sólo C++17
#
# Herramientas: odin (dev-2026-10 o posterior) y g++ con -std=c++17.
# En Windows, g++ de MinGW-w64 UCRT sirve; el resultado son .exe.

set -u

PORTS_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT="$PORTS_DIR/bin"
WHAT="${1:-todo}"

# Lista única de utilidades: tiene que coincidir con la tabla de README.md.
TOOLS='true false yes echo basename dirname cat wc head tee cut uniq'

if [ -t 1 ]; then
    G=$(printf '\033[32m'); R=$(printf '\033[31m'); Y=$(printf '\033[33m')
    C=$(printf '\033[36m'); O=$(printf '\033[0m')
else
    G=''; R=''; Y=''; C=''; O=''
fi

EXE=''
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) EXE='.exe' ;;
esac

mkdir -p "$OUT"

ok=0
fail=0

report() {
    if [ "$1" = 0 ]; then
        ok=$((ok + 1))
        printf '  %sOK%s   %s\n' "$G" "$O" "$2"
    else
        fail=$((fail + 1))
        printf '  %sFAIL%s %s\n' "$R" "$O" "$2"
    fi
}

# --- Odin --------------------------------------------------------------------
build_odin() {
    if ! command -v odin >/dev/null 2>&1; then
        printf '%sOdin no está en el PATH; se saltea.%s\n' "$Y" "$O"
        return
    fi

    printf '\n%s==> Odin%s\n' "$C" "$O"
    for t in $TOOLS; do
        err=$(odin build "$PORTS_DIR/odin/$t" -out:"$OUT/$t$EXE" -o:speed 2>&1)
        rc=$?
        if [ $rc -eq 0 ] && [ -z "$err" ]; then
            report 0 "odin/$t"
        else
            report 1 "odin/$t"
            printf '%s\n' "$err" | head -5 | sed 's/^/        /'
        fi
    done
}

# --- C++17 -------------------------------------------------------------------
build_cpp17() {
    if ! command -v g++ >/dev/null 2>&1; then
        printf '%sg++ no está en el PATH; se saltea.%s\n' "$Y" "$O"
        return
    fi

    printf '\n%s==> C++17%s\n' "$C" "$O"
    for t in $TOOLS; do
        # El binario se llama distinto para no pisar el de Odin.
        err=$(g++ -std=c++17 -O2 -Wall -Wextra \
                  -I"$PORTS_DIR/cpp17" \
                  "$PORTS_DIR/cpp17/$t/main.cpp" \
                  -o "$OUT/$t-cpp$EXE" 2>&1)
        rc=$?
        if [ $rc -eq 0 ] && [ -z "$err" ]; then
            report 0 "cpp17/$t"
        else
            report 1 "cpp17/$t"
            printf '%s\n' "$err" | head -5 | sed 's/^/        /'
        fi
    done
}

case "$WHAT" in
    odin)   build_odin ;;
    cpp17)  build_cpp17 ;;
    todo|*) build_odin; build_cpp17 ;;
esac

printf '\n%s────────────────────────────────%s\n' "$C" "$O"
if [ "$fail" -eq 0 ]; then
    printf '%s%d/%d compilados.%s Binarios en %s\n\n' "$G" "$ok" "$((ok + fail))" "$O" "$OUT"
    exit 0
fi
printf '%s%d/%d compilados, %d fallaron.%s\n\n' "$R" "$ok" "$((ok + fail))" "$fail" "$O"
exit 1
