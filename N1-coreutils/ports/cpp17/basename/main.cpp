// basename(1) — el último componente de una ruta, con sufijo opcional quitado.
// Port de openbsd-src/usr.bin/basename/basename.c.
//
// basename(1) opera sobre TEXTO, no sobre el sistema de archivos: no toca el
// disco ni comprueba que la ruta exista. Por eso el algoritmo de POSIX se
// escribe a mano en vez de usar std::filesystem::path::filename(), que sigue
// las convenciones de la plataforma anfitriona y en Windows trataría '\' como
// separador. El original de OpenBSD sólo conoce '/', y un basename que de
// pronto entienda '\' dejaría de ser el mismo programa.
//
// Reglas de POSIX, en orden:
//   1. "//"                      -> "//"
//   2. todo barras               -> "/"
//   3. sacar las barras finales
//   4. si no queda ninguna barra -> toda la cadena
//   5. lo que sigue a la última barra
#include "common.hpp"

static std::string posix_basename(std::string p) {
    if (p == "//") return "//";

    while (!p.empty() && p.back() == '/') p.pop_back();
    if (p.empty()) return "/";

    const auto slash = p.rfind('/');
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: basename string [suffix]\n";
        return 1;
    }

    std::string base = posix_basename(argv[1]);

    // El sufijo sólo se quita si no deja la cadena vacía: `basename .txt .txt`
    // debe seguir dando ".txt".
    if (argc > 2) {
        const std::string suffix = argv[2];
        if (!suffix.empty() && suffix.size() < base.size() &&
            base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0) {
            base.resize(base.size() - suffix.size());
        }
    }

    cu::out(base + "\n");
}
