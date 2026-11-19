# service/updates (+coreupdater), service/configure, service/sync — informe (~7.300 líneas)

Tests: updates 15, coreupdater 2, configure 2, sync 7 — OK x3. Go portado: updates_test completo, index_test (20 rutas válidas/inválidas), signature_test (sin firma, clave equivocada, alterado → rechazado). Servidor local (pm:lib/http) con índice firmado por una clave jess generada en el test; artefactos raw, .gz, .zip; espejos; reuso por hash; checksum fallido no deja .download. Smoke opcional `PM_UPDATES_SMOKE=1`: baja stable.v3.json real de updates.safing.io y lo verifica con las claves reales (pasó).

Decisiones: go-version propio (Compare idéntico, máx. 8 segmentos); subconjunto de gobwas/glob; parser RFC3339 estricto propio (parse_go_time); `coreupdater` en subpaquete para que updates no importe core; `package pm_sync`; emisor YAML compatible con yaml.v3 (sangría, orden natural de claves, análisis de escalares de libyaml, quoting de booleanos 1.1).
PORT-TODO: lectura YAML con reglas 1.1 (yes/no/on/off, octal 0777) vía lib/yaml; perfiles de sync por hooks (etapa 3, 501 mientras falten); tiempos en UTC; unpack > 1 GB rechazado (Go trunca en silencio).
`updates.apply_file_permissions = false` en tests: las ACL de ensure_directory dejan a un proceso no elevado fuera de sus propios temporales.

Bugs de Go corregidos: ImportProfile usa r.Export.Source (opcional) → con source vacío chequea `core:profiles//<id>` y rechaza todo mergedFrom; ExportProfile limpia PresentationPath en el perfil cargado en vez del exportado; GenerateIndexFromDir arma URLs con backslashes en Windows. Conservados: glob con os.PathSeparator en Windows (`\` también es escape); CheckSHA256 envuelve un error nil (`%!w(<nil>)`).
Bug del port encontrado: jess.parse_time_rfc3339 (core:time lee exactamente 2 dígitos de fracción) → abierto en STAGES.

Trampas: string literal asignado a un config clonado al heap y después liberado por el destructor → segfault sin mensaje (clonar después del Check); `any` de StructFunc al stack; `package sync` choca con core:sync; mgr.wait_for_workers no vuelve mientras viva un Worker_Mgr → esperar un efecto observable; heredocs de python manglean escapes.
