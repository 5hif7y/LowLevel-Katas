// false(1) — no hace nada y sale con error.
//
// Port de openbsd-src/usr.bin/false/false.c. Igual que true(1) pero devolviendo
// 1. El código de salida 1 (y no otro) importa: es lo que esperan `if !cmd`,
// `set -e` y las cadenas `&&` / `||` del shell.

package main

import "core:os"

main :: proc() {
	os.exit(1)
}
