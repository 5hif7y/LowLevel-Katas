// tee(1) — copia la entrada a la salida y además a archivos.
//
// Port de openbsd-src/usr.bin/tee/tee.c.
//
// Este es el otro port que delata el modo texto. En el port a C, tee leía 8
// bytes del pipe y escribía 9 en el archivo: el CRT traducía cada \n a \r\n al
// escribir. La medición está en tests/run-suite.sh, sección 4:
//
//	$ printf 'bin\000ary\n' | tee.exe out.bin ; wc -c < out.bin
//	9        <- cuando debería decir 8
//
// En Odin no hay traducción de ningún tipo, así que el round-trip es byte-exacto
// por construcción y este port no necesita la maquinaria de binary_mode.c.
//
// Detalle del original que se preserva: tee(1) ignora SIGINT mientras escribe,
// para no dejar archivos a medio hacer si el usuario corta la tubería. Ese
// detalle no se puede portar tal cual a Odin —no hay SIGINT— y es una de las
// asimetrías reales entre las dos plataformas.

package main

import "core:fmt"
import "core:os"

main :: proc() {
	os.exit(run())
}

run :: proc() -> int {
	append_mode := false
	files := make([dynamic]string)
	defer delete(files)

	for arg in os.args[1:] {
		switch arg {
		case "-a":
			append_mode = true
		case:
			append(&files, arg)
		}
	}

	data, err := os.read_entire_file(os.stdin, context.allocator)
	if err != nil {
		fmt.eprintf("tee: read error: %v\n", err)
		return 1
	}
	defer delete(data)

	// Orden del original: primero los archivos, después stdout. Si un archivo no
	// se puede abrir, tee(1) avisa y SIGUE con el resto; no aborta.
	status := 0
	for name in files {
		flags: os.File_Flags = {.Write, .Create}
		if append_mode {
			flags += {.Append}
		} else {
			flags += {.Trunc}
		}

		f, oerr := os.open(name, flags)
		if oerr != nil {
			fmt.eprintf("tee: %s: %v\n", name, oerr)
			status = 1
			continue
		}

		if _, werr := os.write(f, data); werr != nil {
			fmt.eprintf("tee: %s: %v\n", name, werr)
			status = 1
		}
		os.close(f)
	}

	// Si el consumidor se fue, esto falla y salimos en vez de girar.
	if _, werr := os.write(os.stdout, data); werr != nil {
		return 1
	}
	return status
}
