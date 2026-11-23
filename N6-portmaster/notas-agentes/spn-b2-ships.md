# Etapa 5 (SPN), ronda B2 — spn/ships — informe

Sin commit. Escrito en `odin/spn/ships` (`pm_spn_ships`) + 2 arreglos mínimos en `lib/http` (abajo). No toqué cabin/terminal. Ojo coordinador: `odin/spn/hub/codec.odin` aparece modificado en el árbol y **no es mío**.

## Archivos
- Port: ship (vtable `Ship`+`Ship_Base`, refcount), tcp, http, http_shared, http_info (template de Go por `#load`), pier (vtable `Pier`+`Pier_Base`), registry, masking, mtu, launch, virtual_network, testship, module, kcp (vacío, como Go).
- Nuevos: errors, conn (conn TCP portable: no bloqueante, poll en rebanadas de 50 ms, deadlines, abort por ctx), docking (cola de `dockingRequests`), **os_windows** (WSAPoll, connect no bloqueante + SO_ERROR, listen dual-stack, keep-alive), **os_linux** (lo mismo con `core:sys/linux`, syscalls reales, sin libc), os_other (stub darwin/BSD).
- Tests: connection, http, http_shared, http_info, testship (Go) + ships_test, ships_windows_test.
- Fuera de alcance, mínimo: `lib/http/server.odin` + `server_use_listener` (servir sobre un socket ya escuchando: dual-stack); `lib/http/poll_linux.odin` nuevo (poll real; antes en Linux `poll_readable` devolvía siempre true → conexiones ociosas bloqueadas en recv y shutdown de 10 s) y `poll_other.odin` pasa a `#+build !windows` + `#+build !linux`.

## Portado vs PORT-TODO
Todo `spn/ships` portado. **Wire = Go**: request del cliente `GET <path> HTTP/1.1` / `Host: ` (vacío) / `User-Agent: Go-http-client/1.1` / `Connection: Upgrade` / `Upgrade: SPN` (orden de Request.Write: Host, UA, headers ordenados); respuesta del pier `HTTP/1.1 101 Switching Protocols` / `Connection` / `Upgrade` / `Date` (lo que escribe chunkWriter al hacer Hijack; sin Content-Length/TE en 1xx), verificado byte a byte en tests. Servidor compartido con los timeouts de Go (MaxHeaderBytes 4096+4096 como net/http). Info page: el `.tmpl` del árbol Go + renderer del subconjunto usado con el escape de html/template (`&#34; &#39; &#43;` …).
- Linux: `_linux.odin` real (no stub); los paquetes de piers/ships usan sólo core:net + os_*. Chequea tipos, **no ejecutado** (sin Linux).
- Deadlines: ships los provee para sus conns (`conn_set_*deadline`, `ship_set_deadline/read/write`); sluice.Conn sigue sin SetDeadline.
- PORT-TODO: os_other (darwin/BSD: poll/connect/listen con core:sys/posix); kcp (Go lo tiene comentado: no se registra); el camino dual-stack `[::]` con IP de bind nil no se ejecuta en tests (escucharía en todas las interfaces → prompt del firewall; tests atan a 127.0.0.1 vía conf); cola con capacidad 0 = 1.

## Hooks
Ninguno (no hay deps hacia paquetes posteriores; sin deps.odin). Para captain/docks: `new_docking_requests(100)` + `docking_requests_recv(q, ctx)`/`try_recv`, `establish_pier`, `launch`, `pier_destroy`, `ship_release`, `set_display_hub_id`, `enable_masking`, `set_virtual_network_config` (copia profunda). Contrato: quien recibe un ^Ship/^Pier libera una ref; hilos concurrentes retienen la suya; sink sólo cierra.

## Tests (`timeout 600 odin test odin/spn/ships -collection:pm=odin -vet -out:$TEMP/spn_ships.exe`)
25 tests, **3/3 OK** (+7 corridas extra OK), ~3 s, sin leaks. `lib/http` 8/8 ×3 OK. `odin check -vet -no-entry-point` limpio en windows_amd64, linux_amd64 y darwin_arm64 (ships y lib/http). Todo loopback, puertos efímeros. Cubre: Go (TestConnections tcp+http ×101 idas/vueltas, SharedHTTP, InfoPageTemplate, UnresponsivePeer deadline/cancel, TestShip) + bytes exactos cliente/servidor, datos pegados al head en ambos sentidos, respuestas inválidas, info page/404, sink desbloquea unload, EOF, deadlines, 4 MB en ambos sentidos a la vez, connection refused, abolish (no acepta, hunde el ship en espera), launch (errores, IPv4, red virtual forzada), cola, masking (vectores Python), load size, keep-alive.

## Bugs de Go
Corregidos: `removeHTTPHandler` invertido (borra el server del mapa con handlers vivos y lo deja tras apagarlo → un add posterior arranca otro server en el puerto; el test de Go pasa por casualidad); `sharedServer.ServeHTTP` corre el handler con el lock exclusivo (serializa el puerto y un upgrade bloqueado en la cola bloquea todo y el remove) → RLock + ref; `Protocols()` siempre nil; piers que abandonan el ship al cancelar (conn filtrada) → sink; bytes tras el head perdidos en el bufio del cliente y el buffer del Hijack descartado → datos iniciales; `Launch` lee Info/VerifiedIPs sin lock.
Conservados: `for sub := range subtract` resta índices → load size 1440/1420 (el crane deriva de ahí su tamaño/padding: cambiarlo cambia los bytes en la red); deadline del handshake da "i/o timeout" (no el error del ctx); Load sobre ship hundido = error de conn cerrada (no ErrSunk; sólo TestShip lo da); Host vacío.

## Trampas
1. Go escribe el 101 recién en `Hijack` (flush del chunkWriter, con Date después de los headers del handler); `lib/http.hijack` no manda nada → respuesta a mano.
2. core:net en Linux: dial sin bind ni timeout → connect no bloqueante propio (bind = conf.get_bind_addr, 30 s, ctx).
3. `":port"` en lib/http = sólo IPv4; Go `ListenTCP(nil IP)` = `[::]` dual-stack → `_os_listen_any` (V6ONLY=0, REUSEADDR en Linux como Go, fallback IPv4).
4. WSAPoll no reporta connects fallidos antes de Win10 2004 → SO_ERROR en cada rebanada.
5. Go activa keep-alive 15 s/15 s/9 en conexiones aceptadas (dialer con -1 no) → `_os_set_keepalive` sólo del lado pier.
6. Test UnresponsivePeer: con deadline de conn = deadline del ctx gana casi siempre la conn ("i/o timeout"), a veces el ctx → aceptar ambos.
7. `or_return` exige resultados nombrados → renombrar locales que los sombrean (-vet).
8. Un `make_*_vtable` "contextless" con `$NAME` sí sirve como inicializador global.
