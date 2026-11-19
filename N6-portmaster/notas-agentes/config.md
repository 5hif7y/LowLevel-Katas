# base/config — informe (15/15 tests, x4 estable, sin leaks)

Incluye la glue con la base de datos (database.go + provider.go). Tests Go portados: Get, ReleaseLevel, ConfigPersistence, JSONMapConversion (byte-exacto), ConfigCleaning, Registry, LayersGetters/Setters. config.json compatible con MarshalIndent de Go, incluido `<`.

PORT-TODO: flags -export-config-options / -devmode (los fija cmds/cmdbase, etapa 6); `%v` exacto de Go para floats muy chicos/grandes.

Diseño: sin GC, opciones y cachés nunca se liberan en ejecución (lista de retirados) → getters/perspectivas prestan strings con seguridad. Validity flag = contador de generación. Quirk de Go conservado: en updateExpertiseLevel/ReleaseLevel gana el default activo sobre el valor del usuario.

Trampas Go → Odin:
1. Map literals deshabilitados por defecto (dynamic literals) → `init_annotations: []Annotation`.
2. `Register(&Option{...})` → &T{} apunta a un temporal del stack (y sus slice literals) → register clona al heap y devuelve ^Option nuevo.
3. GetAsBool devuelve closure → struct paramétrico Typed_Option($T) + get(&g) con `when T == ...`.
4. interface{} → union cerrada; `2` sin tipo va a i64 aunque haya f64; una variable `int` no convierte: `i64(i)`.
5. `[]Value(nil)` no parsea → `([]Value)(nil)`.
6. Campo llamado como un paquete (`mgr: ^mgr.Manager`) → "Illegal declaration cycle" → renombrar.
7. -vet rechaza sombreado.
8. core:testing no tiene errorf → testing.expectf(t, false, ...).
9. `or_break` no vale en loops inline → for explícito.
10. (herramienta) Write convierte `<` en `<`.
11. Tests Go asumen ejecución serial; el runner Odin usa 4 hilos → mutex global + sync.once_do (TestMain).
12. Borrar de un map mientras se itera → juntar claves.
13. Maps anidados de Go se mutan por el padre; en Odin son valores → `vp := &m^[part]; sub = &vp.(Value_Map)`.
14. `Error(os.Error(nil))` no es nil → from_os_error.
15. Contrato de refcount en storage put: devolver una copia retenida del mismo record filtraba una referencia.
16. @(init) "contextless".
17. RE2 MatchString → core:text/regex {.No_Capture, .Unicode} (no anclado, como Go).
