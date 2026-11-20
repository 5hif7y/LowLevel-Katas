# service/netquery + orm — informe (~7.500 líneas; 6 + 14 tests x3) — "Network History" + "Bandwidth"

- orm sobre pm:vendor/sqlite3 + core:reflect: tags `sqlite:"name,primary,autoincrement,not-null,nullable,unixnano,time,…,default="`, generación de schema, encode/decode (Convert de Go), run_query_result en lugar de WithResult, reintento ante LOCKED/BUSY (el sqlite de vcpkg no tiene unlock_notify).
- netquery: `main` en memoria (shared cache) + `history.db` ATTACH, WAL, synchronous=NORMAL; **schema byte-idéntico al de Go** (test sobre el CREATE y sqlite_master, con sus rarezas: `external  NOT NULL`, `allowed ,`, `DEFAULT 0  NOT NULL`); **abre un history.db escrito por Go**; lenguaje de consultas JSON de la UI (tests de Go verbatim), charts, 6 endpoints, purga por `history/keep` por app. Hook firewall.deps.netquery_update_bandwidth.
- e2e: con `history/enable` (por app) y `bandwidth/enable` la conexión llega a main, history y al archivo; el push runtime con el JSON de Go; bandwidth acumulado; consulta de la UI por HTTP; cleanup y clear por perfil; **con ambas opciones apagadas no se escribe nada en history.db**.

Bugs de Go corregidos — **dos inyecciones SQL en la API de netquery**:
1. `charOnlyRegexp "[a-zA-Z]+"` no está anclado → cualquier alias `as` con al menos una letra se concatenaba en el SQL. Ahora identificador SQL anclado (sólo letras rompería la UI, que manda "first_connection").
2. `databases` se concatenaba sin validar → sólo "main" y "history".
(La API escucha en 127.0.0.1 con autenticación, pero cualquier proceso local con un token de la UI podía inyectar SQL.)
Otros: TextSearch con campo desconocido → deref nil; `$in`/`$notIn` con `null` → panic en reflect.TypeOf(nil).Kind(); colisión de parámetros con `$sum` → panic; ToParamMap fallaba con `sqlite:"-"`; CleanupHistory/MarkAllHistoryConnectionsEnded formateaban en hora local contra tiempos guardados en UTC.
Conservados: string suelto en select ignorado; orderBy null → 400; $min con condición sin field falla; DEFAULT float en formato 'b'.
Trampas: `bind_string("")` liga NULL (raw_data("") es nil) → byte estático de largo 0; `distinct` y `not_in` son keywords de Odin; Odin no desescapa `\"` en tags; extra_data debe ser byte-exacto (json.Marshal de un map de Go) → valores recortados con un scanner del JSON de network en vez de re-codificar; layered profile no se refresca tras un guardado externo hasta profile.update.
