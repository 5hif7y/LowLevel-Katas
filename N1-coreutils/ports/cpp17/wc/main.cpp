// wc(1) — cuenta líneas, palabras y bytes.
// Port de openbsd-src/usr.bin/wc/wc.c.
//
// Este es el port que delata los errores de modo binario. En el port a C,
// `cat crlf.txt | wc -c` devolvía 4 sobre un archivo de 6 bytes, porque cat ya
// había mutilado los \r al leerlo.
//
// Acá se cuenta sobre bytes crudos, así que el número coincide siempre con el
// tamaño real. Que sea trivial es el punto: la dificultad del port a C no
// estaba en wc sino en la capa de abajo.
#include "common.hpp"

namespace {

// Una «línea» es un '\n'. Un archivo sin salto final no cuenta una línea extra,
// que es lo que haría un split() ingenuo.
long long count_lines(const std::string& s) {
    long long n = 0;
    for (unsigned char c : s) if (c == '\n') ++n;
    return n;
}

// Una «palabra» es una tirada de bytes que no sean espacio, tab, \n, \r, \v ni
// \f — la definición de isspace(3) que usa el original.
long long count_words(const std::string& s) {
    long long n = 0;
    bool in_word = false;
    for (unsigned char c : s) {
        const bool space = c == ' ' || c == '\t' || c == '\n' ||
                           c == '\r' || c == '\v' || c == '\f';
        if (space) in_word = false;
        else if (!in_word) { in_word = true; ++n; }
    }
    return n;
}

void print_counts(const std::string& s, bool l, bool w, bool b, const std::string& name) {
    std::string line;
    if (l) line += cu::pad7(count_lines(s));
    if (w) line += cu::pad7(count_words(s));
    if (b) line += cu::pad7(static_cast<long long>(s.size()));
    if (!name.empty()) line += " " + name;
    cu::out(line + "\n");
}

} // namespace

int main(int argc, char** argv) {
    cu::init_binary_stdio();

    bool l = false, w = false, b = false;
    std::vector<std::string> files;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-l") l = true;
        else if (a == "-w") w = true;
        else if (a == "-c") b = true;
        else files.push_back(a);
    }
    if (!l && !w && !b) l = w = b = true;

    if (files.empty()) {
        print_counts(cu::slurp_stdin(), l, w, b, "");
        return 0;
    }

    int status = 0;
    for (const auto& name : files) {
        bool ok = false;
        const std::string data = cu::slurp_file(name, ok);
        if (!ok) {
            cu::err("wc: " + name + ": No such file or directory\n");
            status = 1;
            continue;
        }
        print_counts(data, l, w, b, name);
    }
    return status;
}
