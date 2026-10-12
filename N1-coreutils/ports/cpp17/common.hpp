// Utilidades compartidas por los ports a C++17.
//
// El header existe sobre todo por init_binary_stdio(): C++ sobre Windows tiene
// EXACTAMENTE la misma trampa de modo texto que C. std::cout y std::ifstream
// pasan por el mismo CRT, así que sin ios::binary y sin _setmode el port a C++
// reproduce los tres bugs del port a C: \n se vuelve \r\n, 0x1A se confunde con
// EOF, y `cat imagen.png > copia.png` no reproduce el original.
//
// Ver Docs/porter-guide/10-estudio-de-casos-de-depuracion.md.
#pragma once

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <fcntl.h>
#  include <io.h>
#endif

namespace cu {

// Pone los streams estándar en binario. En Unix no existe el modo texto y esto
// no compila a nada: la semántica buscada ya es la de por defecto.
inline void init_binary_stdio() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
}

// El mismo truco que en el port a C, y por la misma razón: esto tiene que correr
// ANTES de main(), mientras los iostreams todavía no existen.
//
// Medido: llamar a init_binary_stdio() como primera línea de main() NO alcanza
// para std::cout — la salida sigue saliendo con \r\n. La versión en C lo hace
// desde la sección .CRT$XCU y sí funciona. Acá el equivalente es un objeto
// estático cuyo constructor corre durante la inicialización del programa.
namespace detail {
struct BinaryStdioInit {
    BinaryStdioInit() { init_binary_stdio(); }
};
// inline: una sola instancia aunque el header se incluya en varias unidades.
inline const BinaryStdioInit g_binary_stdio_init{};
} // namespace detail

// Lee un stream entero, byte por byte y sin traducciones.
//
// Se lee DIRECTAMENTE sobre el buffer del propio std::string en vez de usar un
// char buf[65536] local. La versión con el array en el stack compila y anda a
// -O0 pero revienta silenciosamente a -O1/-O2/-O3 con MinGW-w64 UCRT 13.2.0:
// el proceso termina sin ejecutar el código que sigue a la lectura y sin error
// visible. Con el buffer en el heap no hay problema en ningún nivel.
inline std::string slurp(std::istream& in) {
    constexpr std::size_t kChunk = 65536;
    std::string out;
    std::size_t used = 0;

    for (;;) {
        if (out.size() < used + kChunk) out.resize(used + kChunk);
        in.read(&out[used], static_cast<std::streamsize>(kChunk));
        const std::streamsize n = in.gcount();
        used += static_cast<std::size_t>(n);
        if (n < static_cast<std::streamsize>(kChunk)) break;
    }

    out.resize(used);
    return out;
}

// Lee stdin completo por stdio de C, por la misma razón que slurp_file.
inline std::string slurp_stdin() {
    std::string out;
    char buf[65536];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, stdin)) > 0) out.append(buf, n);
    return out;
}

// Lee un archivo completo. `ok` distingue "vacío" de "no se pudo abrir".
//
// OJO: acá se usa stdio de C y no std::ifstream, y no es por gusto.
//
// Con MinGW-w64 UCRT g++ 13.2.0, std::ifstream sobre un archivo hace SEGFAULT al
// compilar con -O1/-O2/-O3. Reproducido y acotado en tests/probes del kata (y en
// /tmp durante la depuración): a -O0 anda, a -O2 el proceso muere sin mensaje.
// Cambiar la construcción (constructor con std::string, con .c_str(), open()
// separado) mueve el síntoma pero no lo elimina, y el mismo código copiado a
// mano en un main() sí funciona — o sea que depende del layout del binario. Eso
// es un bug de la toolchain, no del programa, y no se arregla desde acá.
//
// std::fopen/fread no lo tienen y son igual de portables, así que la lectura de
// archivos va por ahí. El resto del port sigue siendo C++17 puro.
inline std::string slurp_file(const std::string& path, bool& ok) {
    std::FILE* f = std::fopen(path.c_str(), "rb");   // "rb": byte-exacto
    if (!f) { ok = false; return {}; }
    ok = true;

    std::string out;
    char buf[65536];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);

    std::fclose(f);
    return out;
}

// Escritura de bytes por stdio de C.
//
// No es capricho: medido con MinGW-w64 UCRT g++ 13.2.0, _setmode(_O_BINARY)
// sobre stdout NO afecta a std::cout — la salida sigue saliendo con \r\n aunque
// el _setmode se llame desde un inicializador estático antes de main(). El
// camino de stdio de C sí respeta el modo. Como acá lo que importa es que los
// bytes salgan intactos, la E/S de bytes va por stdio y el resto del programa
// sigue siendo C++17 normal.
//
// Devuelve false si la escritura falló (típicamente, el consumidor de un pipe
// se fue). En C++ no hay SIGPIPE que mate al proceso: hay que chequear.
inline bool out(const std::string& s) {
    if (s.empty()) return true;
    return std::fwrite(s.data(), 1, s.size(), stdout) == s.size();
}

inline void err(const std::string& s) {
    if (!s.empty()) std::fwrite(s.data(), 1, s.size(), stderr);
}

// Archivo de salida binario. Existe porque std::ofstream arrastra el mismo
// problema de toolchain que std::ifstream (ver slurp_file).
class OutFile {
public:
    OutFile() = default;
    OutFile(const std::string& path, bool append) {
        f_ = std::fopen(path.c_str(), append ? "ab" : "wb");
    }
    ~OutFile() { if (f_) std::fclose(f_); }

    OutFile(const OutFile&) = delete;
    OutFile& operator=(const OutFile&) = delete;

    bool ok() const { return f_ != nullptr; }
    bool write(const std::string& s) const {
        return f_ && std::fwrite(s.data(), 1, s.size(), f_) == s.size();
    }

private:
    std::FILE* f_ = nullptr;
};

// wc(1) y uniq(1) alinean los números a 7 columnas rellenando con ESPACIOS.
inline std::string pad7(long long v) {
    std::string s = std::to_string(v);
    if (s.size() < 7) s.insert(0, 7 - s.size(), ' ');
    return s;
}

} // namespace cu
