// dirname(1) — todo menos el último componente de una ruta.
//
// Port de openbsd-src/usr.bin/dirname/dirname.c.
//
// Igual que basename(1), es una operación sobre TEXTO: no toca el disco. Por eso
// se implementa el algoritmo de POSIX a mano en vez de delegar en
// core:path/filepath, que sigue las convenciones de la plataforma anfitriona y
// en Windows trataría la barra invertida como separador. El original de OpenBSD
// sólo conoce '/', y un dirname que de pronto entienda '\' dejaría de ser el
// mismo programa.
//
// Reglas de POSIX, en orden:
//   1. si la cadena es "//"            -> "//"
//   2. si es todo barras               -> "/"
//   3. sacar las barras finales
//   4. si no queda ninguna barra       -> "."
//   5. sacar el último componente
//   6. sacar las barras finales que queden
//   7. devolver lo que quedó

package main

import "core:fmt"
import "core:os"

main :: proc() {
	if len(os.args) < 2 {
		fmt.eprintln("usage: dirname string")
		os.exit(1)
	}
	fmt.println(dirname(os.args[1]))
}

dirname :: proc(path: string) -> string {
	// Regla 1: "//" es un caso especial histórico y se preserva tal cual.
	if path == "//" {
		return "//"
	}

	// Reglas 2 y 3: sin barras finales.
	end := len(path)
	for end > 0 && path[end - 1] == '/' {
		end -= 1
	}

	// Todo barras (o cadena vacía): la raíz.
	if end == 0 {
		return "/"
	}

	// Regla 5: sacar el último componente.
	for end > 0 && path[end - 1] != '/' {
		end -= 1
	}

	// Regla 4: no había ninguna barra, así que no hay directorio.
	if end == 0 {
		return "."
	}

	// Regla 6: sacar las barras finales que quedaron.
	for end > 1 && path[end - 1] == '/' {
		end -= 1
	}

	return path[:end]
}
