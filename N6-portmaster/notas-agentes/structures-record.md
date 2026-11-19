# structures/{varint,container,dsd}, base/container, accessor, record, iterator — informe

Tests: varint 3, container 6, dsd 6, base/container 1, accessor 3, record 7, iterator 2 — todos OK, sin leaks, con y sin -vet.

Decisiones:
- varint: Go Unpack8 devuelve read=1 para la forma de 2 bytes (bug); el port devuelve 2. No afecta el formato.
- base/container = re-export de structures/container (`package base_container`: nombres de paquete únicos en todo el programa).
- dsd: codecs JSON y CBOR reflectivos propios, porque core:encoding/json (floats a precisión fija, []u8 como array, maps sin ordenar, sin HTML escape, rechaza punteros) y core:encoding/cbor (achica floats, sólo tag cbor) no son compatibles con Go. Vectores de bytes de Go derivados de las reglas del encoder (no capturados de un Go real: no hay toolchain).
- GZIP: descompresión con core:compress/gzip; compresión escribe bloques deflate "stored" (core no tiene encoder deflate) — válido para cualquier lector, bytes distintos a Go.
- record: Meta gencode byte-exacto (34 bytes LE); sobre: versión 1, varint(35), 'G'+meta, formato+datos.
- iterator: next = chan.Chan(^Record) buffer 10; done se cierra una vez (chan.close devuelve false si ya estaba cerrado).

PORT-TODO: YAML y MsgPack (.Not_Implemented); deflate real; big.Int; time.Time como RFC3339Nano; unions multi-variante y `any` al decodificar; glue net/http de dsd; gjson wildcards/queries/modifiers; Value union sin objetos/arrays mixtos.

Trampas Go → Odin:
1. Slice literal devuelto desde un proc vive en el stack → basura. Globals o make.
2. Dos paquetes `container` → "Duplicate declaration of package".
3. New/Delete chocan con builtins new/delete → new_container, meta_delete.
4. or_return con múltiples resultados exige resultados nombrados.
5. Selector implícito ambiguo en union de dos enums con el mismo miembro → calificar.
6. Indexar una constante string con índice variable no compila → variable.
7. No hay cap() para slices.
8. -vet rechaza sombreado `format, ok := Validate(format)`.
9. runtime.map_free_dynamic es require_results.
10. `==` no existe para structs con slices/punteros → comparar campo a campo (no hay DeepEqual).
11. strconv.generic_ftoa('f', -1) antepone '+'.
12. Encoders del core no compatibles con Go (arriba).
13. (herramienta) escapes \uXXXX en el input de Write/Edit se convertían en el carácter literal.
