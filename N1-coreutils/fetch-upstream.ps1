<#
.SYNOPSIS
    Rehidrata los árboles de código fuente upstream de este kata, pinneados a commits exactos.

.DESCRIPTION
    Este kata no versiona openbsd-src/ ni busybox-src/ (2 GB de material de referencia).
    Este script los reconstruye bajo demanda:

      openbsd-src/  -> sparse checkout (solo bin/, usr.bin/, include/) del commit fijado.
                       ~40 MB en vez de ~2 GB.
      busybox-src/  -> clon shallow completo del commit fijado. ~20 MB.

    Los SHA están en UPSTREAM.md; si los actualizás allá, actualizalos acá también.

    Compatible con Windows PowerShell 5.1 (no requiere pwsh 7).

.PARAMETER Verify
    No descarga nada. Solo compara el HEAD de cada árbol contra el SHA esperado y reporta
    si hay modificaciones locales.

.PARAMETER Force
    Borra los árboles existentes y los vuelve a bajar aunque ya estén correctos.

.EXAMPLE
    .\fetch-upstream.ps1
    .\fetch-upstream.ps1 -Verify
    .\fetch-upstream.ps1 -Force
#>
[CmdletBinding()]
param(
    [switch]$Verify,
    [switch]$Force
)

Set-StrictMode -Version Latest

# --- Commits congelados (espejo de UPSTREAM.md) -----------------------------
$OpenBsdUrl = 'https://github.com/openbsd/src'
$OpenBsdSha = '9477e7ac084034d747d8a0505752dd7fa48b4392'
# Solo estas rutas del árbol de OpenBSD. bin/ + usr.bin/ = 38 MB, include/ = 722 KB.
# Todo el resto (sys/, gnu/, lib/, regress/, usr.sbin/) es lo que inflaba el clon a 2 GB.
$OpenBsdPaths = @('bin', 'usr.bin', 'include')

$BusyboxUrl = 'https://git.busybox.net/busybox/'
$BusyboxSha = '74ac096e895acd6b02976bb010e9b3511234e899'

# --- Helpers ----------------------------------------------------------------
$RepoRoot = $PSScriptRoot

function Write-Step { param([string]$m) Write-Host "`n==> $m" -ForegroundColor Cyan }
function Write-Ok   { param([string]$m) Write-Host "    OK  $m" -ForegroundColor Green }
function Write-Warn { param([string]$m) Write-Host "    !   $m" -ForegroundColor Yellow }
function Write-Fail { param([string]$m) Write-Host "    X   $m" -ForegroundColor Red }
function Write-Dim  { param([string]$m) Write-Host "    $m" -ForegroundColor DarkGray }

# Invoca git y devuelve su exit code, sin dejar que stderr de un comando nativo
# dispare NativeCommandError (comportamiento de PowerShell 5.1 al redirigir 2>&1).
function Invoke-Git {
    param(
        [Parameter(Mandatory)][string[]]$GitArgs,
        [switch]$ShowOutput
    )
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'SilentlyContinue'
    try {
        if ($ShowOutput) {
            & git @GitArgs
        } else {
            & git @GitArgs 2>&1 | Out-Null
        }
        return $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $prev
    }
}

function Assert-Git {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        throw 'git no está en el PATH. Instalá Git for Windows y volvé a intentar.'
    }
}

function Get-TreeHead {
    param([string]$Path)
    if (-not (Test-Path (Join-Path $Path '.git'))) { return $null }
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'SilentlyContinue'
    try {
        $sha = & git -C $Path rev-parse HEAD 2>$null
        if ($LASTEXITCODE -ne 0) { return $null }
        return ([string]$sha).Trim()
    }
    finally { $ErrorActionPreference = $prev }
}

function Get-TreeDirty {
    param([string]$Path)
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'SilentlyContinue'
    try {
        $out = & git -C $Path status --porcelain 2>$null
        return @($out | Where-Object { $_ -match '\S' })
    }
    finally { $ErrorActionPreference = $prev }
}

function Get-DirSizeMB {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return 0 }
    $bytes = (Get-ChildItem -LiteralPath $Path -Recurse -File -Force -ErrorAction SilentlyContinue |
              Measure-Object -Property Length -Sum).Sum
    if (-not $bytes) { return 0 }
    return [math]::Round($bytes / 1MB, 1)
}

function Remove-Tree {
    param([string]$Path)
    Write-Dim 'borrando árbol existente...'
    # git marca muchos objetos como read-only; Remove-Item -Force a veces no alcanza.
    & cmd /c "attrib -r -h -s `"$Path`" /s /d" 2>&1 | Out-Null
    Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction SilentlyContinue
    if (Test-Path $Path) {
        throw "No se pudo borrar $Path. Cerrá cualquier programa que lo esté usando y reintentá."
    }
}

# --- Verificación -----------------------------------------------------------
function Test-Tree {
    param([string]$Name, [string]$Path, [string]$Expected)

    Write-Step "Verificando $Name"
    $head = Get-TreeHead -Path $Path
    if (-not $head) {
        Write-Warn "$Name no existe (o no es un repo git). Corré sin -Verify para bajarlo."
        return $false
    }
    if ($head -ne $Expected) {
        Write-Fail 'HEAD divergente'
        Write-Host "        esperado: $Expected"
        Write-Host "        real:     $head"
        return $false
    }
    Write-Ok "HEAD $($head.Substring(0,12)) coincide"

    $dirty = Get-TreeDirty -Path $Path
    if ($dirty.Count -gt 0) {
        Write-Warn "$($dirty.Count) archivo(s) con cambios locales — se esperaba cero en un árbol de referencia:"
        $dirty | Select-Object -First 10 | ForEach-Object { Write-Host "          $_" }
        return $false
    }
    Write-Ok "árbol limpio ($(Get-DirSizeMB $Path) MB)"
    return $true
}

# --- Descarga: OpenBSD (sparse) --------------------------------------------
function Fetch-OpenBsd {
    $dest = Join-Path $RepoRoot 'openbsd-src'

    Write-Step "OpenBSD src -> openbsd-src/ (sparse: $($OpenBsdPaths -join ', '))"

    if (Test-Path $dest) {
        if ($Force) { Remove-Tree -Path $dest }
        else {
            Write-Warn 'ya existe openbsd-src/. Usá -Force para rebajarlo, o -Verify para chequearlo.'
            return
        }
    }

    New-Item -ItemType Directory -Path $dest -Force | Out-Null

    Invoke-Git @('-C', $dest, 'init', '--quiet')                      | Out-Null
    Invoke-Git @('-C', $dest, 'remote', 'add', 'origin', $OpenBsdUrl) | Out-Null

    # --filter=blob:none trae commits y árboles de directorios pero ningún blob.
    # Los blobs de las rutas sparse se bajan perezosamente en el checkout, que es
    # justamente lo que hace que esto pese 40 MB y no 300.
    Write-Dim "fetching $($OpenBsdSha.Substring(0,12)) (depth 1, sin blobs)..."
    $rc = Invoke-Git @('-C', $dest, 'fetch', '--depth', '1', '--filter=blob:none',
                       '--quiet', 'origin', $OpenBsdSha) -ShowOutput

    if ($rc -ne 0) {
        # Servidor sin soporte de partial clone, o SHA no alcanzable por fetch directo.
        Write-Warn 'fetch filtrado falló; reintentando sin filtro (pack completo, ~300 MB)...'
        $rc = Invoke-Git @('-C', $dest, 'fetch', '--depth', '1', 'origin', $OpenBsdSha) -ShowOutput
        if ($rc -ne 0) {
            throw "No se pudo bajar el commit $OpenBsdSha desde $OpenBsdUrl. Verificá la red y que el SHA siga existiendo."
        }
    }

    Invoke-Git @('-C', $dest, 'sparse-checkout', 'init', '--cone') | Out-Null
    $sparseArgs = @('-C', $dest, 'sparse-checkout', 'set') + $OpenBsdPaths
    Invoke-Git $sparseArgs | Out-Null
    if ((Invoke-Git @('-C', $dest, 'checkout', '--quiet', 'FETCH_HEAD')) -ne 0) {
        throw 'Falló el checkout sparse de openbsd-src.'
    }

    Write-Ok "listo — $(Get-DirSizeMB $dest) MB en disco"
}

# --- Descarga: BusyBox (completo) ------------------------------------------
function Fetch-Busybox {
    $dest = Join-Path $RepoRoot 'busybox-src'

    Write-Step 'BusyBox -> busybox-src/ (clon completo)'

    if (Test-Path $dest) {
        if ($Force) { Remove-Tree -Path $dest }
        else {
            Write-Warn 'ya existe busybox-src/. Usá -Force para rebajarlo, o -Verify para chequearlo.'
            return
        }
    }

    New-Item -ItemType Directory -Path $dest -Force | Out-Null

    Invoke-Git @('-C', $dest, 'init', '--quiet') | Out-Null
    Invoke-Git @('-C', $dest, 'remote', 'add', 'origin', $BusyboxUrl) | Out-Null

    Write-Dim "fetching $($BusyboxSha.Substring(0,12)) (depth 1)..."
    $rc = Invoke-Git @('-C', $dest, 'fetch', '--depth', '1', '--quiet', 'origin', $BusyboxSha) -ShowOutput

    if ($rc -ne 0) {
        # cgit NO habilita uploadpack.allowReachableSHA1InWant, asi que no acepta que le
        # pidamos un SHA suelto ("Server does not allow request for unadvertised object").
        # Alternativa barata: profundizar desde master hasta que el commit entre en el
        # historial local. Con depth 200 alcanza (~5 MB) contra ~50 MB de historial completo.
        Write-Warn 'el remoto no permite fetch por SHA; profundizando desde master...'
        $found = $false
        foreach ($depth in 50, 200, 600, 1500, 5000) {
            if ((Invoke-Git @('-C', $dest, 'fetch', '--depth', "$depth", '--no-tags',
                              '--quiet', 'origin', 'master')) -ne 0) { continue }
            if ((Invoke-Git @('-C', $dest, 'cat-file', '-e', $BusyboxSha)) -eq 0) {
                Write-Dim "commit alcanzado con --depth $depth"
                $found = $true
                break
            }
        }
        if (-not $found) {
            throw "El commit $BusyboxSha no es alcanzable desde master. Actualizá `$BusyboxSha en este script y en UPSTREAM.md."
        }
    }

    # Tras la profundizacion FETCH_HEAD apunta a master, no al commit buscado; pero el
    # objeto ya esta local, asi que se puede hacer checkout del SHA directamente.
    if ((Invoke-Git @('-C', $dest, 'cat-file', '-e', $BusyboxSha)) -eq 0) {
        if ((Invoke-Git @('-C', $dest, 'checkout', '--quiet', $BusyboxSha)) -ne 0) {
            throw 'Falló el checkout de busybox-src.'
        }
    } elseif ((Invoke-Git @('-C', $dest, 'checkout', '--quiet', 'FETCH_HEAD')) -ne 0) {
        throw 'Falló el checkout de busybox-src.'
    }

    Write-Ok "listo — $(Get-DirSizeMB $dest) MB en disco"
}

# --- Main -------------------------------------------------------------------
if ($Verify) {
    Write-Host "`nVerificando procedencia de los árboles upstream" -ForegroundColor White
    $okA = Test-Tree -Name 'openbsd-src' -Path (Join-Path $RepoRoot 'openbsd-src') -Expected $OpenBsdSha
    $okB = Test-Tree -Name 'busybox-src' -Path (Join-Path $RepoRoot 'busybox-src') -Expected $BusyboxSha
    if ($okA -and $okB) {
        Write-Host "`nAmbos árboles están en el commit pinneado y sin cambios locales.`n" -ForegroundColor Green
        exit 0
    }
    Write-Host "`nHay divergencias (ver arriba).`n" -ForegroundColor Yellow
    exit 1
}

Assert-Git
Fetch-OpenBsd
Fetch-Busybox

Write-Host "`nListo. Recompilá con:  cmake -B build -S . ; cmake --build build --config Release`n" -ForegroundColor Green
