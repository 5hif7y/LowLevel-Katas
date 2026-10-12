// uniq(1) — colapsa líneas adyacentes repetidas.
//
// Port de openbsd-src/usr.bin/uniq/uniq.c.
//
// ADYACENTES es la palabra que importa: uniq(1) NO deduplica el archivo, sólo
// colapsa repeticiones contiguas. `a b a` sale tal cual. Para deduplicar de
// verdad hay que ordenar antes — de ahí el idiom `sort | uniq`.
//
// Opciones:
//   -c   prefija cada línea con el número de repeticiones
//   -d   emite sólo las líneas que se repiten
//   -u   emite sólo las que NO se repiten
//
// Detalle del original: uniq(1) usa freopen(3) para leer de un archivo y
// escribir en otro. En Windows freopen tuvo que entrar en el lote de funciones
// forzadas a binario (compat/src/binary_mode.c), porque si no traducía \n a \r\n
// al escribir el archivo de salida.

package main

import "core:fmt"
import "core:os"

main :: proc() {
	os.exit(run())
}

run :: proc() -> int {
	count, only_dup, only_uniq: bool
	positional := make([dynamic]string)
	defer delete(positional)

	for arg in os.args[1:] {
		switch arg {
		case "-c":
			count = true
		case "-d":
			only_dup = true
		case "-u":
			only_uniq = true
		case:
			append(&positional, arg)
		}
	}

	input, output := "", ""
	if len(positional) > 0 {
		input = positional[0]
	}
	if len(positional) > 1 {
		output = positional[1]
	}

	// -d y -u juntos se cancelan: ninguna línea puede ser a la vez repetida y
	// única, así que no hay nada que emitir.
	if only_dup && only_uniq {
		return 0
	}

	data: []byte
	err: os.Error
	if len(input) > 0 && input != "-" {
		data, err = os.read_entire_file(input, context.allocator)
	} else {
		data, err = os.read_entire_file(os.stdin, context.allocator)
	}
	if err != nil {
		fmt.eprintf("uniq: %v\n", err)
		return 1
	}
	defer delete(data)

	sink := os.stdout
	out_file: ^os.File = nil
	if len(output) > 0 && output != "-" {
		f, oerr := os.open(output, {.Write, .Create, .Trunc})
		if oerr != nil {
			fmt.eprintf("uniq: %s: %v\n", output, oerr)
			return 1
		}
		out_file = f
		sink = f
	}

	// Recorrido por tramos contiguos iguales, avanzando sobre el buffer.
	// El avance es por `pos < len(data)`: iterar con `<=` reprocesa la posición
	// final y emite una línea vacía de más cuando el archivo termina en '\n'.
	pos := 0
	for pos < len(data) {
		line, next := next_line(data, pos)

		// Contar cuántas líneas idénticas siguen.
		n := 1
		for next < len(data) {
			other, other_next := next_line(data, next)
			if !bytes_equal(other, line) {
				break
			}
			n += 1
			next = other_next
		}

		emit := true
		if only_dup {
			emit = n > 1
		} else if only_uniq {
			emit = n == 1
		}

		if emit {
			if count {
				// Espacios, no ceros: el %7d de Odin rellena con ceros e
				// imprimiría "0000002" donde uniq(1) imprime "      2".
				write_padded(sink, n)
				os.write(sink, []byte{' '})
			}
			os.write(sink, line)
			os.write(sink, []byte{'\n'})
		}

		pos = next
	}

	if out_file != nil {
		os.close(out_file)
	}
	return 0
}

// Alinea el contador a 7 columnas con espacios, como el %7lld del original.
write_padded :: proc(sink: ^os.File, v: int) {
	s := fmt.tprintf("%d", v)
	if len(s) < 7 {
		for _ in 0 ..< 7 - len(s) {
			os.write(sink, []byte{' '})
		}
	}
	os.write(sink, transmute([]byte)s)
}

// Devuelve la línea que arranca en `pos` y el offset de la siguiente.
next_line :: proc(data: []byte, pos: int) -> (line: []byte, next: int) {
	i := pos
	for i < len(data) && data[i] != '\n' {
		i += 1
	}
	return data[pos:i], i + 1
}

bytes_equal :: proc(a, b: []byte) -> bool {
	if len(a) != len(b) {
		return false
	}
	for i in 0 ..< len(a) {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}
