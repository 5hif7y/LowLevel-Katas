# N5 smoke tests: drive dtach/abduco/dvtm through tests/ptyrun.exe (a scripted
# ConPTY terminal) and check what they render. Run after build.bat:
#     powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1 [-Only dtach]
param([string]$Only = "")

$ErrorActionPreference = "Continue"  # PS 5.1 turns native stderr into errors
$root = Split-Path -Parent $PSScriptRoot
$B    = Join-Path $root "build\bin\Release"
$tmp  = Join-Path $root "tests\tmp"
New-Item -ItemType Directory -Force $tmp | Out-Null
Get-ChildItem -LiteralPath $tmp -Force | ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force -Recurse }
Push-Location $root

$script:pass = 0; $script:fail = 0
function Check([string]$name, [bool]$ok, [string]$detail = "") {
    if ($ok) { $script:pass++; Write-Host "  ok   $name" -ForegroundColor Green }
    else     { $script:fail++; Write-Host "  FAIL $name $detail" -ForegroundColor Red }
}
# Run a client inside a fresh pseudoconsole; returns the rendered VT stream.
# Keys use ptyrun escapes: \r \e \xHH; "RESIZE CxR" resizes the terminal.
function Term([string[]]$keys, [int]$ms, [string[]]$cmd, [int]$cols = 80, [int]$rows = 25) {
    $a = @("-c", $cols, "-r", $rows, "-t", $ms)
    foreach ($k in $keys) { $a += @("-k", $k) }
    $a += "--"; $a += $cmd
    $out = Join-Path $tmp "term.out"
    & (Join-Path $B "ptyrun.exe") @a > $out 2> (Join-Path $tmp "term.err")
    return [string](Get-Content -LiteralPath $out -Raw)
}
function Sessions() { return [string]((& $abduco) -join "`n") }

$dtach  = Join-Path $B "dtach.exe"
$abduco = Join-Path $B "abduco.exe"
$dvtm   = Join-Path $B "dvtm.exe"
$err    = Join-Path $tmp "e.txt"

if ((-not $Only -or $Only -eq "dtach") -and (Test-Path $dtach)) {
    Write-Host "dtach"
    $s = "tests/tmp/d1"
    & $dtach -n $s cmd.exe
    Check "-n creates a detached session" ($LASTEXITCODE -eq 0 -and (Test-Path $s))
    $t = Term @('1500:echo marker123\r', '1500:\x1c') 6000 @($dtach, "-a", $s)
    Check "attach shows the program and runs input" ($t -match 'marker123')
    Check "Ctrl+\ detaches" ($t -match '\[detached\]')
    Check "session survives detach" (Test-Path $s)
    "echo pushed-by-p`r" | & $dtach -p $s
    $t = Term @('1500:exit\r') 8000 @($dtach, "-a", $s)
    Check "reattach repaints previous screen" ($t -match 'marker123')
    Check "-p pushes stdin into the session" ($t -match 'pushed-by-p')
    Check "program exit ends client with EOF" ($t -match 'EOF - dtach terminating')
    Start-Sleep -Milliseconds 500
    Check "socket removed on exit" (-not (Test-Path $s))
    & $dtach -n $s does-not-exist.exe 2> $err
    Check "exec failure reported to client" ($LASTEXITCODE -ne 0 -and ([string](Get-Content $err -Raw)) -match 'could not execute')
    $t = Term @('2000:echo created-by-A\r', '1500:exit\r') 7000 @($dtach, "-A", "tests/tmp/d2", "cmd.exe")
    Check "-A creates and attaches" ($t -match 'created-by-A')
    $t = Term @('2000:RESIZE 100x30', '1000:mode con\r', '1500:exit\r') 8000 @($dtach, "-A", "tests/tmp/d3", "cmd.exe")
    Check "terminal resize reaches the program" ($t -match 'Columns:\s+100')
}

if ((-not $Only -or $Only -eq "abduco") -and (Test-Path $abduco)) {
    Write-Host "abduco"
    $env:ABDUCO_SOCKET_DIR = $tmp
    & $abduco -n a1 cmd.exe
    Check "-n creates a session, listed" ($LASTEXITCODE -eq 0 -and (Sessions) -match '\sa1(\n|$)')
    $t = Term @('1500:echo abd-marker\r', '1500:\x1c') 6000 @($abduco, "-a", "a1")
    Check "attach runs input" ($t -match 'abd-marker')
    Check "Ctrl+\ detaches" ($t -match 'detached')
    $t = Term @('1500:exit 3\r') 7000 @($abduco, "-a", "a1")
    Check "reattach repaints" ($t -match 'abd-marker')
    Check "exit status is propagated" ($t -match 'exit status 3')
    Start-Sleep -Milliseconds 800
    Check "server gone after exit" ((Sessions) -notmatch '\sa1(\n|$)')
    & $abduco -n a2 cmd.exe /c "exit 5"
    Start-Sleep -Milliseconds 1500
    Check "terminated session flagged '+'" ((Sessions) -match '(?m)^\+.*\sa2$')
    $t = Term @() 4000 @($abduco, "-a", "a2")
    Check "late attach gets exit status" ($t -match 'exit status 5')
    Start-Sleep -Milliseconds 800
    Check "server gone after late attach" ((Sessions) -notmatch '\sa2(\n|$)')
    & $abduco -n a3 does-not-exist.exe 2> $err
    Check "exec failure reported" ($LASTEXITCODE -ne 0 -and ([string](Get-Content $err -Raw)) -match 'server-execvp')
    Remove-Item env:ABDUCO_SOCKET_DIR
}

if ((-not $Only -or $Only -eq "dvtm") -and (Test-Path $dvtm)) {
    Write-Host "dvtm"
    # default modifier is Ctrl+G (\x07); MOD c = create, MOD q q = quit
    $t = Term @('2500:echo dvtm-one\r', '1000:\x07c', '2500:echo dvtm-two\r', '1500:\x07qq') 12000 @($dvtm, "cmd.exe") 120 30
    Check "first window runs cmd" ($t -match 'dvtm-one')
    Check "Ctrl+G c opens a second window" ($t -match 'dvtm-two')
    Check "Ctrl+G q q quits" ($t -match '\[exit 0\]')
    # no argument: the window runs $SHELL/%COMSPEC%, and when THAT exits dvtm
    # quits (a non-shell command would be replaced by a fresh shell instead)
    $t = Term @('2500:exit\r') 8000 @($dvtm) 100 30
    Check "last window exiting ends dvtm" ($t -match '\[exit 0\]')
    $t = Term @('2500:RESIZE 140x40', '1500:mode con\r', '1500:\x07qq') 9000 @($dvtm, "cmd.exe") 100 30
    Check "resize reaches the window's program" ($t -match 'Columns:\s+1[34]\d')

    if (Test-Path $abduco) {
        Write-Host "abduco + dvtm"
        $env:ABDUCO_SOCKET_DIR = $tmp
        $t = Term @('3000:echo inside-dvtm\r', '1500:\x1c') 8000 @($abduco, "-c", "ad", $dvtm, "cmd.exe") 100 30
        Check "dvtm runs under abduco" ($t -match 'inside-dvtm')
        Check "detach leaves dvtm running" ((Sessions) -match '\sad(\n|$)')
        $t = Term @('2500:\x07qq') 8000 @($abduco, "-a", "ad") 100 30
        Check "reattach shows dvtm screen" ($t -match 'inside-dvtm')
        Check "quitting dvtm ends the session" ($t -match 'exit status 0')
        Remove-Item env:ABDUCO_SOCKET_DIR
    }
}

Pop-Location
Write-Host ""
Write-Host "passed $script:pass, failed $script:fail"
exit $script:fail
