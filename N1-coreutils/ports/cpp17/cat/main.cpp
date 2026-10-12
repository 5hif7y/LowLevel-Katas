// cat(1) — concatena archivos a la salida estándar.
// Port de openbsd-src/bin/cat/cat.c.
//
// Lo interesante de este port es la E/S, no la lógica. En el port a C, cat
// abría los archivos con open(2) en modo texto y el CRT se comía los \r: un
// `cat imagen.png > copia.png` no reproducía el original. Hubo que forzar
// _O_BINARY en compat/src/binary_mode.c.
//
// Acá se resuelve con std::ios::binary en el ifstream y con init_binary_stdio()
// para stdout. Es el mismo bug y la misma solución, en otro lenguaje.
#include "common.hpp"

int main(int argc, char** argv) {
    cu::init_binary_stdio();

    if (argc < 2) {
        cu::out(cu::slurp_stdin());
        return 0;
    }

    int status = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string name = argv[i];

        // "-" significa la entrada estándar, igual que en el original.
        if (name == "-") {
            cu::out(cu::slurp_stdin());
            continue;
        }

        bool ok = false;
        const std::string data = cu::slurp_file(name, ok);
        if (!ok) {
            cu::err("cat: " + name + ": No such file or directory\n");
            status = 1;
            continue;
        }
        cu::out(data);
        if (!std::cout) return 1;   // consumidor ido: no hay SIGPIPE que ayude
    }
    return status;
}
