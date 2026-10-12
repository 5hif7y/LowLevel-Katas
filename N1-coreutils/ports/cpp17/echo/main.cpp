// echo(1) — imprime sus argumentos separados por un espacio.
// Port de openbsd-src/bin/echo/echo.c.
//
// El original de OpenBSD interpreta secuencias de escape con barra invertida y
// reconoce -n sólo como PRIMER argumento. Acá se implementa el contrato central
// —separar con espacios, terminar en salto de línea, -n para suprimirlo—; las
// secuencias quedan como ejercicio (el README de ports/ lo anota).
#include "common.hpp"

int main(int argc, char** argv) {
    int first = 1;
    bool newline = true;

    if (argc > 1 && std::string(argv[1]) == "-n") {
        newline = false;
        first = 2;
    }

    std::string line;
    for (int i = first; i < argc; ++i) {
        if (i > first) line += ' ';
        line += argv[i];
    }
    if (newline) line += '\n';

    cu::out(line);
}
