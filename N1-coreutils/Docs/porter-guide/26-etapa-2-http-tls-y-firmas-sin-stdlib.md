# Módulo 26: Etapa 2 — HTTP, TLS y firmas cuando el lenguaje no los trae

Go viene con `net/http`, `crypto/tls`, `archive/zip`, `encoding/json`, un
profiler y un scheduler. La etapa 2 de Portmaster (API, servicio, updates)
usa todo eso. Odin trae sockets, `core:crypto` y poco más. Este módulo es el
inventario de **qué se escribió, qué se delegó al sistema operativo y qué se
dejó afuera**, y por qué.

Código (fork `port/odin`): `odin/lib/{http,websocket,httpclient,zip,yaml}`,
`odin/jess/`, `odin/base/{api,metrics,template,apprise}`,
`odin/service/{core,ui,status,control,broadcasts,updates,configure,sync}`.
Commits `f057d0f7`, `8e94e41f`, `c29a1894`, `ec014989`. ~40.000 líneas de
Odin nuevas, ~125 tests.

---

## 1. La matriz de decisión

| Necesidad | Go | Odin | Decisión |
|---|---|---|---|
| Servidor HTTP/1.1 | `net/http` | nada | **escrito** (`lib/http`): parser, writer chunked, keep-alive, pipelining, límites 431/413, shutdown |
| Router | gorilla/mux | nada | **escrito** con la semántica que usa Portmaster (`{var}`, `{var:regex}`, prefix, 405); sin subrouters ni Host |
| Websocket | gorilla/websocket | nada | **escrito** (RFC 6455, ambos lados) |
| Cliente HTTPS | `net/http` + `crypto/tls` | sin TLS | **delegado a Windows**: WinHTTP (Schannel). Reimplementar TLS no es un port, es otro proyecto |
| zip | `archive/zip` | inflate sí, zip no | **escrito** el lector; escritor sólo *stored* (no hay deflate) |
| YAML | ghodss / yaml.v3 | nada | **escrito** el subconjunto: lector YAML→JSON y un emisor compatible con yaml.v3 |
| Firmas de updates | safing/jess | ed25519, aes, chacha… | **porteado jess** + lo que falta en core: BLAKE3, scrypt, Salsa20, SHA-512/224, HMAC/HKDF genéricos, DER PKCS#1, base58 |
| Profiling | `runtime/pprof` | nada | **afuera**: los endpoints responden 501 |
| Recuperar un panic en un handler | `recover` | no existe | **afuera**: documentado |

Criterio: si es un **formato o protocolo** que Portmaster habla con el mundo
(HTTP hacia la UI, websocket de la base, zip y firmas de updates), se escribe
y se prueba contra bytes reales. Si es **criptografía de transporte** con
superficie de ataque enorme (TLS), se usa la del sistema. Si depende del
**runtime de Go** (pprof, goroutine stacks), no hay qué portear.

## 2. Trampas de sockets en Windows

Go desbloquea un `Read` bloqueado cerrando la conexión desde otra goroutine,
y corta un `Accept` con `listener.Close()`. En Windows:

* `shutdown(SD_BOTH)` **no** despierta un `recv` bloqueado;
* cerrar un socket que otro hilo está usando arriesga que el número de handle
  se reutilice para otra cosa antes de que ese hilo vuelva.

Patrón usado: `WSAPoll` en rebanadas de 50 ms + un flag atómico de parada
(conexiones keep-alive ociosas, `stop()` de websocket, cliente de la API).
Para cortar `accept`, el dueño **se conecta a su propio listener** y después
lo cierra.

Conectar a un puerto cerrado de localhost tarda ~2 s en Windows (reintentos
de SYN): los tests de "conexión rechazada" son lentos por eso, no por el código.

## 3. WinHTTP en vez de TLS

* Cancelar = `WinHttpCloseHandle` sobre el request **desde otro hilo**; la
  llamada bloqueada vuelve con 12017. El contexto (`^mgr.Context`) lo hace en
  su `after_func`, con un mutex porque esa función corre bajo el lock del
  contexto.
* El timeout de Go cubre también la lectura del body: se implementa con un
  contexto hijo con deadline, no con `WinHttpSetTimeouts`.
* `WINHTTP_OPTION_DECOMPRESSION` quita `Content-Encoding` y `Content-Length`
  por su cuenta: `Response.Uncompressed` de Go no tiene equivalente.
* Un `204` con body es una respuesta inválida y WinHTTP la rechaza (12152).
  El servidor de prueba de metrics lo hacía; Go lo toleraba.
* `User-Agent` sólo se reemplaza con `WinHttpAddRequestHeaders(REPLACE)`.

## 4. jess: misma criptografía, otros bordes

Diferencias entre las APIs de Go y `core:crypto` que cambian bytes:

| Go | core:crypto | Efecto |
|---|---|---|
| clave privada ed25519 = seed‖pub (64 B) | seed (32 B) | partir al cargar, verificar la mitad pública |
| secreto ECDH NIST = `big.Int.Bytes()` (sin ceros a la izquierda) | ancho fijo | **alimenta HKDF**: hay que recortar o las sesiones no interoperan |
| X25519 acepta salida todo-cero | `ecdh.ecdh` la rechaza | `x25519.scalarmult` directo |
| PSS con salt "auto" | salt explícito | firmar con el máximo; verificar con máximo y con largo del digest |

Verificado contra datos reales: las **4 claves de firma de Portmaster** cargan
y se re-exportan idénticas; firmas JSON hechas por Go verifican; el índice real
`stable.v3.json` de `updates.safing.io` verifica con esas claves (smoke test
opcional).

Un bug del port que encontraron los tests de updates: `core:time` parsea
RFC3339 leyendo **exactamente dos** dígitos de fracción, así que rechaza
`.6Z` y trunca `.1234567Z` en silencio. Go acepta 0–9 dígitos. updates usa su
propio parser estricto; jess todavía no (abierto en `STAGES.md`).

## 5. Dependencias hacia adelante: hooks explícitos

`service/status` necesita `netenv` (etapa 3), `control` necesita la
intercepción del firewall (3) y SPN (5), `broadcasts` necesita geoip (4) y la
cuenta (5), `sync` necesita perfiles (3). En vez de stubs escondidos, cada
paquete tiene un `deps.odin`:

```odin
Deps :: struct {
	// service/netenv (etapa 3). nil = sin conectividad conocida.
	get_online_status:          proc() -> Online_Status,
	add_online_status_callback: proc(cb: proc(user: rawptr), user: rawptr),
	...
}
deps: Deps
```

* `nil` significa "todavía no portado" y el código responde algo razonable:
  503 en los endpoints de updates sin updater, 501 en perfiles de sync.
* Quien lo llena está escrito en el comentario (paquete y etapa).
* Para no crear ciclos, la implementación vive en un **subpaquete puente**:
  `service/updates/coreupdater` implementa `core.Updater` e instala los hooks
  de `configure` en `core.deps`; `updates` no importa `core`.

## 6. Bugs de Go de esta etapa

| Paquete | Bug |
|---|---|
| base/api | `ready` con la condición invertida; `checkAPIKey` hace `key[:4]` (panic con claves cortas); `RecordFunc` chequea el request en vez del record; `handleInsert` con accessor nil; `qsub` filtra la suscripción si falla la query inicial |
| base/metrics | con sólo namespace (el caso de Portmaster) se descartan todas las métricas runtime |
| service/core | devuelve JSON con `Content-Type: application/yaml`; `DataError` serializa `{}` |
| service/broadcasts | borra estados en memoria y nunca los guarda; una entrada null hace panic |
| service/core (app/auth) | sigue esperando al cliente con el prompt ya expirado |
| service/ui | un archivo zip abierto por otra request se pisa y se filtra |
| service/sync | `ImportProfile` usa el `source` opcional del export → rechaza todo `mergedFrom` si falta; `ExportProfile` limpia el path en el perfil equivocado |
| service/updates | `GenerateIndexFromDir` arma URLs con `\` en Windows |

Los que afectan formato o compatibilidad se conservaron (glob con `\` como
separador y escape a la vez, el error envuelto nil de `CheckSHA256`, el
`"/"` después del query string en `redirAddSlash`).

## 7. Lo que quedó afuera, con motivo

* `debug/stack|cpu|heap|allocs`: necesitan el runtime de Go.
* Recuperación de panics por request: Odin no tiene `recover`; un bug en un
  handler tira el proceso.
* `wss://` y permessage-deflate: la UI habla `ws://` por loopback.
* Lectura YAML 1.2 (yaml.v3): el lector actual resuelve como YAML 1.1; los
  exports (de Go y del port) citan los valores ambiguos, sólo afecta YAML
  escrito a mano.
* RSA > 4096 bits y algunos hashes para OAEP/PSS: límites de `core:crypto`.

---

Anterior: [25](25-notificaciones-persistentes-portmaster-notifyd.md).
