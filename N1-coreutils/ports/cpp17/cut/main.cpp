// cut(1) — extrae campos o columnas de cada línea.
// Port de openbsd-src/usr.bin/cut/cut.c.
//
//   -f LIST   campos separados por el delimitador de -d (TAB por defecto)
//   -c LIST   rangos de caracteres
//
// LIST es "1,3", "2-5", "3-", "-4". El parser de rangos es el corazón de cut(1)
// y donde vive casi toda su superficie de error.
//
// Detalle portable: cut(1) NO reordena. `cut -f 3,1` emite el campo 3 y después
// el 1, en el orden pedido.
#include "common.hpp"

namespace {

struct Range { long long lo, hi; };

constexpr long long kMaxIndex = 1LL << 30;

bool atoi_(const std::string& s, long long& out) {
    if (s.empty()) return false;
    long long v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    out = v;
    return true;
}

std::vector<Range> parse_ranges(const std::string& list) {
    std::vector<Range> out;
    std::size_t start = 0;
    while (start <= list.size()) {
        const auto comma = list.find(',', start);
        const std::string part = list.substr(start, comma == std::string::npos
                                                      ? std::string::npos
                                                      : comma - start);
        start = (comma == std::string::npos) ? list.size() + 1 : comma + 1;
        if (part.empty()) continue;

        const auto dash = part.find('-');
        long long v = 0;
        if (dash == std::string::npos) {
            if (atoi_(part, v)) out.push_back({v, v});
            continue;
        }
        long long lo = 1, hi = kMaxIndex;
        if (dash > 0) { long long t; if (atoi_(part.substr(0, dash), t)) lo = t; }
        if (dash + 1 < part.size()) { long long t; if (atoi_(part.substr(dash + 1), t)) hi = t; }
        out.push_back({lo, hi});
    }
    return out;
}

// Campo n-ésimo (1-based) de la línea, o false si no existe.
bool nth_field(const std::string& line, char delim, long long n, std::string& out) {
    long long cur = 1;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == delim) {
            if (cur == n) { out = line.substr(start, i - start); return true; }
            ++cur;
            start = i + 1;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    cu::init_binary_stdio();

    enum class Mode { None, Fields, Chars } mode = Mode::None;
    char delim = '\t';
    std::string list;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.size() < 2 || a[0] != '-') { cu::err("cut: illegal option\n"); return 1; }

        const char opt = a[1];
        std::string value = a.substr(2);
        if (value.empty()) {
            if (i + 1 >= argc) { cu::err("cut: option requires an argument\n"); return 1; }
            value = argv[++i];
        }

        switch (opt) {
        case 'f': mode = Mode::Fields; list = value; break;
        case 'c': mode = Mode::Chars;  list = value; break;
        case 'd': if (value.empty()) return 1; delim = value[0]; break;
        default:  cu::err(std::string("cut: illegal option -- ") + opt + "\n"); return 1;
        }
    }

    if (mode == Mode::None || list.empty()) {
        cu::err("cut: you must specify a list of bytes, characters, or fields\n");
        return 1;
    }

    const auto ranges = parse_ranges(list);
    const std::string data = cu::slurp_stdin();

    // Se opera línea por línea SIN el '\n', que se re-agrega al final: así el
    // delimitador nunca se confunde con el salto de línea.
    std::size_t start = 0;
    for (std::size_t i = 0; i <= data.size(); ++i) {
        if (i < data.size() && data[i] != '\n') continue;
        const std::string line = data.substr(start, i - start);
        start = i + 1;

        std::string out;
        if (mode == Mode::Fields) {
            bool first = true;
            for (const auto& r : ranges) {
                for (long long f = r.lo; f <= r.hi && f <= kMaxIndex; ++f) {
                    std::string content;
                    if (!nth_field(line, delim, f, content)) continue;
                    if (!first) out += delim;
                    out += content;
                    first = false;
                }
            }
        } else {
            for (const auto& r : ranges) {
                const long long lo = r.lo < 1 ? 1 : r.lo;
                const long long hi = r.hi > static_cast<long long>(line.size())
                                         ? static_cast<long long>(line.size()) : r.hi;
                if (lo > hi) continue;
                out += line.substr(static_cast<std::size_t>(lo - 1),
                                   static_cast<std::size_t>(hi - lo + 1));
            }
        }
        cu::out(out + "\n");
    }
    return 0;
}
