# lib/tls (OpenSSL) + lib/httpclient (HTTP/1.1 propio) + lib/http poll — ronda unix

Fork `DEV/gitclones/portmaster`, rama `port/odin` (base 32c3f2b3). Sin commit.

## Qué quedó

- **lib/tls en unix = OpenSSL 3** (`foreign import "system:ssl"` / `"system:crypto"`; en WSL linkea `libssl.so` → **OpenSSL 4.1**, sólo uso funciones que siguen en 3 y 4). Misma API y semántica que SChannel:
  - El `SSL*` trabaja sobre **dos BIO de memoria**; los bytes entre BIO y socket los movemos nosotros → socket no bloqueante + `poll(2)` en rebanadas de 50 ms, re-chequeando close()/deadline/ctx (nunca SO_RCVTIMEO), igual que Windows. Locks: `read_mu`, `write_mu` (también drena el BIO de salida: los records salen en orden), `ssl_mu` (toda llamada a OpenSSL; nunca tomado mientras se espera el socket).
  - Validación como Go: `SSL_CTX_set_default_verify_paths` + `SSL_VERIFY_PEER`; nombre con `SSL_set1_host` y flags `NEVER_CHECK_SUBJECT | NO_PARTIAL_WILDCARDS` (**sólo SANs, como Go ≥1.15; SChannel todavía acepta el CN**); IP literal → `X509_VERIFY_PARAM_set1_ip_asc` y **sin SNI** (Go no manda SNI a una IP). Pin: verify callback que acepta todo error de cadena salvo hostname/IP mismatch, y después SHA-256 del DER de la hoja. `insecure_skip_verify`: nada. Mapeo: `SSL_R_CERTIFICATE_VERIFY_FAILED` + verify result 62/64 → `.Name_Mismatch`, otro → `.Certificate_Invalid`, resto `.Handshake_Failed`.
  - ALPN (`SSL_set_alpn_protos` / `SSL_get0_alpn_selected`; servidor con `SSL_select_next_proto` en orden del servidor), `min_version` por SSL, `tls_version` = `SSL_version`, close_notify con `SSL_shutdown` best effort 200 ms, nunca después de un error fatal.
  - Un `SSL_CTX` cliente por proceso (raíces cargadas una vez); todo lo demás por conexión.
  - Certificados de cliente (`keypair_other.odin`): `d2i_X509` (+ cadena), `d2i_AutoPrivateKey` del PKCS#8, `X509_check_private_key`; **RSA, ECDSA y Ed25519 funcionan** (en Windows sólo RSA). Ed25519 ya no da el texto de "not supported" fuera de Windows.
  - Servidor de test (`server_other.odin`): misma API (`create_self_signed`, `server_credentials(ss.cert, alpn, allow_tls13, require_client_cert)`, `accept`), clave RSA en memoria (una por proceso, cada cert único por serial), cert con SAN DNS/IP. Nada toca disco ni stores. `https_server_windows.odin` pasó a **`https_server.odin` portable** (sólo usaba la API pública).
  - `dial_other.odin`: connect no bloqueante + poll + `SO_ERROR`, bind local → `.Bind_Failed` con `net.Bind_Error`, keep-alive (`TCP_KEEPIDLE/INTVL` Linux, `TCP_KEEPALIVE/INTVL` darwin), `poll_socket(posix.FD, …)` y `endpoint_to_sockaddr` públicos como en Windows.
  - darwin: mismo código; linkea OpenSSL de Homebrew con `-extra-linker-flags:"-L$(brew --prefix openssl@3)/lib"` (comentado en `openssl_other.odin`). Sólo type-check (no hay Mac).
- **lib/httpclient en unix** (`client_other.odin`): cliente HTTP/1.1 propio sobre lib/tls, con el contrato de WinHTTP/Go: pool keep-alive por Client (2 por host, 90 s, reintento único si la conexión reusada estaba muerta y el request se puede repetir), Host/User-Agent/Content-Length según `shouldSendContentLength` de Go, chunked de subida, `Accept-Encoding: gzip` con la regla de Go y decodificación transparente (quita Content-Encoding/Content-Length, `content_length = -1`), 1xx salteados, HEAD/204/304 sin cuerpo, cuerpo por chunked/Content-Length/EOF, redirects con `redirectBehavior` de Go (301/302/303 → GET sin cuerpo; 307/308 conservan método y cuerpo si es repetible, si no devuelven el 3xx), headers sensibles fuera al cambiar de host, `.Too_Many_Redirects`, `Follow_No_Downgrade`, `Never`; Basic auth desde la URL; proxies: `HTTP(S)_PROXY`/`NO_PROXY` con las reglas de `httpproxy` (dominios, `*.`, puertos, IPs, CIDR, loopback nunca) para `.Environment`/`.System`, `.Named` con bypass estilo WinHTTP; http por absolute-form, https por CONNECT + `tls.wrap` (nuevo `tls.plain_detach`, sólo unix). Timeouts: `Client.timeout` = ctx hijo que va pegado a la Conn (cancelación ≤ 50 ms), `read_timeout` por operación, `connect_timeout` cubre connect + handshake. Errores mapeados a los mismos `Error_Kind` (TLS → `.TLS_Failed`, EOF/reset → `.Connection_Error`, ctx → `.Canceled`/`.Timeout`). Sin HTTP/2.
- `Request_State` pasó de `client.odin` a cada plataforma (texto idéntico en `client_windows.odin`). `error_string` dice "winhttp error" sólo en Windows.
- **lib/http/poll_other.odin**: `poll(2)` de `core:sys/posix` (darwin/BSD).

## Cambios que tocan Windows (mínimos, con tests 3×)

- `server_windows.odin` `accept`: si falla, ya **no cierra el socket** (los 3 llamadores lo cierran → doble `closesocket`, que con tests paralelos puede cerrar un socket ajeno). Unix igual.
- `tls_test.odin`: `s.alpn` guardaba el string de una Conn destruida (use-after-free; en Windows no se veía) → clon en heap.
- `test_https_online` (httpclient) iba a Internet siempre → saltado salvo `PM_HTTPCLIENT_SMOKE=1`.
- `Test_Key_Kind` movido a `keypair.odin`; `tls_test`, `keypair_test`, `mtls_test` (ex `mtls_windows_test`) portables; las diferencias por OS (ECDSA, texto de ENOENT) con `when`.

## Bug encontrado por el flake del resolver (corregido, también en Windows)

`service/resolver test_tcp_resolver_dial_retries_on_bind_error` fallaba con `connection failed (0x6f)` bajo carga (reproducido 15/40 con 4 instancias en paralelo; 0/25 en serie). Instrumentando una copia del mirror: en el momento del rechazo **un dial nuevo sin bind al mismo puerto también era rechazado** y el fd 0 ya era el propio socket del dial → el listener del test (que había recibido **fd 0**) había sido cerrado por otro.
Causa: `dial_tcp` (dial_other **y dial_windows**) tenía `sock` como **resultado con nombre** y `defer if !ok do net.close(sock)`; `return {}, err` pone `sock = 0` antes de correr el defer → todo dial fallido (p. ej. el bind fallido del primer intento) **cerraba el fd 0 y filtraba el socket real**. En unix el primer fallo cierra stdin y desde ahí fd 0 es un socket vivo de otro test. Arreglo: copia local (`owned := sock`) en el defer. Después: 0/40 con la misma carga. Go no reintenta un ECONNREFUSED; no hacía falta tocar el connect/poll/SO_ERROR.
Queda en service/resolver (fuera de alcance): `test_tcp_resolver_reconnects` falla ~4/40 bajo carga con "query q3: query failed": después de q2 (servidor `Close_After_One`) el test pasa a `.Answer` y pregunta q3 sin esperar a que la conexión de q2 quede `abandoned`, así q3 puede salir por la conexión que el servidor está cerrando (EOF). Arreglo sugerido: repetir el `for … abandoned` también después de q2.

## Tests

- **Linux** (WSL, `PM_MIRROR=/root/pm/agent-tls`, 3 corridas cada uno): lib/tls 20/20 ×3, lib/httpclient 23/23 ×3, lib/http 8/8 ×3. Sin "bad free"/leaks.
- **Windows** (3 corridas, `-vet`): lib/tls 17/17 ×3, lib/httpclient 14/14 ×3, lib/http 8/8 ×3.
- `VET=1 bash odin/tests/crosscheck.sh windows_amd64 linux_amd64 darwin_arm64 -- odin/lib/tls odin/lib/httpclient odin/lib/http`: 0 fallas.
- Tests nuevos unix: `tls_other_test.odin` (TLS 1.3 + min_version, nombre IP con SAN IP, cert sólo-CN rechazado como Go, Conn plano deadline/EOF), `client_other_test.odin` (pool keep-alive y conexión vieja reintentada, cuerpo hasta EOF, HEAD, 204, 1xx, cuerpo corto → `.Connection_Error`, 307 repite el cuerpo / 303 → GET / cuerpo stream no repetible devuelve el 307, `resolve_reference`, reglas NO_PROXY y bypass, proxy http + CONNECT con auth Basic, validación HTTPS, read_timeout). Portables que ahora corren en Linux: tls_test, keypair_test (mTLS con RSA **y ECDSA**), mtls_test (httpclient con cert cliente).
- Smoke reales (saltados por defecto): `PM_TLS_SMOKE=1` (DoT 1.1.1.1), `PM_HTTPCLIENT_SMOKE=1` (https://updates.safing.io).

**Consumidores en Linux** (1 corrida cada uno, sin editarlos): base/apprise 3/3 ok · base/metrics 6/6 ok · service/updates 15/15 ok · spn/access 14/14 ok · spn/access/account 5/5 ok · structures/dsd 20/20 ok · cmds/trafficgen 4/4 ok · cmds/updatemgr 8/8 ok · cmds/testsuite 1/1 ok · **cmds/observation-hub: cuelga** (timeout, 5 tests sin terminar). Causa fuera de alcance: `testutil_test.odin` `fake_stop` despierta al hilo bloqueado en `accept()` con `net.close(f.sock)`; en Windows eso lo despierta, en Linux no → `thread.join` eterno. Con `net.shutdown(f.sock, .Both)` antes del close (probado sólo en una copia del mirror) pasan 8/8, incluidos los de apprise por el httpclient nuevo. Además base/apprise y observation-hub tienen sus tests mTLS como `*_windows_test.odin`: ahora podrían ser portables (lib/tls tiene servidor HTTPS y certs de cliente en unix).

Nota de entorno: `sync.sh` (tar `--keep-newer-files`, errores a /dev/null) dejó de copiar archivos editados después de un `rm -rf` del mirror; mi runner copia explícitamente lib/tls, lib/httpclient y lib/http antes de compilar.

## Trampas

- `defer if !ok do close(x)` con `x` resultado con nombre: `return {}, err` lo pone en cero antes del defer → se cierra el handle 0 (ver arriba). Usar una copia local.
- fd 0 no es "inválido" en unix: un listener puede recibir fd 0 (p. ej. si alguien cerró stdin); guardas `if sock != 0` y valores cero por defecto mienten.
- Linux: `close()` de un socket en escucha no despierta un `accept()` bloqueado en otro hilo (Windows sí) → `shutdown` antes.

- `c` como parámetro `^Conn` tapa el paquete `core:c` → `import cc "core:c"`.
- `net.send_tcp` en Linux devuelve `(escritos, .Would_Block)` con escritura parcial: el patrón de Windows (`continue` ante Would_Block) perdería bytes → `posix.send` directo una vez por espera.
- `.EAGAIN, .EWOULDBLOCK` en el mismo `case`: duplicado (mismo valor) → error de compilación.
- `uint(len(data) - sent)` dentro de un argumento `c.size_t` hace que Odin infiera `uint` para `len(...)` → slice primero y `uint(len(left))`.
- `accept` + caller cerrando el socket = doble close (ver arriba): en unix es peor (reuso de fd inmediato).
- OpenSSL: la cola de errores es por hilo → `ERR_clear_error()` antes de cada llamada de I/O (si no, `SSL_get_error` ve errores viejos).
- Pin + `SSL_VERIFY_NONE` no sirve para chequear el nombre (la verificación corta en el primer error de cadena) → callback que sólo falla en 62/64.
- Pool + servidor de test que atiende una conexión a la vez: la conexión ociosa del pool bloquea al servidor → servidor de test keep-alive con un hilo por conexión y `shutdown` de las activas al parar.
