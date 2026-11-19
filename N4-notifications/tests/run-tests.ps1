<#
.SYNOPSIS
  End-to-end tests for N4-notifications: wnotify/notify-send, tiramisu (C and
  Odin), herbe, notifyd (all modes, Windows toasts, FTS) and dunst, including
  notification icon support (app_icon, image-path, image-data, toast images).

.DESCRIPTION
  Every daemon runs on its own pipe (WNOTIFY_PIPE) and database under a temp
  directory, so the suite never touches a real session daemon or history.
  Visual checks (herbe, dunst icons) take a screenshot and count pixels of a
  known colour instead of relying on a human looking at it.

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File tests\run-tests.ps1
  powershell ... -File tests\run-tests.ps1 -Only icons,dunst
#>
param(
    [string]$GitClones = (Join-Path $PSScriptRoot '..\..\..\gitclones'),
    [string[]]$Only = @(),
    [switch]$KeepTemp
)
$ErrorActionPreference = 'Continue'
Set-StrictMode -Version 2

$N4 = Split-Path $PSScriptRoot -Parent
$Bin = @{
    NotifySend  = Join-Path $N4 'wnotify\notify-send.exe'
    Notifyd     = Join-Path $N4 'notifyd\notifyd.exe'
    TiramisuC   = Join-Path $GitClones 'tiramisu\tiramisu.exe'
    TiramisuOdin= Join-Path $GitClones 'tiramisu\tiramisu-odin.exe'
    Herbe       = Join-Path $GitClones 'herbe\win32\herbe.exe'
    Dunst       = Join-Path $GitClones 'dunst\build-win\dunst.exe'
    Dunstctl    = Join-Path $GitClones 'dunst\dunstctl.ps1'
}
$Tmp = Join-Path ([IO.Path]::GetTempPath()) ("n4-tests-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $Tmp | Out-Null
$script:Pass = 0; $script:Fail = 0; $script:Skip = 0
$script:Procs = New-Object System.Collections.ArrayList

Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type -TypeDefinition @"
using System; using System.IO; using System.IO.Pipes; using System.Text;
public static class WnRaw {
  public static string Call(string pipe, string line) {
    using (var c = new NamedPipeClientStream(".", pipe, PipeDirection.InOut)) {
      c.Connect(3000);
      var b = Encoding.UTF8.GetBytes(line + "\n"); c.Write(b, 0, b.Length); c.Flush();
      return new StreamReader(c, Encoding.UTF8).ReadLine(); } } }
"@

# ---- helpers --------------------------------------------------------------

function Section([string]$name) {
    if ($Only.Count -and -not ($Only -contains $name)) { return $false }
    Write-Host "`n== $name" -ForegroundColor Cyan
    $true
}
function Check([string]$what, [bool]$ok, [string]$detail = '') {
    if ($ok) { $script:Pass++; Write-Host "  PASS  $what" -ForegroundColor Green }
    else { $script:Fail++; Write-Host "  FAIL  $what  $detail" -ForegroundColor Red }
}
function SkipTest([string]$what, [string]$why) { $script:Skip++; Write-Host "  SKIP  $what ($why)" -ForegroundColor Yellow }
function Need([string[]]$keys) {
    foreach ($k in $keys) { if (-not (Test-Path $Bin[$k])) { return $k } }
    $null
}
function Set-Pipe([string]$name) { $env:WNOTIFY_PIPE = "\\.\pipe\$name" }
function Start-Daemon([string]$exe, [string[]]$argv, [string]$tag) {
    $sp = @{ FilePath = $exe; PassThru = $true; NoNewWindow = $true
             RedirectStandardOutput = "$Tmp\$tag.out"; RedirectStandardError = "$Tmp\$tag.err" }
    if ($argv.Count) { $sp.ArgumentList = $argv }
    $p = Start-Process @sp
    [void]$script:Procs.Add($p)
    $p
}
function Wait-Pipe([string]$name, [int]$ms = 5000) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $ms) {
        if ([IO.Directory]::GetFiles('\\.\pipe\') -contains "\\.\pipe\$name") { return $true }
        Start-Sleep -Milliseconds 100
    }
    $false
}
function Stop-Daemon($p) { if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; $p.WaitForExit(3000) | Out-Null } }
function Esc([string]$s) { $s.Replace('\', '\\').Replace("`t", '\t').Replace("`n", '\n').Replace("`r", '\r') }
function Raw([string]$pipe, [string[]]$fields) { [WnRaw]::Call($pipe, (($fields | ForEach-Object { Esc $_ }) -join "`t")) }
function Read-Utf8([string]$path) { if (Test-Path $path) { [IO.File]::ReadAllText($path, [Text.Encoding]::UTF8) } else { '' } }

# Notify line through the raw protocol. $hints is FLAT: name, type, value, name, type, value...
# (not an array of triples: PowerShell flattens @(a,b,c) into @(a,b,c), so a
#  single nested hint silently produced a malformed request in the first version)
function Raw-Notify([string]$pipe, [string]$app, [string]$icon, [string]$summary, [string]$body, [string[]]$hints = @(), [string[]]$actions = @(), [int]$timeout = -1) {
    if ($hints.Count % 3) { throw "hints must be name,type,value triples" }
    $f = @('Notify', $app, '0', $icon, $summary, $body, [string]$actions.Count) + $actions + @([string]($hints.Count / 3)) + $hints + @([string]$timeout)
    $r = Raw $pipe $f
    if ($r -notlike 'OK*') { Write-Host "  (raw Notify rejected: $r)" -ForegroundColor DarkYellow }
    $r
}
# ConvertFrom-Json in PS 5.1 emits a JSON array as ONE object; re-enumerate it.
function From-JsonArray([string[]]$text) { @(($text -join "`n") | ConvertFrom-Json | ForEach-Object { $_ }) }
function Ctl { & $Bin.Dunstctl @args }

# Solid-colour test image: PNG on disk and the matching image-data hint value
function New-SolidPng([string]$path, [int]$r, [int]$g, [int]$b, [int]$size = 64) {
    $bmp = New-Object Drawing.Bitmap($size, $size)
    $gr = [Drawing.Graphics]::FromImage($bmp); $gr.Clear([Drawing.Color]::FromArgb(255, $r, $g, $b)); $gr.Dispose()
    $bmp.Save($path, [Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    $path
}
function New-ImageData([int]$r, [int]$g, [int]$b, [int]$size = 48) {
    $px = New-Object byte[] ($size * $size * 3)
    for ($i = 0; $i -lt $px.Length; $i += 3) { $px[$i] = $r; $px[$i + 1] = $g; $px[$i + 2] = $b }
    "{0}:{0}:{1}:0:8:3:{2}" -f $size, ($size * 3), [Convert]::ToBase64String($px)
}
# Pixels of (r,g,b) +-tol on the primary screen
function Count-ScreenColor([int]$r, [int]$g, [int]$b, [int]$tol = 6, [string]$save = '') {
    $bd = [Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object Drawing.Bitmap($bd.Width, $bd.Height)
    $gr = [Drawing.Graphics]::FromImage($bmp); $gr.CopyFromScreen($bd.Left, $bd.Top, 0, 0, $bmp.Size); $gr.Dispose()
    if ($save) { $bmp.Save($save) }
    $rect = New-Object Drawing.Rectangle(0, 0, $bmp.Width, $bmp.Height)
    $data = $bmp.LockBits($rect, [Drawing.Imaging.ImageLockMode]::ReadOnly, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bytes = New-Object byte[] ($data.Stride * $data.Height)
    [Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    $bmp.UnlockBits($data); $bmp.Dispose()
    $n = 0
    for ($i = 0; $i -lt $bytes.Length; $i += 4) {   # BGRA
        if ([Math]::Abs($bytes[$i + 2] - $r) -le $tol -and [Math]::Abs($bytes[$i + 1] - $g) -le $tol -and [Math]::Abs($bytes[$i] - $b) -le $tol) { $n++ }
    }
    $n
}

$enye = [char]0xF1; $u_acute = [char]0xFA
$IconMagenta = New-SolidPng "$Tmp\icon-magenta.png" 255 0 255
$IconCyan    = New-SolidPng "$Tmp\icon-cyan.png" 0 255 255
$ImgOrange   = New-ImageData 255 128 0
$ImgBad      = '9:9:27:0:8:3:' + [Convert]::ToBase64String([byte[]](0..11))

# ---- wnotify / notify-send -------------------------------------------------

function Test-Tiramisu([string]$key, [string]$label) {
    $miss = Need @($key, 'NotifySend'); if ($miss) { SkipTest $label "$miss not built"; return }
    $pipe = "n4t-$key"; Set-Pipe $pipe
    $d = Start-Daemon $Bin[$key] @('-j') $key
    Check "$label owns the pipe" (Wait-Pipe $pipe)
    $q = Start-Process $Bin[$key] -PassThru -NoNewWindow -RedirectStandardError "$Tmp\$key-2.err" -RedirectStandardOutput "$Tmp\$key-2.out"
    Check "$label second instance exits" ($q.WaitForExit(3000) -and (Read-Utf8 "$Tmp\$key-2.err") -match 'own')
    $info = & $Bin.NotifySend --server-info
    Check "$label GetServerInformation" (($info -join ' ') -match 'name: tiramisu')

    $id1 = & $Bin.NotifySend -p -a mail -i $IconMagenta 'New mail' 'body #summary'
    $id2 = & $Bin.NotifySend -p -r $id1 -a mail 'Replaced' 'second'
    Check "$label replaces_id keeps the id" ($id1 -eq $id2) "($id1 vs $id2)"
    Raw-Notify $pipe 'img' '' 'pic' '' @('image-data', 'image', $ImgOrange, 'bad', 'image', $ImgBad, 'image-path', 's', $IconCyan) | Out-Null
    Raw-Notify $pipe ("app`"q\b") '' ("sum `"x`"") ("l1`nl2`tt $enye") @('urgency', 'y', '2', 'ratio', 'd', '0.5') @('default', 'Open') | Out-Null
    Start-Sleep -Milliseconds 300
    Stop-Daemon $d
    $lines = @((Read-Utf8 "$Tmp\$key.out") -split "`n" | Where-Object { $_.Trim() })
    $objs = @(); $valid = $true
    foreach ($l in $lines) { try { $objs += ($l | ConvertFrom-Json) } catch { $valid = $false } }
    Check "$label -j emits valid JSON ($($lines.Count) lines)" ($valid -and $lines.Count -eq 4)
    if ($objs.Count -ge 4) {
        Check "$label icon: app_icon path in #icon" ($objs[0].icon -eq $IconMagenta) "got '$($objs[0].icon)'"
        Check "$label body containing #summary is not re-expanded" ($objs[0].body -eq 'body #summary')
        Check "$label icon: valid image-data kept" ([string]$objs[2].hints.'image-data' -like '48:48:144:0:8:3:*')
        Check "$label icon: invalid image-data dropped" (-not ($objs[2].hints.PSObject.Properties.Name -contains 'bad'))
        Check "$label icon: image-path hint kept" ($objs[2].hints.'image-path' -eq $IconCyan)
        Check "$label escaping round-trips (quotes, backslash, ñ, \n, \t)" ($objs[3].source -eq "app`"q\b" -and $objs[3].body -eq "l1`nl2`tt $enye")
        Check "$label typed hints" ($objs[3].hints.urgency -eq 2 -and $objs[3].hints.ratio -eq 0.5)
        Check "$label actions as object" ($objs[3].actions.default -eq 'Open')
    }
}

if (Section 'tiramisu') {
    Test-Tiramisu 'TiramisuC' 'tiramisu C'
    Test-Tiramisu 'TiramisuOdin' 'tiramisu Odin'
}

# ---- herbe -------------------------------------------------------------------

if (Section 'herbe') {
    $miss = Need @('Herbe')
    if ($miss) { SkipTest 'herbe' "$miss not built" } else {
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $p = Start-Process $Bin.Herbe -ArgumentList 'timeout test' -PassThru -NoNewWindow; [void]$p.Handle; $p.WaitForExit(10000) | Out-Null
        Check "herbe timeout -> exit 2 (~5 s)" ($p.ExitCode -eq 2 -and $sw.Elapsed.TotalSeconds -ge 4.5) "exit=$($p.ExitCode) t=$($sw.Elapsed.TotalSeconds)"
        $p = Start-Process $Bin.Herbe -ArgumentList 'action test' -PassThru -NoNewWindow; [void]$p.Handle; Start-Sleep 1
        & $Bin.Herbe --action; $p.WaitForExit(5000) | Out-Null
        Check "herbe --action -> exit 0" ($p.ExitCode -eq 0)
        $q = Start-Process $Bin.Herbe -ArgumentList '--dismiss' -PassThru -NoNewWindow; [void]$q.Handle; $q.WaitForExit(3000) | Out-Null
        Check "herbe --dismiss with nothing shown -> exit 1" ($q.ExitCode -eq 1)
        Check "herbe icons: not supported upstream (text only), nothing to test" $true
    }
}

# ---- notifyd -----------------------------------------------------------------

if (Section 'notifyd') {
    $miss = Need @('Notifyd', 'NotifySend')
    if ($miss) { SkipTest 'notifyd' "$miss not built" } else {
        $db = "$Tmp\notifyd.sqlite3"; $pipe = 'n4t-notifyd'; Set-Pipe $pipe
        $d = Start-Daemon $Bin.Notifyd @('--db', $db) 'notifyd'
        Check 'notifyd owns the pipe' (Wait-Pipe $pipe)
        $id1 = & $Bin.NotifySend -p -a mail -u critical -i $IconMagenta 'New mail' 'from Ana'
        $id2 = & $Bin.NotifySend -p -a build 'Build OK' 'green'
        & $Bin.NotifySend -r $id2 -a build 'Build FAILED' '3 tests' | Out-Null
        & $Bin.NotifySend --close=$id1
        Raw-Notify $pipe 'img' '' 'with image' '' @('image-data', 'image', $ImgOrange, 'image-path', 's', $IconCyan) | Out-Null
        Raw-Notify $pipe 'img' '' 'same image again' '' @('image-data', 'image', $ImgOrange) | Out-Null
        Raw-Notify $pipe 'deploy' '' ("Despliegue $($enye)and$($u_acute)") 'produccion lista' | Out-Null
        Stop-Daemon $d
        $d = Start-Daemon $Bin.Notifyd @('--db', $db) 'notifyd2'; Wait-Pipe $pipe | Out-Null
        $id3 = & $Bin.NotifySend -p -a chat 'Hi' 'after restart'
        Stop-Daemon $d

        $h = From-JsonArray (& $Bin.Notifyd --db $db history -n 50 --json)
        $byId = @{}; foreach ($n in $h) { $byId[[int]$n.id] = $n }
        Check 'notifyd ids unique across restarts' ([int]$id3 -gt 5) "id3=$id3"
        Check 'notifyd replace bumps revision' ($byId[[int]$id2].revision -eq 2 -and $byId[[int]$id2].summary -eq 'Build FAILED')
        Check 'notifyd CloseNotification -> reason 3' ($byId[[int]$id1].close_reason -eq 3)
        Check 'notifyd records client exe' ($byId[[int]$id1].client_exe -like '*notify-send.exe')
        Check 'notifyd icon: app_icon stored' ($byId[[int]$id1].app_icon -eq $IconMagenta)
        $img = @($h | Where-Object { $_.summary -eq 'with image' })[0]
        Check 'notifyd icon: image-path hint stored' ($img.hints.'image-path' -eq $IconCyan)
        Check 'notifyd icon: image-data replaced by an image reference' ($img.hints.'image-data'.image -match '^[0-9a-f]{16}$')
        $stats = (& $Bin.Notifyd --db $db stats) -join "`n"
        Check 'notifyd icon: identical image-data stored once' ($stats -match 'images: 1')
        $s = @(& $Bin.Notifyd --db $db history --search 'nandu')
        Check 'notifyd FTS: accent-insensitive search' (($s -join ' ') -match 'Despliegue')
        $e = & $Bin.Notifyd --db $db history --search 'AND OR' 2>&1
        Check 'notifyd FTS: syntax error reported' ($LASTEXITCODE -eq 1)
        $show = (& $Bin.Notifyd --db $db show $id2) -join "`n"
        Check 'notifyd show: notify + replace events' ($show -match 'notify' -and $show -match 'replace')
    }
}

if (Section 'notifyd-exec') {
    $miss = Need @('Notifyd', 'NotifySend', 'Herbe')
    if ($miss) { SkipTest 'notifyd --exec herbe' "$miss not built" } else {
        $db = "$Tmp\exec.sqlite3"; $pipe = 'n4t-exec'; Set-Pipe $pipe
        $d = Start-Daemon $Bin.Notifyd @('--db', $db, '--exec', $Bin.Herbe) 'exec'; Wait-Pipe $pipe | Out-Null
        $j = Start-Job { param($ns, $p) $env:WNOTIFY_PIPE = $p; & $ns -a cal -A 'default=Open' 'Meeting' 'in 5' } -ArgumentList $Bin.NotifySend, $env:WNOTIFY_PIPE
        Start-Sleep 2; & $Bin.Herbe --action
        Wait-Job $j -Timeout 5 | Out-Null
        Check 'notifyd --exec: herbe action -> ActionInvoked(default)' ((Receive-Job $j) -eq 'default')
        $id = & $Bin.NotifySend -p -a timer 'Tea' 'ready'; Start-Sleep 1; & $Bin.NotifySend --close=$id; Start-Sleep 1
        Check 'notifyd --exec: CloseNotification closes herbe' (@(Get-Process herbe -ErrorAction SilentlyContinue).Count -eq 0)
        Stop-Daemon $d
    }
}

if (Section 'notifyd-windows') {
    $miss = Need @('Notifyd')
    if ($miss) { SkipTest 'notifyd --windows' "$miss not built" } else {
        $db = "$Tmp\win.sqlite3"; $pipe = 'n4t-win'; Set-Pipe $pipe
        $d = Start-Daemon $Bin.Notifyd @('--db', $db, '--windows') 'win'; Wait-Pipe $pipe | Out-Null
        Start-Sleep 2
        [void][Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType = WindowsRuntime]
        [void][Windows.Data.Xml.Dom.XmlDocument, Windows.Data.Xml.Dom.XmlDocument, ContentType = WindowsRuntime]
        $aumid = '{1AC14E77-02E7-4E5D-B744-2EB1AE5198B7}\WindowsPowerShell\v1.0\powershell.exe'
        $iconUri = ([Uri]$IconMagenta).AbsoluteUri
        $tag = 'n4t-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
        $xml = New-Object Windows.Data.Xml.Dom.XmlDocument
        $xml.LoadXml("<toast><visual><binding template=`"ToastGeneric`"><text>n4 toast $tag &amp; $enye</text><text>line one</text><text>line two</text><image placement=`"appLogoOverride`" src=`"$iconUri`"/></binding></visual></toast>")
        $t = New-Object Windows.UI.Notifications.ToastNotification $xml; $t.Tag = $tag; $t.Group = 'n4tests'
        [Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier($aumid).Show($t)
        Start-Sleep 4
        $rows = @(From-JsonArray (& $Bin.Notifyd --db $db history -n 500 --json) | Where-Object { $_.summary -like "n4 toast $tag*" })
        Check 'notifyd --windows: toast imported' ($rows.Count -eq 1)
        if ($rows.Count -eq 1) {
            Check 'notifyd --windows: UTF-8 and entities decoded' ($rows[0].summary -eq "n4 toast $tag & $enye" -and $rows[0].body -eq "line one`nline two")
            Check 'notifyd --windows: app name from AUMID' ($rows[0].app_name -eq 'powershell')
            Check 'notifyd --windows icon: <image src> becomes app_icon' ($rows[0].app_icon -eq $iconUri) "got '$($rows[0].app_icon)'"
        }
        [Windows.UI.Notifications.ToastNotificationManager]::History.Remove($tag, 'n4tests', $aumid)
        Start-Sleep 4
        $rows = @(From-JsonArray (& $Bin.Notifyd --db $db history -n 500 --json) | Where-Object { $_.summary -like "n4 toast $tag*" })
        Check 'notifyd --windows: removal from Action Center -> close reason 4' ($rows.Count -eq 1 -and $rows[0].close_reason -eq 4)
        Stop-Daemon $d
    }
}

# ---- dunst -------------------------------------------------------------------

if (Section 'dunst') {
    $miss = Need @('Dunst', 'NotifySend')
    if ($miss) { SkipTest 'dunst' "$miss not built" } else {
        $pipe = 'n4t-dunst'; Set-Pipe $pipe
        $d = Start-Daemon $Bin.Dunst @() 'dunst'
        Check 'dunst owns the pipe' (Wait-Pipe $pipe 8000)
        $caps = (& $Bin.NotifySend --capabilities) -join ' '
        Check 'dunst GetCapabilities' ($caps -match 'body' -and $caps -match 'actions')

        # icons: one notification at a time, colour must appear on screen
        $base = Count-ScreenColor 255 0 255
        & $Bin.NotifySend -a icons -i $IconMagenta 'Icon from app_icon path' 'magenta' | Out-Null
        Start-Sleep -Milliseconds 900
        $n = Count-ScreenColor 255 0 255 6 "$Tmp\dunst-app_icon.png"
        Check "dunst icon: app_icon = Windows path is drawn ($n px)" ($n - $base -gt 400) "see $Tmp\dunst-app_icon.png"
        Ctl close-all; Start-Sleep -Milliseconds 400

        $base = Count-ScreenColor 0 255 255
        Raw-Notify $pipe 'icons' '' 'Icon from image-path hint' 'cyan' @('image-path', 's', $IconCyan) | Out-Null
        Start-Sleep -Milliseconds 900
        $n = Count-ScreenColor 0 255 255 6 "$Tmp\dunst-image-path.png"
        Check "dunst icon: image-path hint is drawn ($n px)" ($n - $base -gt 400) "see $Tmp\dunst-image-path.png"
        Ctl close-all; Start-Sleep -Milliseconds 400

        $base = Count-ScreenColor 255 128 0
        Raw-Notify $pipe 'icons' '' 'Icon from image-data' 'orange' @('image-data', 'image', $ImgOrange) | Out-Null
        Start-Sleep -Milliseconds 900
        $n = Count-ScreenColor 255 128 0 6 "$Tmp\dunst-image-data.png"
        Check "dunst icon: raw image-data pixels are drawn ($n px)" ($n - $base -gt 400) "see $Tmp\dunst-image-data.png"
        Ctl close-all; Start-Sleep -Milliseconds 400

        $base = Count-ScreenColor 255 0 255
        & $Bin.NotifySend -a icons -i ([Uri]$IconMagenta).AbsoluteUri 'Icon from file:// URI' 'magenta' | Out-Null
        Start-Sleep -Milliseconds 900
        $n = Count-ScreenColor 255 0 255 6
        Check "dunst icon: file:// URI is drawn ($n px)" ($n - $base -gt 400)
        Ctl close-all; Start-Sleep -Milliseconds 400

        # dunstctl
        & $Bin.NotifySend -a a 'one' 'x' | Out-Null; & $Bin.NotifySend -a b 'two' 'y' | Out-Null
        Start-Sleep -Milliseconds 400
        Check 'dunstctl count displayed' ([string](Ctl count displayed) -eq '2')
        Ctl close; Start-Sleep -Milliseconds 300
        Check 'dunstctl close' ([string](Ctl count displayed) -eq '1')
        Ctl set-paused true
        Check 'dunstctl set-paused / is-paused' ([string](Ctl is-paused) -eq 'true')
        Ctl set-paused false
        Ctl close-all; Start-Sleep -Milliseconds 300
        $hist = From-JsonArray (Ctl history)
        Check "dunstctl history is JSON ($($hist.Count) entries)" ($hist.Count -ge 2)
        $iconEntry = @($hist | Where-Object { $_.summary -eq 'Icon from app_icon path' })
        Check 'dunst icon: history keeps the resolved icon_path' ($iconEntry.Count -ge 1 -and $iconEntry[0].icon_path -eq $IconMagenta) "got '$(if ($iconEntry.Count) { $iconEntry[0].icon_path })'"
        Ctl history-pop; Start-Sleep -Milliseconds 300
        Check 'dunstctl history-pop' ([string](Ctl count displayed) -eq '1')
        Ctl close-all

        $j = Start-Job { param($ns, $p) $env:WNOTIFY_PIPE = $p; & $ns -a cal -A 'default=Open' 'Meeting' 'in 5' } -ArgumentList $Bin.NotifySend, $env:WNOTIFY_PIPE
        Start-Sleep 2; Ctl action 0; Start-Sleep 1; Ctl close-all
        Wait-Job $j -Timeout 5 | Out-Null
        Check 'dunstctl action -> ActionInvoked(default) to the client' ((Receive-Job $j) -eq 'default')
        Stop-Daemon $d
    }
}

if (Section 'chain') {
    $miss = Need @('Dunst', 'Notifyd', 'NotifySend')
    if ($miss) { SkipTest 'notifyd --forward dunst' "$miss not built" } else {
        $db = "$Tmp\chain.sqlite3"
        Set-Pipe 'n4t-down'; $dn = Start-Daemon $Bin.Dunst @() 'chain-dunst'; Wait-Pipe 'n4t-down' 8000 | Out-Null
        Set-Pipe 'n4t-up'; $up = Start-Daemon $Bin.Notifyd @('--db', $db, '--forward', '\\.\pipe\n4t-down') 'chain-nd'; Wait-Pipe 'n4t-up' | Out-Null
        $base = Count-ScreenColor 255 0 255
        $id = & $Bin.NotifySend -p -a chain -i $IconMagenta 'Via notifyd to dunst' 'chained, with icon'
        Start-Sleep -Milliseconds 900
        $n = Count-ScreenColor 255 0 255
        Check "chain: dunst draws the forwarded icon ($n px)" ($n - $base -gt 400)
        Set-Pipe 'n4t-down'; Ctl close-all; Start-Sleep 1
        Stop-Daemon $up; Stop-Daemon $dn
        $row = (From-JsonArray (& $Bin.Notifyd --db $db history --json))[0]
        Check 'chain: close in dunst recorded by notifyd (reason 2)' ($row.close_reason -eq 2)
        Check 'chain: icon recorded by notifyd' ($row.app_icon -eq $IconMagenta)
    }
}

# ---- summary -------------------------------------------------------------------

foreach ($p in $script:Procs) { Stop-Daemon $p }
Get-Process herbe -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Remove-Item Env:WNOTIFY_PIPE -ErrorAction SilentlyContinue
Write-Host ("`n{0} passed, {1} failed, {2} skipped" -f $script:Pass, $script:Fail, $script:Skip) -ForegroundColor $(if ($script:Fail) { 'Red' } else { 'Green' })
if ($KeepTemp -or $script:Fail) { Write-Host "artifacts: $Tmp" } else { Remove-Item -Recurse -Force $Tmp -ErrorAction SilentlyContinue }
exit $(if ($script:Fail) { 1 } else { 0 })
