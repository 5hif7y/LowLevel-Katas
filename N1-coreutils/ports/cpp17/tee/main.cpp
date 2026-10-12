// tee(1) — copia la entrada a la salida y además a archivos.
// Port de openbsd-src/usr.bin/tee/tee.c.
//
// El otro port que delata el modo texto. En el port a C, tee leía 8 bytes del
// pipe y escribía 9 en el archivo, porque el CRT traducía cada \n a \r\n al
// escribir. Medición en tests/run-suite.sh, sección 4:
//
//	$ printf 'bin\000ary\n' | tee.exe out.bin ; wc -c < out.bin
//	9        <- cuando debería decir 8
//
// Acá se resuelve con std::ios::binary al abrir el archivo de salida, más
// init_binary_stdio() para stdout.
#include "common.hpp"

int main(int argc, char** argv) {
    cu::init_binary_stdio();

    bool append = false;
    std::vector<std::string> files;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-a") append = true;
        else files.push_back(a);
    }

    const std::string data = cu::slurp_stdin();

    // Orden del original: primero los archivos, después stdout. Si un archivo no
    // se puede abrir, tee(1) avisa y SIGUE con el resto; no aborta.
    int status = 0;
    for (const auto& name : files) {
        cu::OutFile f(name, append);
        if (!f.ok()) {
            cu::err("tee: " + name + ": cannot open\n");
            status = 1;
            continue;
        }
        if (!f.write(data)) {
            cu::err("tee: " + name + ": write error\n");
            status = 1;
        }
    }

    if (!cu::out(data)) return 1;   // consumidor ido
    return status;
}
