// false(1) — no hace nada y sale con error.
// Port de openbsd-src/usr.bin/false/false.c.
//
// El código 1 (y no otro) importa: es lo que esperan `if !cmd`, `set -e` y las
// cadenas `&&` / `||` del shell.
int main() {
    return 1;
}
