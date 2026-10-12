// basename(1) — el último componente de una ruta, con sufijo opcional quitado.
//
// Port de openbsd-src/usr.bin/basename/basename.c.
//
// Punto clave del porteo multiplataforma: basename(1) opera sobre TEXTO, no
// sobre el sistema de archivos. No toca el disco ni comprueba que la ruta
// exista. Por eso se implementa el algoritmo de POSIX a mano en vez de usar
// core:path/filepath: filepath.base() sigue las convenciones de la plataforma
// anfitriona y NO aplica las reglas de barras finales de POSIX. Medido:
// filepath.base("/a/b/") devuelve "", cuando basename(1) debe devolver "b".
//
// Reglas de POSIX, en orden:
//   1. si la cadena es "//"            -> "//"
//   2. si es todo barras               -> "/"
//   3. sacar las barras finales
//   4. si no queda ninguna barra       -> toda la cadena
//   5. devolver lo que sigue a la última barra

package main

import "core:fmt"
import "core:os"

main :: proc() {
	if len(os.args) < 2 {
		fmt.eprintln("usage: basename string [suffix]")
		os.exit(1)
	}

	base := posix_basename(os.args[1])

	// El sufijo sólo se quita si no deja la cadena vacía: `basename .txt .txt`
	// debe seguir dando ".txt". Es una regla de POSIX y el origen de los casos
	// borde de basename(1).
	if len(os.args) > 2 {
		suffix := os.args[2]
		if len(suffix) > 0 && len(suffix) < len(base) &&
		   base[len(base) - len(suffix):] == suffix {
			base = base[:len(base) - len(suffix)]
		}
	}

	fmt.println(base)
}

posix_basename :: proc(path: string) -> string {
	// Regla 1: "//" es un caso especial histórico y se preserva tal cual.
	if path == "//" {
		return "//"
	}

	// Reglas 2 y 3. `rest` es una copia local porque en Odin un parámetro no
	// es asignable.
	rest := path
	end := len(rest)
	for end > 0 && rest[end - 1] == '/' {
		end -= 1
	}
	if end == 0 {
		return "/"
	}
	rest = rest[:end]

	// Regla 5: lo que sigue a la última barra (o toda la cadena si no hay).
	start := len(rest)
	for start > 0 && rest[start - 1] != '/' {
		start -= 1
	}
	return rest[start:]
}
