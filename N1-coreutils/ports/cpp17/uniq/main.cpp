// uniq(1) — colapsa líneas adyacentes repetidas.
// Port de openbsd-src/usr.bin/uniq/uniq.c.
//
// ADYACENTES es la palabra que importa: uniq(1) NO deduplica el archivo, sólo
// colapsa repeticiones contiguas. `a b a` sale tal cual. Para deduplicar de
// verdad hay que ordenar antes — de ahí el idiom `sort | uniq`.
//
//   -c   prefija cada línea con el número de repeticiones
//   -d   emite sólo las líneas que se repiten
//   -u   emite sólo las que NO se repiten
#include "common.hpp"
#include <memory>

namespace {

// Devuelve la línea que arranca en `pos` y el offset de la siguiente.
std::string next_line(const std::string& s, std::size_t pos, std::size_t& next) {
    const auto nl = s.find('\n', pos);
    if (nl == std::string::npos) { next = s.size(); return s.substr(pos); }
    next = nl + 1;
    return s.substr(pos, nl - pos);
}

} // namespace

int main(int argc, char** argv) {
    cu::init_binary_stdio();

    bool count = false, only_dup = false, only_uniq = false;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-c") count = true;
        else if (a == "-d") only_dup = true;
        else if (a == "-u") only_uniq = true;
        else positional.push_back(a);
    }

    // -d y -u juntos se cancelan: ninguna línea puede ser a la vez repetida y
    // única, así que no hay nada que emitir.
    if (only_dup && only_uniq) return 0;

    const std::string input  = positional.size() > 0 ? positional[0] : "";
    const std::string output = positional.size() > 1 ? positional[1] : "";

    std::string data;
    if (!input.empty() && input != "-") {
        bool ok = false;
        data = cu::slurp_file(input, ok);
        if (!ok) { cu::err("uniq: " + input + ": cannot open\n"); return 1; }
    } else {
        data = cu::slurp_stdin();
    }

    std::FILE* sink = stdout;
    std::FILE* owned = nullptr;
    if (!output.empty() && output != "-") {
        owned = std::fopen(output.c_str(), "wb");
        if (!owned) { cu::err("uniq: " + output + ": cannot open\n"); return 1; }
        sink = owned;
    }

    // Recorrido por tramos contiguos iguales.
    std::size_t pos = 0;
    while (pos < data.size()) {
        std::size_t next = 0;
        const std::string line = next_line(data, pos, next);

        long long n = 1;
        while (next < data.size()) {
            std::size_t other_next = 0;
            const std::string other = next_line(data, next, other_next);
            if (other != line) break;
            ++n;
            next = other_next;
        }

        const bool emit = only_dup ? n > 1 : (only_uniq ? n == 1 : true);
        if (emit) {
            std::string rec;
            if (count) { rec += cu::pad7(n); rec += ' '; }
            rec += line;
            rec += '\n';
            if (std::fwrite(rec.data(), 1, rec.size(), sink) != rec.size()) {
                if (owned) std::fclose(owned);
                return 1;
            }
        }
        pos = next;
    }
    if (owned) std::fclose(owned);
    return 0;
}
