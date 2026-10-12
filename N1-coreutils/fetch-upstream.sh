#!/bin/sh
# Rehidrata los árboles de código fuente upstream de este kata, pinneados a commits exactos.
#
# Este kata no versiona openbsd-src/ ni busybox-src/ (2 GB de material de referencia).
# Este script los reconstruye bajo demanda:
#
#   openbsd-src/  -> sparse checkout (solo bin/, usr.bin/, include/) del commit fijado.
#                    ~40 MB en vez de ~2 GB.
#   busybox-src/  -> clon shallow completo del commit fijado. ~20 MB.
#
# Los SHA están en UPSTREAM.md; si los actualizás allá, actualizalos acá también.
#
# Uso:
#   ./fetch-upstream.sh              # baja lo que falte
#   ./fetch-upstream.sh --verify     # no baja nada, solo compara HEAD contra el SHA esperado
#   ./fetch-upstream.sh --force      # borra y rebaja todo

set -eu

# --- Commits congelados (espejo de UPSTREAM.md) -----------------------------
OPENBSD_URL='https://github.com/openbsd/src'
OPENBSD_SHA='9477e7ac084034d747d8a0505752dd7fa48b4392'
# Solo estas rutas del árbol de OpenBSD. bin/ + usr.bin/ = 38 MB, include/ = 722 KB.
# Todo el resto (sys/, gnu/, lib/, regress/, usr.sbin/) es lo que inflaba el clon a 2 GB.
OPENBSD_PATHS='bin usr.bin include'

BUSYBOX_URL='https://git.busybox.net/busybox/'
BUSYBOX_SHA='74ac096e895acd6b02976bb010e9b3511234e899'

# --- Helpers ----------------------------------------------------------------
REPO_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ -t 1 ]; then
    C_CYAN=$(printf '\033[36m'); C_GREEN=$(printf '\033[32m')
    C_YELLOW=$(printf '\033[33m'); C_RED=$(printf '\033[31m')
    C_GREY=$(printf '\033[90m'); C_OFF=$(printf '\033[0m')
else
    C_CYAN=''; C_GREEN=''; C_YELLOW=''; C_RED=''; C_GREY=''; C_OFF=''
fi

step()  { printf '\n%s==> %s%s\n' "$C_CYAN" "$1" "$C_OFF"; }
ok()    { printf '    %sOK%s  %s\n' "$C_GREEN" "$C_OFF" "$1"; }
warn()  { printf '    %s!%s   %s\n' "$C_YELLOW" "$C_OFF" "$1"; }
fail()  { printf '    %sX%s   %s\n' "$C_RED" "$C_OFF" "$1"; }
dim()   { printf '    %s%s%s\n' "$C_GREY" "$1" "$C_OFF"; }
die()   { fail "$1"; exit 1; }

need_git() {
    command -v git >/dev/null 2>&1 || die 'git no está en el PATH.'
}

tree_head() {
    [ -d "$1/.git" ] || return 1
    git -C "$1" rev-parse HEAD 2>/dev/null
}

tree_size_mb() {
    [ -e "$1" ] || { printf '0'; return; }
    du -sm "$1" 2>/dev/null | cut -f1
}

# --- Verificación -----------------------------------------------------------
verify_tree() {
    _name=$1; _path=$2; _expect=$3

    step "Verificando $_name"

    if ! _head=$(tree_head "$_path"); then
        warn "$_name no existe (o no es un repo git). Corré sin --verify para bajarlo."
        return 1
    fi

    if [ "$_head" != "$_expect" ]; then
        fail 'HEAD divergente'
        printf '        esperado: %s\n' "$_expect"
        printf '        real:     %s\n' "$_head"
        return 1
    fi
    ok "HEAD $(printf '%s' "$_head" | cut -c1-12) coincide"

    _dirty=$(git -C "$_path" status --porcelain 2>/dev/null | grep -c . || true)
    if [ "$_dirty" -gt 0 ]; then
        warn "$_dirty archivo(s) con cambios locales — se esperaba cero en un árbol de referencia:"
        git -C "$_path" status --porcelain | head -10 | sed 's/^/          /'
        return 1
    fi
    ok "árbol limpio ($(tree_size_mb "$_path") MB)"
    return 0
}

# --- Descarga: OpenBSD (sparse) --------------------------------------------
fetch_openbsd() {
    _dest="$REPO_ROOT/openbsd-src"
    _short=$(printf '%s' "$OPENBSD_SHA" | cut -c1-12)

    step "OpenBSD src -> openbsd-src/ (sparse: $OPENBSD_PATHS)"

    if [ -e "$_dest" ]; then
        if [ "$FORCE" = 1 ]; then
            dim 'borrando árbol existente...'
            rm -rf "$_dest"
        else
            warn 'ya existe openbsd-src/. Usá --force para rebajarlo, o --verify para chequearlo.'
            return 0
        fi
    fi

    mkdir -p "$_dest"
    git -C "$_dest" init --quiet
    git -C "$_dest" remote add origin "$OPENBSD_URL"

    # --filter=blob:none trae commits y árboles de directorios pero ningún blob.
    # Los blobs de las rutas sparse se bajan perezosamente en el checkout, que es
    # justamente lo que hace que esto pese 40 MB y no 300.
    dim "fetching $_short (depth 1, sin blobs)..."
    if ! git -C "$_dest" fetch --depth 1 --filter=blob:none origin "$OPENBSD_SHA" >/dev/null 2>&1; then
        # Servidor sin soporte de partial clone, o SHA no alcanzable por fetch directo.
        warn 'fetch filtrado falló; reintentando sin filtro (pack completo, ~300 MB)...'
        git -C "$_dest" fetch --depth 1 origin "$OPENBSD_SHA" \
            || die "No se pudo bajar el commit $OPENBSD_SHA desde $OPENBSD_URL."
    fi

    # shellcheck disable=SC2086 # OPENBSD_PATHS debe expandirse a palabras sueltas
    git -C "$_dest" sparse-checkout init --cone
    # shellcheck disable=SC2086
    git -C "$_dest" sparse-checkout set $OPENBSD_PATHS
    git -C "$_dest" checkout --quiet FETCH_HEAD || die 'Falló el checkout sparse de openbsd-src.'

    ok "listo — $(tree_size_mb "$_dest") MB en disco"
}

# --- Descarga: BusyBox (completo) ------------------------------------------
fetch_busybox() {
    _dest="$REPO_ROOT/busybox-src"
    _short=$(printf '%s' "$BUSYBOX_SHA" | cut -c1-12)

    step 'BusyBox -> busybox-src/ (clon completo)'

    if [ -e "$_dest" ]; then
        if [ "$FORCE" = 1 ]; then
            dim 'borrando árbol existente...'
            rm -rf "$_dest"
        else
            warn 'ya existe busybox-src/. Usá --force para rebajarlo, o --verify para chequearlo.'
            return 0
        fi
    fi

    mkdir -p "$_dest"
    git -C "$_dest" init --quiet
    git -C "$_dest" remote add origin "$BUSYBOX_URL"

    dim "fetching $_short (depth 1)..."
    if ! git -C "$_dest" fetch --depth 1 origin "$BUSYBOX_SHA" >/dev/null 2>&1; then
        # cgit NO habilita uploadpack.allowReachableSHA1InWant, asi que no acepta que le
        # pidamos un SHA suelto ("Server does not allow request for unadvertised object").
        # Alternativa barata: profundizar desde master hasta que el commit entre en el
        # historial local. Con depth 200 alcanza (~5 MB) contra ~50 MB de historial completo.
        warn 'el remoto no permite fetch por SHA; profundizando desde master...'
        _found=0
        for _depth in 50 200 600 1500 5000; do
            git -C "$_dest" fetch --depth "$_depth" --no-tags --quiet origin master >/dev/null 2>&1 || continue
            if git -C "$_dest" cat-file -e "$BUSYBOX_SHA" 2>/dev/null; then
                dim "commit alcanzado con --depth $_depth"
                _found=1
                break
            fi
        done
        [ "$_found" = 1 ] || die "El commit $BUSYBOX_SHA no es alcanzable desde master. Actualizalo en este script y en UPSTREAM.md."
    fi

    # Tras la profundizacion FETCH_HEAD apunta a master, no al commit buscado; pero el
    # objeto ya esta local, asi que se puede hacer checkout del SHA directamente.
    if git -C "$_dest" cat-file -e "$BUSYBOX_SHA" 2>/dev/null; then
        git -C "$_dest" checkout --quiet "$BUSYBOX_SHA" || die 'Falló el checkout de busybox-src.'
    else
        git -C "$_dest" checkout --quiet FETCH_HEAD || die 'Falló el checkout de busybox-src.'
    fi

    ok "listo — $(tree_size_mb "$_dest") MB en disco"
}

# --- Main -------------------------------------------------------------------
VERIFY=0; FORCE=0
for _arg in "$@"; do
    case "$_arg" in
        --verify|-v) VERIFY=1 ;;
        --force|-f)  FORCE=1 ;;
        -h|--help)
            sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) die "Opción desconocida: $_arg  (usá --help)" ;;
    esac
done

if [ "$VERIFY" = 1 ]; then
    printf '\nVerificando procedencia de los árboles upstream\n'
    _rc=0
    verify_tree 'openbsd-src' "$REPO_ROOT/openbsd-src" "$OPENBSD_SHA" || _rc=1
    verify_tree 'busybox-src' "$REPO_ROOT/busybox-src" "$BUSYBOX_SHA" || _rc=1
    if [ "$_rc" = 0 ]; then
        printf '\n%sAmbos árboles están en el commit pinneado y sin cambios locales.%s\n\n' "$C_GREEN" "$C_OFF"
    else
        printf '\n%sHay divergencias (ver arriba).%s\n\n' "$C_YELLOW" "$C_OFF"
    fi
    exit "$_rc"
fi

need_git
fetch_openbsd
fetch_busybox

printf '\n%sListo. Recompilá con:  cmake -B build -S . && cmake --build build --config Release%s\n\n' \
    "$C_GREEN" "$C_OFF"
