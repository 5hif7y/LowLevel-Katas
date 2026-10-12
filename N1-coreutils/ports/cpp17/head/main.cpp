// head(1) — las primeras líneas de cada archivo.
// Port de openbsd-src/usr.bin/head/head.c.
//
// Detalle portable que suele romperse: con MÁS DE UN archivo, head(1) imprime
// un encabezado `==> nombre <==` antes de cada uno; con uno solo, no. No se
// deduce de la firma, hay que copiarlo del original.
#include "common.hpp"

namespace {

constexpr int kDefaultLines = 10;

// Emite los primeros n renglones CONSERVANDO el '\n' de cada uno. Un archivo
// sin salto final emite su última línea sin agregarlo: head(1) no inventa bytes.
void print_first_lines(const std::string& s, int n) {
    int seen = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\n') continue;
        if (seen == n) return;      // el corte va ANTES de escribir
        ++seen;
        cu::out(s.substr(start, i - start + 1));
        start = i + 1;
    }
    if (seen < n && start < s.size()) cu::out(s.substr(start));
}

bool parse_int(const std::string& s, int& out) {
    if (s.empty()) return false;
    int v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    out = v;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    cu::init_binary_stdio();

    int n = kDefaultLines;
    std::vector<std::string> files;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-n" && i + 1 < argc) {
            int v = 0;
            if (parse_int(argv[++i], v)) n = v;
        } else if (a.size() > 2 && a.compare(0, 2, "-n") == 0) {
            int v = 0;
            if (parse_int(a.substr(2), v)) n = v;   // forma -n5
        } else {
            files.push_back(a);
        }
    }
    if (n < 0) n = 0;

    if (files.empty()) {
        print_first_lines(cu::slurp_stdin(), n);
        return 0;
    }

    int status = 0;
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (files.size() > 1) {
            if (i > 0) cu::out("\n");
            cu::out("==> " + files[i] + " <==\n");
        }
        bool ok = false;
        const std::string data = cu::slurp_file(files[i], ok);
        if (!ok) {
            cu::err("head: " + files[i] + ": No such file or directory\n");
            status = 1;
            continue;
        }
        print_first_lines(data, n);
    }
    return status;
}
