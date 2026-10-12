// dirname(1) — todo menos el último componente de una ruta.
// Port de openbsd-src/usr.bin/dirname/dirname.c.
//
// Igual que basename(1), es una operación sobre TEXTO. Se implementa el
// algoritmo de POSIX a mano en vez de std::filesystem::path::parent_path(), que
// en Windows trataría '\' como separador y rompería la equivalencia con el
// original.
//
// Reglas, en orden:
//   1. "//"                       -> "//"
//   2. todo barras                -> "/"
//   3. sacar las barras finales
//   4. si no queda ninguna barra  -> "."
//   5. sacar el último componente
//   6. sacar las barras finales que queden
#include "common.hpp"

static std::string posix_dirname(std::string p) {
    if (p == "//") return "//";

    while (!p.empty() && p.back() == '/') p.pop_back();
    if (p.empty()) return "/";

    const auto slash = p.rfind('/');
    if (slash == std::string::npos) return ".";

    p.resize(slash);
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: dirname string\n";
        return 1;
    }
    cu::out(posix_dirname(argv[1]) + "\n");
}
