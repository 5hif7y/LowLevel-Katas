# Rebuild upstream\ pinned to the commits in UPSTREAM.md.
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force upstream | Out-Null
$pins = @(
    @("dtach",       "https://github.com/crigler/dtach",         "b027c27b2439081064d07a86883c8e0b20a183c9"),
    @("abduco",      "https://github.com/martanne/abduco",       "8c32909a159aaa9484c82b71f05b7a73321eb491"),
    @("dvtm",        "https://github.com/martanne/dvtm",         "7bcf43f8dbd5c4a67ec573a1248114caa75fa3c2"),
    @("PDCursesMod", "https://github.com/Bill-Gray/PDCursesMod", "8df2949d368f21e290a25280bdd5f826f6c9f648")
)
foreach ($p in $pins) {
    $dir = "upstream\$($p[0])"
    if (-not (Test-Path "$dir\.git")) { git clone -q $p[1] $dir }
    git -C $dir fetch -q origin $p[2]
    git -C $dir checkout -q $p[2]
    Write-Host "$($p[0]) @ $($p[2])"
}
