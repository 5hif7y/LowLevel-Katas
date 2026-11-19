# lib/http, lib/websocket, base/api (+client, freshtest) — informe (~8.400 líneas)

Tests: http 7, websocket 4, api 7, freshtest 1 — OK (-vet), 3–4 corridas.

Servidor HTTP/1.1 propio sobre core:net: parseo (chunked, 100-continue, keep-alive, pipelining), writer (Content-Length/chunked, HEAD/204/304), límites (431/413, timeouts), un hilo de pool por conexión, shutdown con semántica de Go. Router con semántica gorilla/mux ({var}, {var:regex}, PathPrefix, Methods+405, middleware, path clean 301). Websocket RFC 6455 ambos lados. base/api completo, incluido el protocolo de base de datos por websocket que usa la UI (marshal_record byte-compatible con sjson).

PORT-TODO: debug/stack|cpu|heap|allocs → 501 (no hay pprof); recuperación de panics en handlers; insert de objetos/null JSON; router sin Host/Queries/subrouters (Portmaster no los usa); wss y permessage-deflate; flags de cmdbase (etapa 6); bodies leídos completos (Go streamea).

Bugs de Go corregidos: `ready` con condición invertida; RecordFunc chequeaba el request en vez del record (panic con nil); handleInsert con accessor nil en records no-JSON; checkAPIKey hace key[:4] y entra en panic con claves < 4 bytes; qsub filtraba la suscripción si la query inicial fallaba.
Bug del port encontrado por otro agente y corregido: sync.Once re-entrante en ensure_routers → deadlock (test de regresión freshtest).

Trampas:
1. Nunca re-entrar un sync.Once (deadlock silencioso).
2. Go desbloquea un Read cerrando la conexión desde otra goroutine; en Windows shutdown(SD_BOTH) no despierta un recv bloqueado y cerrar un socket que usa otro hilo arriesga reuso del handle → WSAPoll en rebanadas de 50 ms + flag atómico.
3. listener.Close() para cortar Accept → mismo riesgo → despertar accept conectándose al propio listener.
4. El recorder de httptest conserva el body de HEAD/204 y los tests de Go dependen de eso.
5. select shutdown vs it.Next/sub.Feed → el shutdown cancela iteradores y suscripciones para cerrar sus canales.
6. DatabaseAPI vivo por GC → canal con buffer cerrado al apagar + refcount (lector + escritor + cada op).
7. ctx_after_func sobre el ctx del worker hay que quitarlo antes de retornar (el ctx se cancela después y llamaría a memoria liberada).
8. Type assertions de interfaces → tag `kind: typeid` en el handler.
9. `any` de StructFunc no puede apuntar al stack del proc.
10. Go PutNew no falla con clave existente: resetea la meta.
11. Mensajes del cliente: la op guarda el request para resucitar y el escritor libera lo que manda → la cola lleva copias.
12. `odin build -build-mode:test` ≠ `odin test`; pipelines con head/tail esconden un cuelgue → salida a archivo + timeout.
Nota: el sink del test de metrics respondía 204 con body; WinHTTP lo rechaza (12152). Corregido en el test.
