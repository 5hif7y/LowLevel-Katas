// yes(1) — repite una línea para siempre.
// Port de openbsd-src/usr.bin/yes/yes.c.
//
// Este es el port con la lección más fuerte del kata (ver
// Docs/porter-guide/10-estudio-de-casos-de-depuracion.md). El original en C
// descarta el valor de retorno de puts() y confía en que SIGPIPE lo mate cuando
// el consumidor cierra el pipe. Windows no tiene SIGPIPE: en el port a C el
// bucle se volvía un giro infinito a CPU completa, y hubo que emular SIGPIPE en
// compat/src/sigpipe.c.
//
// En C++ la situación es la MISMA que en C — std::cout no lanza excepción por
// defecto, sólo marca failbit — pero acá sí podemos hacer lo correcto sin capa
// de compatibilidad: chequear el estado del stream y salir. Es la única forma
// de que el bucle termine.
#include "common.hpp"

int main(int argc, char** argv) {
    const std::string line = std::string(argc > 1 ? argv[1] : "y") + "\n";

    for (;;) {
        // pipe cerrado: no hay SIGPIPE que nos salve, hay que chequear
        if (!cu::out(line)) return 1;
    }
}
