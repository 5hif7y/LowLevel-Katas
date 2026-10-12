// yes(1) — repite una línea para siempre.
//
// Port de openbsd-src/usr.bin/yes/yes.c.
//
// Este es el port con la lección más interesante del kata, documentada entera en
// Docs/porter-guide/10-estudio-de-casos-de-depuracion.md. El original en C
// descarta el valor de retorno de puts() y confía en que SIGPIPE lo mate cuando
// el consumidor cierra el pipe. Windows no tiene SIGPIPE, así que en el port a C
// el bucle se volvía un giro infinito a CPU completa (hubo que emular SIGPIPE en
// compat/src/sigpipe.c).
//
// En Odin el problema NO existe por diseño: os.write devuelve un Errno y no hay
// señal que mate al proceso por nosotros. Chequear la escritura no es una
// precaución defensiva, es la única forma de que el bucle termine.
// (Odin tampoco tiene SIGPIPE: os.write sobre un pipe cerrado devuelve EPIPE.)

package main

import "core:os"

main :: proc() {
	line := "y"
	if len(os.args) > 1 {
		line = os.args[1]
	}

	// La línea se arma una sola vez fuera del bucle: eso es lo que hace que
	// yes(1) sea un generador barato en vez de un formateador por iteración.
	buf := make([]byte, len(line) + 1)
	defer delete(buf)
	copy(buf, line)
	buf[len(line)] = '\n'

	for {
		// Si el consumidor se fue (`yes | head -n 1`) la escritura falla con
		// EPIPE y salimos. Sin esto el proceso quedaría girando para siempre.
		if _, err := os.write(os.stdout, buf); err != os.ERROR_NONE {
			os.exit(1)
		}
	}
}
