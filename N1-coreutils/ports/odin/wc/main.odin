// wc(1) — cuenta líneas, palabras y bytes.
//
// Port de openbsd-src/usr.bin/wc/wc.c.
//
// Este es el port que delata los errores de modo binario. En el port a C,
// `cat crlf.txt | wc -c` devolvía 4 sobre un archivo de 6 bytes, porque cat ya
// había mutilado los \r al leerlo; wc sólo contaba lo que le llegaba.
//
// Acá el conteo es sobre bytes crudos y no hay nada que traducir, así que el
// número coincide siempre con el tamaño real. Que sea trivial es el punto: la
// dificultad del port a C no estaba en wc sino en la capa de abajo.

package main

import "core:fmt"
import "core:os"

main :: proc() {
	os.exit(run())
}

run :: proc() -> int {
	show_lines, show_words, show_bytes: bool
	show_all := true

	files := make([dynamic]string)
	defer delete(files)

	for arg in os.args[1:] {
		switch arg {
		case "-l":
			show_lines, show_all = true, false
		case "-w":
			show_words, show_all = true, false
		case "-c":
			show_bytes, show_all = true, false
		case:
			append(&files, arg)
		}
	}

	if show_all {
		show_lines, show_words, show_bytes = true, true, true
	}

	if len(files) == 0 {
		data, err := os.read_entire_file(os.stdin, context.allocator)
		if err != nil {
			return 1
		}
		defer delete(data)
		print_counts(data, show_lines, show_words, show_bytes, "")
		return 0
	}

	status := 0
	for name in files {
		data, err := os.read_entire_file(name, context.allocator)
		if err != nil {
			fmt.eprintf("wc: %s: %v\n", name, err)
			status = 1
			continue
		}
		print_counts(data, show_lines, show_words, show_bytes, name)
		delete(data)
	}
	return status
}

print_counts :: proc(data: []byte, lines, words, bytes: bool, name: string) {
	if lines {
		print_field(count_lines(data))
	}
	if words {
		print_field(count_words(data))
	}
	if bytes {
		print_field(len(data))
	}
	if len(name) > 0 {
		fmt.printf(" %s", name)
	}
	fmt.println()
}

// wc(1) alinea los números a 7 columnas rellenando con ESPACIOS. El %7d de Odin
// rellena con ceros (imprime "0000003" en vez de "      3"), así que el relleno
// se arma a mano.
print_field :: proc(v: int) {
	s := fmt.tprintf("%d", v)
	if len(s) < 7 {
		for _ in 0 ..< 7 - len(s) {
			fmt.print(" ")
		}
	}
	fmt.print(s)
}

// Una «línea» es un '\n'. Un archivo sin salto final no cuenta una línea extra,
// que es el comportamiento de wc(1) y no el de un split() ingenuo.
count_lines :: proc(data: []byte) -> int {
	n := 0
	for b in data {
		if b == '\n' {
			n += 1
		}
	}
	return n
}

// Una «palabra» es una tirada de bytes que no sean espacio, tab, \n, \r, \v ni
// \f — la definición de isspace(3) que usa el original. Se calcula en una pasada
// en vez de partir el buffer, que para archivos grandes importa.
count_words :: proc(data: []byte) -> int {
	n := 0
	in_word := false
	for b in data {
		is_space := b == ' ' || b == '\t' || b == '\n' || b == '\r' || b == '\v' || b == '\f'
		if is_space {
			in_word = false
		} else if !in_word {
			in_word = true
			n += 1
		}
	}
	return n
}
