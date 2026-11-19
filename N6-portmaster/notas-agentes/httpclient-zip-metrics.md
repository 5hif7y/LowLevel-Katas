# lib/httpclient (WinHTTP), lib/zip, base/metrics, base/template, base/apprise — informe

Tests: 12 / 7 / 4 / 1 / 3 OK (-vet -strict-style). HTTPS real a updates.safing.io por Schannel OK.

Decisiones: TLS = WinHTTP (stack nativo de Windows), no reimplementado. Zip lector propio (stored + inflate de core:compress, zip64, CRC) y escritor sólo stored (no hay encoder deflate). Nombres inseguros rechazados más estricto que filepath.IsLocal (.., ./x, //x, absolutos, backslash, `:` / ADS, controles, CON/NUL/COM1, punto/espacio final). metrics: registro Prometheus propio con el mismo formato que VictoriaMetrics.

PORT-TODO / desvíos: load average no existe en Windows → promedio de fracción ocupada de CPU (GetSystemTimes); sin métricas go_memstats/goroutines/gc (no hay runtime Go), sí las de proceso; info metric conserva labels go_* con valores de Odin; flags de cmdbase en etapa 6; Start persistido como string RFC3339Nano (dsd sin time.Time); httpclient: sin basic auth desde la URL, sin credenciales de proxy, sin replay de body en 307/308, sin Response.Uncompressed (WinHTTP borra Content-Encoding); zip sin límite de streaming en inflate.

Bug de Go corregido: runtimeMetrics.WritePrometheus descarta todas las líneas runtime cuando sólo hay namespace (Portmaster pone "portmaster").
Bug encontrado en el port: api.register_handler se re-entra en sync.Once (ensure_routers) → deadlock; reportado al agente de api.

Trampas: `do` keyword; strings.replace_all devuelve (string, allocated); #load constante no se slicea; fmt trata `{` como sintaxis → strings.concatenate para JSON; strconv.write_float escribe en el buffer pasado (uno por llamada); 'g'/'e' anteponen '+'; offsets EOCD (+10 cuenta, +12 tamaño); extract_all valida todo antes de escribir; os.remove_all lento (300 ms–1,5 s) en el runner; un servidor de test por escenario bloqueante.
WinHTTP: cancelar = WinHttpCloseHandle desde otro hilo (12017); timeouts por ctx (Go cubre también el body); WINHTTP_OPTION_DECOMPRESSION gzip; User-Agent por WinHttpAddRequestHeaders REPLACE; chunked con WINHTTP_IGNORE_REQUEST_TOTAL_LENGTH y framing propio; fragmento #x hay que cortarlo; URL final con WINHTTP_OPTION_URL; proxy de entorno emulado con NAMED_PROXY; conexión rechazada a localhost tarda ~2 s.
