// true(1) — no hace nada y sale con éxito.
//
// Port de openbsd-src/usr.bin/true/true.c. El original es literalmente un main()
// vacío que devuelve 0; acá el valor de retorno es implícito porque un proc de
// Odin sin `return` devuelve el cero del tipo, y main no devuelve nada.
//
// El interés de portear esto no está en el código sino en el contrato: true(1)
// debe aceptar CUALQUIER argumento e ignorarlo, incluidos los que parecen flags.
// Un `true --help` que imprima ayuda rompería scripts.

package main

import "core:os"

main :: proc() {
	os.exit(0)
}
