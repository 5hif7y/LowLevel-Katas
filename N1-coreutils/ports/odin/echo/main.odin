// echo(1) — imprime sus argumentos separados por un espacio.
//
// Port de openbsd-src/bin/echo/echo.c.
//
// Nota de fidelidad: el original de OpenBSD interpreta secuencias de escape con
// barra invertida (\c corta la salida, \n, \t, ...) y reconoce -n sólo como
// PRIMER argumento. Acá se implementa el contrato central —separar con espacios
// y terminar en salto de línea, con -n para suprimirlo— y las secuencias quedan
// como ejercicio; el README de ports/ lo anota.
//
// El detalle portable: `strings.join` evita el clásico error de imprimir un
// espacio detrás del último argumento.

package main

import "core:fmt"
import "core:os"
import "core:strings"

main :: proc() {
	args := os.args[1:]
	newline := true

	// -n sólo vale como primer argumento; después se trata como texto. Es lo que
	// permite `echo -n -n` sin sorpresas.
	if len(args) > 0 && args[0] == "-n" {
		newline = false
		args = args[1:]
	}

	out := strings.join(args, " ")
	defer delete(out)

	if newline {
		fmt.println(out)
	} else {
		fmt.print(out)
	}
}
