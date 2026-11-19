#!/bin/sh
# Rebuild upstream/ pinned to the commits in UPSTREAM.md.
set -e
cd "$(dirname "$0")"
mkdir -p upstream
fetch() { # name url sha
	if [ ! -d "upstream/$1/.git" ]; then git clone -q "$2" "upstream/$1"; fi
	git -C "upstream/$1" fetch -q origin "$3" 2>/dev/null || git -C "upstream/$1" fetch -q --unshallow origin || true
	git -C "upstream/$1" checkout -q "$3"
	echo "$1 @ $3"
}
fetch dtach       https://github.com/crigler/dtach         b027c27b2439081064d07a86883c8e0b20a183c9
fetch abduco      https://github.com/martanne/abduco       8c32909a159aaa9484c82b71f05b7a73321eb491
fetch dvtm        https://github.com/martanne/dvtm         7bcf43f8dbd5c4a67ec573a1248114caa75fa3c2
fetch PDCursesMod https://github.com/Bill-Gray/PDCursesMod 8df2949d368f21e290a25280bdd5f826f6c9f648
