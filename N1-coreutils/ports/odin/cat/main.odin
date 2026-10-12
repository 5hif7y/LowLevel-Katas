// cat(1) — concatena archivos a la salida estándar.
//
// Port de openbsd-src/bin/cat/cat.c.
//
// Lo que hace interesante este port es la E/S, no la lógica. Ver
// Docs/porter-guide/10-estudio-de-casos-de-depuracion.md: en el port a C, cat
// abría los archivos con open(2) en modo texto y el CRT se comía los \r, así que
// `cat imagen.png > copia.png` no reproducía el original. Hubo que forzar
// _O_BINARY en compat/src/binary_mode.c.
//
// En Odin el problema no existe: os.read_entire_file devuelve bytes crudos y no
// hay ninguna capa que traduzca \r\n. Es exactamente la semántica de Unix, y es
// la razón por la que este port no necesita nada parecido a la capa compat.
//
// Simplificación consciente: lee el archivo entero en memoria en vez de
// streamearlo. Un cat(1) de verdad usa un buffer de 64 KB (ver raw_cat() en el
// original); para un kata alcanza y el README lo anota como ejercicio.

package main

import "core:fmt"
import "core:os"

main :: proc() {
	// La lógica vive en run() y no en main() porque os.exit diverge: cualquier
	// `defer` declarado en el mismo ámbito que un os.exit queda inalcanzable y
	// Odin lo rechaza en compilación.
	os.exit(run())
}

run :: proc() -> int {
	if len(os.args) < 2 {
		return dump(os.stdin)
	}

	status := 0
	for name in os.args[1:] {
		// "-" significa la entrada estándar, igual que en el original.
		if name == "-" {
			if dump(os.stdin) != 0 {
				status = 1
			}
			continue
		}
		if !dump_file(name) {
			status = 1
		}
	}
	return status
}

dump_file :: proc(name: string) -> bool {
	data, err := os.read_entire_file(name, context.allocator)
	if err != nil {
		fmt.eprintf("cat: %s: %v\n", name, err)
		return false
	}
	defer delete(data)

	_, werr := os.write(os.stdout, data)
	return werr == nil
}

dump :: proc(f: ^os.File) -> int {
	data, err := os.read_entire_file(f, context.allocator)
	if err != nil {
		return 1
	}
	defer delete(data)

	// Si el consumidor cerró el pipe, os.write falla y salimos. En Odin no hay
	// SIGPIPE que mate al proceso por nosotros.
	if _, werr := os.write(os.stdout, data); werr != nil {
		return 1
	}
	return 0
}
