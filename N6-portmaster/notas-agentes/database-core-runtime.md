# base/database (núcleo), dbmodule, migration, base/runtime — informe

Tests: database 3, dbmodule 1, migration 2, runtime 4 (+ regresión de record/accessor/iterator/query/storage) — OK, x10 sin leaks. TestDatabaseSystem corre para sqlite, bbolt (servido por sqlite), hashmap, fstree, con y sin shadow delete.

Cambios a paquetes ya commiteados: refcount atómico en record.Base (`retain`, `ref_count`; `destroy` libera una ref) — Go comparte records entre caché, feeds y hashmap gracias al GC. hashmap ahora retiene su propia referencia y `get` devuelve una ref que el caller destruye (resuelve su PORT-TODO). Bug: sqlite shutdown liberaba con el allocator del caller.

PORT-TODO: caché LRU con expiración (no ARC de gcache), callback de evicción fuera del lock; queries de runtime secuenciales (Go: goroutine por provider); importador bbolt; put_many sin goroutine proxy ni timeout de 1 s.

Bugs de Go corregidos: FlushCache con condición invertida (nunca hacía nada); getController podía arrancar el mismo storage dos veces (carrera) → re-check bajo lock; Interface.Delete no bloqueaba el record y seguía sirviendo el borrado desde la caché; Migrate tragaba errores al guardar el estado → Diagnostics al final.

Trampas Go → Odin:
1. Records compartidos por el GC → refcount.
2. EnsureDirectory con ACL admin-only: un admin no elevado pone el permiso y después no puede abrir su propia carpeta → toggle `apply_directory_acls` (limpiar: `icacls <dir> /reset /T`).
3. Storage arrancado con allocator global y apagado desde el hilo del test → bad free.
4. Buffer de error fijo de 200 bytes cortaba rutas largas de Windows → 255 (largo en u8).
5. TestMain + base compartida vs runner paralelo → un único @(test) secuencial.
6. `using base:` dentro de un struct que ya tiene `using base` → "already declared" → `using inject:`.
7. `@(private)` antes de `package` no es file-private → `#+private` en la primera línea.
8. select sobre Done/trigger/dos tickers → cond + ctx_after_func + cond_wait_with_timeout.
9. Canal de trigger sin buffer + envío no bloqueante pierde triggers → flag + cond_signal.
10. Unions Error de distintos paquetes no son intercambiables → conversores con código embebido.
11. Supuesto de test equivocado: Go convierte el TTL relativo en expiración recién al guardar.
12. `odin test` borra el exe → -keep-executable.
