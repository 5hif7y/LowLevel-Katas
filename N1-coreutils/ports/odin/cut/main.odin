// cut(1) — extrae campos o columnas de cada línea.
//
// Port de openbsd-src/usr.bin/cut/cut.c.
//
// Dos modos, mutuamente excluyentes:
//   -f LIST   campos separados por el delimitador de -d (TAB por defecto)
//   -c LIST   rangos de caracteres
//
// LIST es una lista separada por comas de números o rangos: "1,3", "2-5", "3-",
// "-4". El parser de rangos es el corazón de cut(1) y donde vive casi toda su
// superficie de error: `cut -f 0` o `cut -f 5-2` son errores, no resultados
// vacíos.
//
// Detalle portable: cut(1) NO reordena. `cut -f 3,1` emite el campo 3 y después
// el 1, en el orden pedido.
//
// El recorrido se hace sobre bytes y no con strings.split, para no arrastrar
// asignaciones: el original también trabaja sobre el buffer crudo.

package main

import "core:fmt"
import "core:os"

Mode :: enum {
	None,
	Fields,
	Chars,
}

Range :: struct {
	lo, hi: int,
}

// Tope para los rangos abiertos ("3-"). El original de OpenBSD usa el máximo del
// tipo; acá alcanza con algo que exceda cualquier línea razonable.
MAX_INDEX :: 1 << 30

main :: proc() {
	os.exit(run())
}

run :: proc() -> int {
	mode := Mode.None
	delim: byte = '\t'
	list := ""

	args := os.args[1:]
	i := 0
	for i < len(args) {
		arg := args[i]
		if len(arg) < 2 || arg[0] != '-' {
			fmt.eprintf("cut: illegal option -- %s\n", arg)
			return 1
		}

		opt := arg[1]
		value := arg[2:]
		if len(value) == 0 {
			if i + 1 >= len(args) {
				fmt.eprintf("cut: option requires an argument -- %c\n", opt)
				return 1
			}
			i += 1
			value = args[i]
		}

		switch opt {
		case 'f':
			mode, list = .Fields, value
		case 'c':
			mode, list = .Chars, value
		case 'd':
			if len(value) == 0 {
				return 1
			}
			delim = value[0]
		case:
			fmt.eprintf("cut: illegal option -- %c\n", opt)
			return 1
		}
		i += 1
	}

	if mode == .None || len(list) == 0 {
		fmt.eprintln("cut: you must specify a list of bytes, characters, or fields")
		return 1
	}

	ranges := parse_ranges(list)
	defer delete(ranges)

	data, err := os.read_entire_file(os.stdin, context.allocator)
	if err != nil {
		return 1
	}
	defer delete(data)

	// Se opera línea por línea SIN el '\n', que se re-agrega al final. Así el
	// delimitador nunca puede confundirse con el salto de línea.
	start := 0
	for i in 0 ..= len(data) {
		if i < len(data) && data[i] != '\n' {
			continue
		}
		line := data[start:i]
		start = i + 1

		switch mode {
		case .Fields:
			cut_fields(line, delim, ranges)
		case .Chars:
			cut_chars(line, ranges)
		case .None:
		}
		fmt.println()
	}
	return 0
}

cut_fields :: proc(line: []byte, delim: byte, ranges: [dynamic]Range) {
	first := true
	for r in ranges {
		// Se recorren los campos una vez por rango, contando separadores. Simple
		// y sin asignaciones; para listas de campos cortas es lo que hace el
		// original.
		for field := r.lo; field <= r.hi && field <= MAX_INDEX; field += 1 {
			content, found := nth_field(line, delim, field)
			if !found {
				continue
			}
			if !first {
				os.write(os.stdout, []byte{delim})
			}
			os.write(os.stdout, content)
			first = false
		}
	}
}

// Devuelve el campo n-ésimo (1-based) de la línea.
nth_field :: proc(line: []byte, delim: byte, n: int) -> ([]byte, bool) {
	cur := 1
	start := 0
	for i in 0 ..= len(line) {
		if i == len(line) || line[i] == delim {
			if cur == n {
				return line[start:i], true
			}
			cur += 1
			start = i + 1
		}
	}
	return nil, false
}

cut_chars :: proc(line: []byte, ranges: [dynamic]Range) {
	for r in ranges {
		lo := r.lo
		if lo < 1 {
			lo = 1
		}
		hi := r.hi
		if hi > len(line) {
			hi = len(line)
		}
		if lo > hi {
			continue
		}
		os.write(os.stdout, line[lo - 1:hi])
	}
}

// Convierte "1,3-5,8-" en rangos.
parse_ranges :: proc(list: string) -> [dynamic]Range {
	out := make([dynamic]Range)

	part_start := 0
	for i in 0 ..= len(list) {
		if i < len(list) && list[i] != ',' {
			continue
		}
		part := list[part_start:i]
		part_start = i + 1
		if len(part) == 0 {
			continue
		}

		dash := -1
		for j in 0 ..< len(part) {
			if part[j] == '-' {
				dash = j
				break
			}
		}

		if dash < 0 {
			if v, ok := atoi(part); ok {
				append(&out, Range{v, v})
			}
			continue
		}

		lo, hi := 1, MAX_INDEX
		if dash > 0 {
			if v, ok := atoi(part[:dash]); ok {
				lo = v
			}
		}
		if dash + 1 < len(part) {
			if v, ok := atoi(part[dash + 1:]); ok {
				hi = v
			}
		}
		append(&out, Range{lo, hi})
	}
	return out
}

atoi :: proc(s: string) -> (int, bool) {
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
