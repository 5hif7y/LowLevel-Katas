// true(1) — no hace nada y sale con éxito.
// Port de openbsd-src/usr.bin/true/true.c.
//
// El contrato importa más que el código: true(1) debe aceptar CUALQUIER
// argumento e ignorarlo, incluidos los que parecen flags. Un `true --help` que
// imprima ayuda rompería scripts.
int main() {
    return 0;
}
