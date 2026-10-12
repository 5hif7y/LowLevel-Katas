// head(1) — las primeras líneas de cada archivo.
//
// Port de openbsd-src/usr.bin/head/head.c.
//
// Detalle portable que suele romperse: cuando hay MÁS DE UN archivo, head(1)
// imprime un encabezado `==> nombre <==` antes de cada uno; con uno solo, no.
// Es la clase de regla que no se deduce de la firma y hay que copiarla del
// original.
//
// El original usa getline(3), que en Windows no existe y hubo que reimplementar
// (compat/src/getline.c). Acá se recorre el buffer buscando '\n'.

package main

import "core:fmt"
import "core:os"

DEFAULT_LINES :: 10

main :: proc() {
	os.exit(run())
}

run :: proc() -> int {
	n := DEFAULT_LINES
	files := make([dynamic]string)
	defer delete(files)

	args := os.args[1:]
	i := 0
	for i < len(args) {
		arg := args[i]
		switch {
		case arg == "-n":
			// Acepta `-n 5`.
			if i + 1 < len(args) {
				i += 1
				if v, ok := parse_int(args[i]); ok {
					n = v
				}
			}
		case len(arg) > 2 && arg[:2] == "-n":
			// Acepta `-n5`.
			if v, ok := parse_int(arg[2:]); ok {
				n = v
			}
		case:
			append(&files, arg)
		}
		i += 1
	}

	if n < 0 {
		n = 0
	}

	if len(files) == 0 {
		return head_stream(os.stdin, n)
	}

	status := 0
	for name, idx in files {
		if len(files) > 1 {
			if idx > 0 {
				fmt.println()
			}
			fmt.printf("==> %s <==\n", name)
		}

		data, err := os.read_entire_file(name, context.allocator)
		if err != nil {
			fmt.eprintf("head: %s: %v\n", name, err)
			status = 1
			continue
		}
		print_first_lines(data, n)
		delete(data)
	}
	return status
}

head_stream :: proc(f: ^os.File, n: int) -> int {
	data, err := os.read_entire_file(f, context.allocator)
	if err != nil {
		return 1
	}
	defer delete(data)

	print_first_lines(data, n)
	return 0
}

// Emite los primeros n renglones CONSERVANDO el '\n' de cada uno. Un archivo sin
// salto final emite su última línea sin agregarlo: head(1) no inventa bytes.
print_first_lines :: proc(data: []byte, n: int) {
	seen := 0
	start := 0
	for i in 0 ..< len(data) {
		if data[i] == '\n' {
			// El corte va ANTES de escribir: al encontrar el (n+1)-ésimo salto
			// ya se emitieron los n renglones pedidos y no hay que escribir nada
			// más. Escribir y después cortar agrega una línea de más.
			if seen == n {
				return
			}
			seen += 1
			os.write(os.stdout, data[start:i + 1])
			start = i + 1
		}
	}
	if seen < n && start < len(data) {
		os.write(os.stdout, data[start:])
	}
}

parse_int :: proc(s: string) -> (int, bool) {
	if len(s) == 0 {
		return 0, false
	}
	v := 0
	for c in s {
		if c < '0' || c > '9' {
			return 0, false
		}
		v = v * 10 + int(c - '0')
	}
	return v, true
}
