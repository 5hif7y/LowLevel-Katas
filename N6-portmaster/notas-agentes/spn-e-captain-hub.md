# Etapa 5 (SPN), ronda E — spn/captain + cmds/hub + cliente + e2e local — informe

Sin commit. Nuevo: `odin/spn/captain`, `odin/spn/instance`, `odin/cmds/hub`, `odin/cmds/spnclient`, `odin/tests/spn_e2e`. Fuera, mínimo: **`odin/spn/navigator/pin.odin`** (`new_pin_entity`: con `conf.main_map_scope != .Public`, IPs no globales reciben Location vacía como sin geoip; si no, un hub LAN/loopback queda descartado para todo ruteo). Nada más tocado.

## Archivos
- **captain** (`pm_spn_captain`): module, config, status, exceptions, gossip, op_gossip, op_gossip_query, op_publish, public, piers, hooks, establish, navigation, client, bootstrap, intel, api, deps + captain_test.
- **spn/instance** (`pm_spn_instance`, port mínimo de `spn/instance.go` + lo de cmdbase que hace falta): instance (log → config → db core/cache → runtime → rng → notifications (pipe privado) → metrics → api opcional → cabin, access, terminal, ships, docks, crew, navigator, [patrol], captain), flags (sintaxis Go `-x v`/`-x=v`/bool, duraciones), signals (SIGINT/SIGTERM por libc) + flags_test.
- **cmds/hub**: main.odin + `build_linux.sh`. **cmds/spnclient**: main.odin (gen-zone, gen-intel, connect, serve). **tests/spn_e2e**: spn_e2e_test.odin (`#+build windows`) + `manual_local.sh`.

## Portado vs PORT-TODO
Todo `spn/captain` portado: módulo (prep/start/stop, `spn/enable` y demás opciones con textos Go), hub público (identidad cargar/crear/guardar, home propio, IPs `spn/publicHub/ip4|ip6` manuales, piers por transport, entry policy, exit policy vía cabin, status con lanes/load/net-error, oferta offline al parar), optimizer (bootstrap/desegregación/lanes públicas, retiro de cranes), ops `gossip`/`gossip/query`/`publish` (wire Go: `[varint tipo][datos]`, bloques announcement+status, verificación cabin), hook de crane update, cliente completo (máquina de pasos connect/maintain, home, auth, ping de salud, notificaciones + estados), status `runtime:spn/status` (JSON Go), API `spn/reinit`, bootstrap flags/archivo, intel.
- **Self-hosted**: `access.self_hosted_mode()` = Go `DisableAccount` para usuario/login/tokens, **pero el home terminal se autentica siempre** (token del archivo de zona). Intel: sin updater, captain reaplica el intel local del navigator (bootstrap en mapa vacío, cambios de config) y fija virtual networks.
- Desvíos: captain arranca **último** (Go antes que crew/docks/terminal/ships); sin ubicación del dispositivo se busca home sin ella (Go: error); `use_device_location`/`-device-location=false` evita el traceroute de netenv; `set_api_authenticator` (tests); Hub API desactivada por defecto (`-api-address`; Go 127.0.0.1:817 choca con un Portmaster real).
- PORT-TODO(stage-6): QuickSettings de las reglas + `UpdateConfigQuickSettings` (navigator sin FlagID); updater de intel (hook); cmdbase completo (servicio/systemd, restart, `update`/`version`, `-bin-dir`, print-stack); netenv en Linux sigue stub (IP manual); el hub Linux **compila/linkea pero no se ejecutó** aquí.
- **observation-hub** (no portado, no requerido): hub "observador" que se conecta a la red, vigila en el mapa los announcements/status de los hubs y manda avisos Apprise (online/offline, cambios) — herramienta de monitoreo de operadores; depende de captain + base/apprise (ya portado).

## Hooks
Nuevos: `captain.deps.get_intel_update` (service/updates "main-intel.yaml", instancia etapa 6; nil = intel local), `captain.trigger_spn_intel_update` (callback del updater, 6). Listos para llenar por la instancia (6): `broadcasts.deps.get_spn_status_json` = `captain.get_spn_status_json`, `core.deps.captain_add_to_debug_info` = `captain.add_to_debug_info`, `captain.Instance.net_env` (evento network change). Usados: `docks.register_crane_update_hook`, `crew.enable_connecting`, `crewhooks.install_navigator_hooks` (spnclient).

## Tests (`export PATH=…vcpkg…/bin`; `timeout 600 odin test odin/<pkg> -collection:pm=odin -vet -out:$S/t/x.exe > log 2>&1`, ×3 en primer plano)
- **captain 6/6 ×3** (2,7–5,8 s): hub público real en 127.0.0.1 (identidad guardada, home, pier, bootstrap entry, opciones), publish (Explicit_Ack, crane público y asignado, pin en el mapa, lane en el status), gossip op registrado en ambos extremos, gossip query, relay, wire gossip, status (JSON golden, override, provider runtime), bootstrap file (`J{"Main":{"Hubs":[…]}}`, vacío/roto), excepciones.
- **instance 1/1 ×3**, **navigator 23/23 ×3** (tras el parche), **crew/hooks 2/2 ×3**.
- **e2e 1/1 ×3** (37–44 s con exes pre-compilados `-define:PM_SPN_HUB_EXE=… -define:PM_SPN_CLIENT_EXE=…`; sin defines compila ambos, ~75 s): zona → hub A solo: home + auth + echo 64 KiB+10 msgs y HTTP GET (exit = A); **token equivocado → exit 3, "permission denied"** (hub: "invalid access token"); hub B: lanes A↔B, **cliente→A→B→HTTP y echo** (TUNNEL exit = B, path = A,B, alg double-hop). Sin procesos ni dirs temporales al final (kill por handle).
- `VET=1 bash odin/tests/crosscheck.sh windows_amd64 linux_amd64 -- odin/spn/captain odin/spn/instance odin/cmds/hub odin/cmds/spnclient odin/spn/navigator odin/spn/crew odin/spn/crew/hooks odin/spn/docks` → 16/16 ok (+darwin_arm64 ok).

## Procedimiento manual local (lo mismo corre `bash odin/tests/spn_e2e/manual_local.sh [dir]`)
```
odin build odin/cmds/hub -collection:pm=odin -out:spn-hub.exe
odin build odin/cmds/spnclient -collection:pm=odin -out:spnclient.exe
spnclient gen-zone -zone selftest -tokens 4 -hub-file hub-zone.json -client-file client-zone.json
C="-spn-map selfhosted -spn-map-scope test -self-hosted-zone-file hub-zone.json -patrol=false -device-location=false -allow-local-destinations -optimize-delay 2s -log-stdout"
A="-data-dir hubA -name hub-a -ip4 127.0.0.1 -bind-ip 127.0.0.1 -transports tcp:47121"
B="-data-dir hubB -name hub-b -ip4 127.0.0.2 -bind-ip 127.0.0.2 -transports tcp:47122"
spn-hub $A $C -init ; spn-hub $B $C -init        # → hubX/hub-bootstrap.json, HUB_ID
spnclient gen-intel -out hubs-intel.json -bootstrap hubA/hub-bootstrap.json,hubB/hub-bootstrap.json
spnclient gen-intel -out client-intel.json -bootstrap hubA/hub-bootstrap.json -trusted hubA/hub-bootstrap.json,hubB/hub-bootstrap.json
spnclient serve -echo 127.0.0.1:47199 &   spnclient serve -http 127.0.0.1:47198 &
spn-hub $A $C -intel-file hubs-intel.json &
CL="connect -zone-file client-zone.json -intel-file client-intel.json -spn-map selfhosted -spn-map-scope test"
spnclient $CL -routing home -echo 127.0.0.1:47199              # SPN_READY / TUNNEL exit=A / ECHO_OK 65636
spnclient $CL -routing home -http-get http://127.0.0.1:47198/x # HTTP_STATUS 200
spnclient connect -zone-file wrong-client.json … -echo …       # exit 3 (token rechazado)
spn-hub $B $C -intel-file hubs-intel.json &                    # esperar "current lanes: [<…" en ambos
spnclient $CL -routing double-hop -http-get http://127.0.0.1:47198/x   # TUNNEL exit=B path=A,B
```
Matar los procesos por PID. Otros modos del cliente: `-pipe IP:PORT` (stdin/stdout), `-forward 127.0.0.1:8080=IP:PORT`.

## Build/link Linux (VPS Rocky 9; no accedí al VPS)
```
odin build odin/cmds/hub -collection:pm=odin -target:linux_amd64 -build-mode:obj -use-single-module -reloc-mode:pic -o:speed -out:spn-hub.o
gcc -o spn-hub spn-hub.o -lssl -lcrypto -lsqlite3 -lpthread -lm
```
(= `bash odin/cmds/hub/build_linux.sh [out]`; en Linux con gcc también linkea). Libs por los símbolos indefinidos del .o: libssl/libcrypto (patrol, OpenSSL 3), libsqlite3, libpthread (en glibc ≥2.34, inocuo), libm (sin/cos/exp/pow), libc; **sin -ldl**. Paquetes: gcc, openssl-devel, sqlite-devel (o `-l:libssl.so.3 -l:libcrypto.so.3 -l:libsqlite3.so.0`). Ejemplo VPS: `spn-hub -data-dir /opt/spn-hub -ip4 <IP pública> -transports tcp:17 -self-hosted-zone-file hub-zone.json -intel-file intel.json` (puerto <1024 requiere root; data dir por defecto /opt/safing/spn).

## Bugs de Go
Corregidos: EstablishPublicLane envuelve el `err` nil en vez de tErr y no para el crane si NewPublishOp falla; tras UpdateUser el cliente vuelve a mirar el usuario viejo → se relee; callbacks ("update SPN intel", "update public identity", network change, patrol) se agregan en cada start → una vez; gossipRelayMsg envía bajo RLock (1 s por op) → refs y envío fuera del lock; ship denegado por entry policy no se hunde → sink; ConnectedHub reasignado sin lock (TODO de Go) → swap bajo lock, viejo retenido.
Conservados: bootstrap file IPv6 entre corchetes (ParseBootstrapHub no lo parsea); los dos hubs crean lane uno hacia el otro en bootstrap; publishShutdownStatus duerme 2 s.

## Trampas
1. `os.File` de core:os como stdout de un hijo → la salida se pierde (archivo vacío) → pipe + hilo lector.
2. Hubs con la misma IP se reemplazan (Superseded) → B en 127.0.0.2 (loopback en Windows sin configurar).
3. Scope Public rechaza 127.0.0.1 al validar el announcement → `-spn-map-scope test`; y el navigator no daba ubicación a IPs no globales → parche en pin.odin.
4. Navigator `own_location_fn` (default netenv) hacía traceroute a Internet desde el hub → `-device-location=false`.
5. `tm.new_empty_msg` sin el scheduler de unit → segfault en un test sin begin.
6. servicetest ya tiene authenticator → `Authentication_Already_Set`.
7. `-build-mode:obj` genera un .o por paquete → `-use-single-module`; `-reloc-mode:pic` para el PIE de gcc.
8. Heredoc+python convierten `\r\n`/`\n` de fuentes Odin en saltos reales → reparado con `chr(92)`.
9. Los test hubs de docks toman la identidad de la config pública del proceso (misma IP/transport que captain).
