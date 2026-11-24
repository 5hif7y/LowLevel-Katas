# Ronda unix — sockets: resolver, splittun(+proxy), spn crew/ships/sluice, interop/ivpn, compat — informe

Sin commit, sin cambio de rama. Escrito sólo en mi alcance. Mirror WSL: `/root/pm/agent-sock`.

## Archivos
- **service/resolver**: `sockets_other.odin` = implementación unix real con `core:sys/posix` (poll(2), connect(2) del socket UDP con `bind_err` para EADDRINUSE/EADDRNOTAVAIL/EACCES/ENOBUFS, `IP_MULTICAST_LOOP`=0 + `IP_ADD_MEMBERSHIP` / `IPV6_MULTICAST_LOOP` + `IPV6_JOIN_GROUP`; números IPv4 por OS: Linux 34/35, macOS/BSD 11/12; `sin_len` fuera de Linux). Tests: `transports_test.odin` deja de ser `#+build windows` (poll → `socket_poll` del paquete, MSG_PEEK → `test_peek` en `transports_{windows,other}_test.odin`).
- **service/splittun**: `proxies.odin` en Linux escucha UDP en `127.0.0.17` / `::1` (constantes `PROXY_LINUX_UDP_IP4/6`, Go: `runtime.GOOS == "linux"`). `splittun_test.odin` portable (poll en `splittun_poll_{windows,other}_test.odin`; interfaz de loopback por OS; `return` si no hay respuesta UDP — antes `recv_udp` bloqueaba para siempre).
- **splittun/proxy**: `proxy_other.odin` = posix (poll, `udp_listen`, `udp_dial(remote, local, iface)`, send/recv conectados, `udp_send_to`, `dial_tcp_bound`); `bind_linux.odin` nuevo (`SO_BINDTODEVICE`=25, applyBindToDevice); `bind_other.odin` = `#+build !linux` (no-op, como bind_other.go); `tcp_proxy.odin`: `dial_upstream` (Linux + interfaz → dial propio + `tls.plain_conn`; si no `tls.dial_plain`); `udp_proxy.odin` pasa la interfaz a `udp_dial`; `udp_windows.odin` acepta (e ignora) `iface`. Tests: `bind_linux_test.odin` (sólo `lo` y un nombre inexistente), `proxy_test.odin` usa `lo` en Linux.
- **spn/ships**: `os_linux.odin` borrado; `os_other.odin` (`#+build !windows`) = una sola implementación posix para Linux y macOS (poll, socket+fcntl CLOEXEC/NONBLOCK, connect EINPROGRESS + SO_ERROR, listen dual-stack V6ONLY=0 + REUSEADDR, keep-alive 15/15/9 con números TCP_KEEP* por OS). Test nuevo `ships_other_test.odin` (keep-alive del pier, no del dial; en Linux idle/intvl/cnt exactos).
- **spn/crew**: `udp_dial_linux.odin` borrado; `udp_dial_other.odin` = posix para todo unix. (crew/hooks sin cambios.)
- **spn/sluice**: `udp_other.odin`: Linux `IP_PKTINFO` + `IPV6_RECVPKTINFO` (error sólo si fallan ambos, como Go), `read_msg_udp` (recvmsg con control), `write_msg_udp` (sendmsg con el OOB del primer datagrama); `udp_listener.odin` usa `read_msg_udp` y guarda el OOB en la conn; `udp_windows.odin` `read_msg_udp` = recv_udp. Tests: `udp_pktinfo_linux_test.odin` (IPv4/IPv6 loopback: cmsg IP_PKTINFO con spec_dst/addr, respuesta desde la dirección de escucha; listen_packet sin OOB), `sluice_other_test.odin` (hand-over TCP con bind+connect posix).
- **service/interop/ivpn**: `client_other.odin` = getConnectionFiles de ivpn/desktop-app (`file_locations*.go`: Linux `/etc/opt/ivpn/mutable`, si no existe y hay snap `/var/snap/ivpn/common/opt/ivpn/mutable`, si no error; macOS `/Library/Application Support/IVPN`; `port.txt` + `eaa`) + socket loopback posix (connect no bloqueante con timeout, SO_NOSIGPIPE en macOS). `hook_linux.odin` nuevo = hook_linux.go completo; `hook_other.odin` → `hook_default.odin` (`#+build !windows` `#+build !linux` = hook_default.go). `deps.odin`: hook `spn_connecting_hub`. Tests: `ivpn_test.odin` portable (poll en `ivpn_poll_{windows,other}_test.odin`; el paso del hook SPN se espera por OS), `hook_linux_test.odin`.
- **service/compat**: `compat_other.odin`: debug info Linux (cadenas de iptables), paso 1 del self-check (socket RAW proto 61, necesita root como Go), paso 2 (getaddrinfo). `iptables_linux.odin` nuevo (iptables.go + la parte usada de go-iptables). `deps.odin`: `iptables_look_path` / `iptables_run`. Tests: `test_debug_info_wfp_state` → `compat_windows_test.odin` (`#+build windows`); `compat_linux_test.odin` (salida grabada, jamás se ejecuta iptables); `test_install_hooks_debug_info` portable.

## Portado vs PORT-TODO
Todos los `PORT-TODO(unix)` del alcance resueltos. Quedan:
- `PORT-TODO(darwin)` en `spn/sluice/udp_other.odin`: x/net/ipv4 en macOS usa IP_RECVDSTADDR + IP_RECVIF y Go devuelve ese OOB tal cual a sendmsg; macOS sólo acepta IP_SENDSRCADDR al enviar (y lo rechaza con socket atado a IP concreta) y IP_RECVIF no vale al enviar → sin Mac no se puede verificar qué hace Go; macOS lee/escribe sin OOB (correcto escuchando en IP concreta).
- `PORT-TODO(6, instance)` en `ivpn/deps.odin`: llenar `deps.spn_connecting_hub` (ver Hooks).
- SO_BINDTODEVICE, IP_PKTINFO y la política de rutas/nft de IVPN son Linux-only por naturaleza (Go igual).

## Hooks
- `ivpn.deps.spn_connecting_hub: proc() -> (ipv4, ipv6: net.Address, ok: bool)`: IPs del hub al que conecta el SPN (Go las recibe en el hook; `captain.add_spn_connecting_hook` de Odin no pasa el Announcement). Lo debe llenar la instancia (`service/instance.odin`, etapa 6) desde el home hub del captain. nil = hub desconocido → no se instala ruta al hub (las viejas sí se limpian).
- `compat.deps.iptables_look_path` / `iptables_run` (nil = real: PATH + `os.process_exec`).
- ivpn `Hook_Env` por instancia (`i.extra.env`, nil = `default_hook_env`: runner real, `netenv.gateways_info`, getifaddrs, config `spn/enable` y `splittun/enable`). Los tests de sesión dejan `default_hook_env` inerte; los del hook usan uno grabador propio.

## Tests (resultados en la sección final)
Linux: `wsl … env PM_MIRROR=/root/pm/agent-sock … sh lt.sh <pkg>` ×3. Windows: `timeout 600 odin test odin/<pkg> -collection:pm=odin -vet -out:… > log` ×3. Cross-check: `VET=1 bash odin/tests/crosscheck.sh windows_amd64 linux_amd64 darwin_arm64 -- <9 dirs>`.

Linux (antes → ahora, ×3 salvo nota):
- resolver: 24 tests (transports Windows-only; `test_udp_transport_check_failing_resolvers` fallaba) → **46** tests (incluye DoT/DoH sobre la lib/tls OpenSSL del otro agente, ya presente): 3/3 + 8 corridas extra OK salvo **1 flake en 11**: `test_tcp_resolver_dial_retries_on_bind_error` → `dial tcp 127.0.0.1:…: connection failed (0x6f = ECONNREFUSED)` al server de test (dial TCP de lib/tls `dial_other.odin`, no tocado por mí; no reproducido en 8 corridas seguidas). Para el coordinador / agente de lib/tls.
- splittun: 0 (Windows-only) → 4 tests 3/3. splittun/proxy: 13/17 fallando → 19 tests 3/3 (TLS plano de lib/tls ya funciona en Linux).
- spn/ships 25 ×3, spn/crew 11 ×3, crew/hooks 3 ×3, spn/sluice 5 → 8 ×3, interop/ivpn 0 → 16 ×3, compat 14 (1 fallo WFP) → 18 ×3.
Windows ×3 (≥3 corridas con el código final): resolver 46, splittun 4, proxy 17, crew 11, crew/hooks 3, ships 25, sluice 6, ivpn 8, compat 15 → todo OK.
Cross-check `windows_amd64 linux_amd64 darwin_arm64` de los 9 dirs: **0 fallas**.

## Bugs de Go
- Conservados (fidelidad): hook_linux.go incluye en los gateways rutas por defecto con gateway 0.0.0.0 (wg-quick) y sólo las descarta por la heurística de IP de la interfaz; con más de un default → error. `wgLocalIP` de iptables sólo se recuerda si el insert anduvo. Error de iptables de go-iptables (`running [...]: exit status N: <stderr>`), versión sin `--wait` < 1.4.20. En Linux un ICMP port unreachable en el socket UDP conectado del proxy es error (Go sólo lo apaga en Windows).
- No reproducido: go-iptables toma el lock de xtables cuando el binario no tiene `--wait` (iptables < 1.4.20, 2013); un listado no lo necesita.

## Trampas
1. `lt.sh` sincroniza sólo archivos nuevos: **los archivos borrados/renombrados quedan en el mirror** (os_linux.odin, udp_dial_linux.odin, hook_other.odin → redeclaraciones) → borrarlos a mano en `/root/pm/<mirror>`.
2. `TaskStop` de un Bash en background no mata los `wsl.exe` hijos ni el `lin.sh`: quedaron dos corridas pisando los mismos logs (y binarios colgados en WSL) → matar por línea de comando (Win32_Process) y dentro de WSL.
3. Nombre de interfaz: los tests Windows usaban `"Loopback"` como interfaz del binding; en Linux eso es `SO_BINDTODEVICE("Loopback")` → ENODEV → el dial falla (correcto como Go) → en Linux `"lo"`.
4. `posix.setsockopt` toma `Sock_Option` (enum): opciones que posix no declara (IP_ADD_MEMBERSHIP, SO_BINDTODEVICE, TCP_KEEP*, IP_PKTINFO) → `posix.Sock_Option(n)` con el número por OS.
5. macOS no tiene `SOCK_CLOEXEC|SOCK_NONBLOCK` en socket(2) → fcntl(F_SETFD/F_SETFL); `sockaddr_in` lleva `sin_len` fuera de Linux.
6. `net.UDP_Recv_Error`/`UDP_Send_Error` de core:net son enums propios (no errno) y sus mapeadores son privados → mapeo errno propio en sluice.
7. core:net no enumera interfaces en Linux (`interface_linux.odin` comentado) → getifaddrs vía `foreign import "system:c"`.
8. Un import usado sólo en la rama Linux (`fmt` en sluice) rompe `-vet` en darwin → `@(require) import`.
9. `when` falso no se chequea: los tests pueden llamar helpers definidos sólo en `_linux_test.odin` dentro de `when ODIN_OS == .Linux`.
