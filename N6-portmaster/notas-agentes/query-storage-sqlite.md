# vendor/sqlite3, database/query, database/storage (+sinkhole, hashmap, fstree, sqlite) — informe

Tests: sqlite3 1, query 9, sinkhole 2, hashmap 2, fstree 1, sqlite 8 — OK, sin leaks; 4 corridas con 8 hilos estables. Requiere sqlite3.dll en PATH (DEV/vcpkg/installed/x64-windows/bin) o junto al exe. Link: `#config(SQLITE3_LIB, ...)` relativo a odin/vendor/sqlite3 (override -define:SQLITE3_LIB=...).

Compatibilidad: mismo `<location>/db.sqlite`, mismo esquema `records`, SQL de migraciones `#load` del árbol Go, misma tabla `gorp_migrations` e ids; upsert byte-idéntico al que genera bob (test).

Bug de Go conservado: 0_settings.sql pone auto_vacuum=INCREMENTAL después de crear gorp_migrations → SQLite lo ignora (modo NONE), incremental_vacuum no hace nada. Test lo afirma.

Diferencias deliberadas: fstree devuelve claves con `/`; chequeo de base-dir rechaza hermanos con mismo prefijo; PutMany drena tras error (nunca bloquea al emisor); Shutdown espera batch abierto.

PORT-TODO: hashmap sin refcount (Query manda copias); iterator.Error no lleva mensaje de storage (→ .Failed + log); dberrors/dbinfo/factory/bob no necesarios; importador bbolt (plan en REPLACED.md).

API storage: backend embebe `using base: storage.Interface`; entradas opcionales nil = no soportado (reemplaza type assertions de Go). Registro con @(init) "contextless" = blank imports de Go.

Trampas Go → Odin:
1. Cambiar un resultado nombrado dentro de defer no altera un `return q, err` explícito → el caller recibía un puntero recién liberado. Proc envoltorio para el cleanup.
2. or_return exige resultados nombrados.
3. `where` es keyword; `delete` y `query` chocan con builtin/paquete → where_, delete_key, query_records.
4. interface{} → any + type switch; DeepEqual → condition_equal a mano.
5. SQLITE_TRANSIENT ((destructor)-1) no se puede formar como constante proc → parámetro uintptr, TRANSIENT :: ~uintptr(0).
6. Slicear un array dentro de un case de switch sobre union: "not addressable" → copiar y `switch &e in copy`.
7. Hilos con default context liberaban con otro allocator → storage.worker_context(allocator).
8. Borrar de un map mientras se itera → juntar claves y borrar después.
9. select con time.After → try_send con backoff.
10. Contrato del iterator: finish cierra next y luego done; destruir tras cerrar next puede liberar done mientras se cierra → esperar is_done. (Arreglar/documentar en iterator.)
11. @(init) debe ser "contextless"; registro en heap allocator.
12. filepath.join devuelve (string, Allocator_Error) en este nightly; mensajes de core:text/regex distintos a Go.
13. strconv.parse_i64 acepta `_` y no chequea overflow → parser estricto estilo Go.
14. formato 'g' antepone '+'.
