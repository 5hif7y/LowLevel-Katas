# Etapa 5 (SPN), ronda A1 — spn/{unit,conf,hub,patrol,sluice} — informe

Sin commit. Nada fuera de `odin/spn/{unit,conf,hub,patrol,sluice}` tocado (ningún arreglo en paquetes ya portados).

## Archivos (`odin/spn/…`, paquetes `pm_spn_<pkg>`)
- **unit**: doc, scheduler, scheduler_stats, unit, unit_debug, `sleep_windows` (timer de alta resolución) / `sleep_other` (time.sleep), unit_test.
- **conf**: map, mode, networks, version, conf_test.
- **hub**: hub, status, measurements, format, transport, `url` (subconjunto fiel de net/url: Parse/RequestURI/EscapedPath/Hostname/Port), `codec` (JSON/CBOR Go-compatibles a mano), database, update, intel, intel_override, truststores, errors + tests: hub, format, transport, codec, update, intel.
- **patrol**: domains (291 dominios), http (flujo + request/status line portables), `https_windows` (lib/tls), `https_linux` (OpenSSL libssl real), `https_other` (stub), https_common, module + tests (domains_test, `http_windows_test`).
- **sluice**: conn (interfaz Conn/Listener, TCP), udp_listener (Packet+UDP en una implementación), `udp_windows` / `udp_other`, sluice (+sluices +request), module, errors + tests (sluice_test, `sluice_windows_test`).

## Portado vs PORT-TODO
Todo portado, incluido Go test por test (format_test, transport_test, hub_test, update_test, unit_test, scheduler_test como test corto, domains_test con TestCleanDomains detrás de `-define:PM_PATROL_DOMAIN_TOOLS=true`, red real, saltado).
- **Byte-compat**: Announcement/Status en dsd JSON (`'J'`+JSON: orden de struct, nombres Go, omitempty, HTML-escape, net.IP como texto, mapas ordenados, base64 std) y CBOR (nombres cbor, cabeceras mínimas, net.IP 16 bytes como ParseIP; decodifica 4/16). Goldens derivados de las reglas (no hay Go): JSON completo/vacío, CBOR byte a byte, Status JSON, registro Hub JSON (float32 como AppendFloat 'f'/'e' 32). Firma/apertura con jess real (TOFU con clave contrabandeada en `Keys`), `createHubID` verificado = BLAKE2b-256(bloque(scheme)‖bloque(key)) base58. Registro Hub en sqlite: put → wrapper → unwrap; carga un registro con forma Go (offset local en tiempos, nombres en minúsculas).
- **Linux**: unit, conf, hub, sluice son portables (core:net/sync/time). patrol (corre sólo en hub público) tiene **implementación Linux real** con OpenSSL (`system:ssl`: TLS ≥1.2, roots del sistema, SNI, SSL_set1_host, SO_SNDTIMEO/RCVTIMEO) — chequea tipos, **no ejecutado** (sin Linux).
- PORT-TODO: `-spn-map` flag (cmdbase, etapa 6; `conf.set_main_map_name`); sluice Linux sin IP_PKTINFO/WriteMsgUDP con OOB (`udp_other`, sólo cliente); patrol macOS (`https_other`); Conn sin SetDeadline (para crew/ships, etapa 5); Hub sin get_accessor (nada lo usa aún); Hub record sólo JSON (Go siempre JSON).

## Hooks
Ninguno nuevo en deps.odin: no hay dependencias a paquetes posteriores. `hub.set_navigator_access` (Go ya es setter; lo llena navigator). **Pendiente para la ronda de crew**: llenar `firewall.deps.sluice_await_request` con `sluice.await_request(conn, crew.handle_sluice_request)`.

## Tests (`export PATH=…vcpkg…/bin`; `timeout 600 odin test odin/spn/<pkg> -collection:pm=odin -vet -out:$TEMP/spn_<pkg>.exe > log 2>&1`, ×3)
unit 4 · conf 1 · hub 24 · patrol 6 · sluice 6 → **3/3 OK cada uno**, sin leaks ni errores en logs. TestUnit con los parámetros de Go (1 000 000 unidades, 100 hilos): ~2 s. hub: 0,8 s (una corrida 25 s: primer arranque/limpieza de dirs de test). `VET=1 bash odin/tests/crosscheck.sh windows_amd64 linux_amd64 darwin_arm64 -- odin/spn/{unit,conf,hub,patrol,sluice}`: 15/15 ok. Sólo loopback (TLS local autofirmado por pin, sockets efímeros). Deja `%TEMP%\pm-spn-hub-test-<pid>` (limpiado por remove_stale_test_dirs).

## Bugs de Go
Corregidos: `Announcement.Copy` pierde Group; nil-deref de Info/Status en getName, Obsolete, GetSignet, EnsureHub (wrapper sin Info); Apply* escribe InvalidInfo/InvalidStatus después de soltar el lock (carrera) → bajo lock; AddLane/RemoveLane mutan el slice compartido → copy-on-write del Status; `removeSluice` por nombre podía sacar un sluice reiniciado; unidad que pierde un slot si se anuncia entre chequeo y espera; división por cero del skew con slot de 0 ns.
Conservados: `getRandomTestDomain` nunca elige el último; `Measurements.Copy` no copia GeoProximity; `FlagsEqual` ordena in place; Timestamp de Info actualizado antes de validar; Key null en Status (Go: panic en validación) → clave vacía.

## Trampas
1. Sleep de Windows (1–16 ms) en slots de 5 ms: el skew hacía bajar el pace siempre → pace clavado en 100, TestUnit 3,5 min → timer de alta resolución (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`), 2 s.
2. Canales cerrados como broadcast de slot → contador de generación + futex.
3. Write convierte `<` en `<` también dentro de raw strings → goldens con `~` reemplazado en runtime.
4. `#+build !windows !linux` es inválido → dos líneas `#+build`.
5. `[]u8("...")` global no compila → `transmute([]u8)string(CONST)`.
6. UDP en Windows con buffer menor al datagrama → WSAEMSGSIZE (el test leía 64 B del mensaje de 105).
7. Cert del servidor de test vs dominios aleatorios del chequeo periódico → opción `server_name` (sólo tests).
8. core:net no permite bind+connect TCP → test Windows con Winsock para fijar el puerto origen (clave de AwaitRequest).
9. `delete_key` devuelve (k, v) sin ok; `atomic_add` devuelve el valor viejo (refcounts `!= 1`).
10. GC → refcount: Hub (record), Announcement/Status (inmutables tras publicar), Sluice, UDP_Listener, UDP_Conn; Measurements inmortales (como el registro de Go).
