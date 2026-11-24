# Features de Portmaster: qué está portado y qué se hizo distinto

Estado del port de Portmaster (Go) a Odin al cierre de la ronda unix
(2026-11-24; fork `port/odin` en `5hif7y/portmaster`, hasta `8e05db6f`).

**"Portado"** = el código existe y sus tests corren. Lo que nunca se probó en
vivo (driver real, servicios del escritorio Linux) se marca aparte.

Verificación del cierre:

| Dónde | Resultado |
|---|---|
| Windows, todos los paquetes con tests | 153/154 (el restante, `lib/dbus/dbustest`, es sólo Linux) |
| Linux (WSL Arch), todos los paquetes con tests | 154/154 |
| Linux, tests privilegiados de netfilter y eBPF en network namespaces | 6/6, sin reglas sobrantes; reglas del namespace principal sin cambios |
| `odin check -vet` para windows_amd64, linux_amd64 y darwin_arm64 | 0 fallas |

---

## 1. Features del Portmaster original

| Feature | ¿Portado? | Nota |
|---|---|---|
| Monitor de conexiones por app (Network Activity) | ✔ | El JSON de `Connection` es idéntico al de Go |
| Firewall por app: allow/block, acción por defecto, reglas por dominio, IP, país, ASN y puerto | ✔ | Motor de reglas con los tests de Go tal cual |
| Intercepción en Windows (driver WFP, kext v1 y v2) | ✔ en código, **sin probar con el driver real** | Probado contra un kext falso por named pipe, con los bytes exactos del encoder Rust. El driver nunca se cargó |
| Intercepción en Linux (nfqueue, conntrack, iptables, eBPF) | ✔ | Probado como root sólo dentro de network namespaces de WSL, con el e2e del firewall |
| Filter lists (ads, trackers, malware) y listas propias | ✔ | El bloom filter es bit a bit el de Go |
| Bloqueo de CNAMEs | ✔ | En Go no funcionaba (ver `BUGS.md`) |
| DNS propio: resolver con UDP, TCP, DoT, DoH, mDNS y caché | ✔ | DoH va sobre HTTP/1.1, no h2 (ver §2) |
| Nameserver local con atribución por proceso | ✔ | Además por TCP (ver §2) |
| Monitor DNS del sistema (ETW en Windows, systemd-resolved en Linux) | ✔ | |
| Network History y Bandwidth (netquery) | ✔ | Abre el `history.db` de Go |
| Perfiles de apps, detección de procesos, íconos y nombres | ✔ | Windows por Win32 sin admin; Linux por `/proc` e íconos XDG |
| Tags de proceso (svchost, Windows Store, Flatpak, Snap, AppImage, intérpretes) | ✔ | |
| Geoip (país y ASN) | ✔ | Lector MMDB propio |
| Detección de DGA | ✔ | |
| Estado online y portal cautivo (netenv) | ✔ | En Linux, NetworkManager por D-Bus **sin probar en vivo** (WSL no lo tiene; probado contra uno falso) |
| SPN: cliente multi-hop y hub | ✔ | Probado en local y contra el VPS |
| SPN: tokens, acceso y cuenta de Safing | ✔ en código | El cliente de cuentas se probó contra un servidor falso, nunca contra el de Safing |
| Split tunneling | ✔ | |
| Interop con IVPN (Windows y Linux) | ✔ | En Linux los comandos nft/ip sólo se verifican en tests, nunca se ejecutan |
| Self-check de compatibilidad | ✔ | |
| Notificaciones de escritorio y prompts | ✔ | Windows contra el notifyd real de N4; Linux contra un daemon falso (ningún dunst real) |
| Updates automáticos con índices firmados | ✔ | Índices compatibles byte a byte con los de Go |
| API REST y websocket para la UI | ✔ | La UI del desktop no se portó (fuera de alcance) |
| Config global y por app, exportar configuración | ✔ | |
| Sync de settings y perfiles | ✔ | |
| Broadcasts de Safing | ✔ | |
| Métricas y push de Prometheus | ✔ | |
| Info de debug exportable | ✔ | Sin stacks de goroutines (ver §2) |
| Keyring (Credential Manager; Secret Service en Linux) | ✔ | Linux sólo contra un Secret Service falso |
| Modo servicio (SCM en Windows; señales en Linux) | ✔ | En Linux se probaron las señales con el binario real; systemd no se probó |
| `recover-iptables` | ✔ | Probado en un namespace |
| Herramientas: observation-hub, updatemgr, trafficgen, integrationtest, testsuite, rng/test | ✔ | |
| winkext-test | Compilado, nunca ejecutado | Habla con el driver real |
| macOS | Sólo compila | 14 `PORT-TODO(darwin)`: procesos, intercepción, notificaciones, keyring y algo de netenv/metrics |
| UI desktop (Angular + Tauri), driver WFP (Rust), scripts | No (fuera de alcance) | No son Go; el driver y los scripts se usan tal cual |

---

## 2. Lo que se hizo distinto

### Decisiones del usuario

| Qué | Go | Esta implementación |
|---|---|---|
| Notificaciones | Las muestra la UI Tauri | Híbrido: historial completo en sqlite, y las importantes van al notifyd de N4 (Windows) o a `org.freedesktop.Notifications` (Linux, lo que implementa dunst) |
| Red SPN | La de Safing | Self-hosted: zona de tokens y archivo de intel propios |
| Hub en el VPS | | Escucha en todas las interfaces (firewalld ya bloquea el puerto) |
| Pruebas | | Nunca se carga el driver, nunca se pide admin, nunca se toca el DNS, la red ni el firewall del sistema. Root sólo dentro de network namespaces de WSL |

### Decisiones del port (portabilidad, o Odin no tiene el equivalente)

| Qué | Go | Esta implementación |
|---|---|---|
| Base de datos | badger y bbolt | sqlite, más un importador de sólo lectura de bases bbolt de Go |
| TLS | `crypto/tls` | SChannel en Windows, OpenSSL 3 en Linux |
| Certificados de cliente (mTLS) | RSA, ECDSA, Ed25519 | Windows sólo RSA (ECDSA espera una decisión: necesita un contenedor de claves persistido); Linux los tres |
| Cliente HTTP | `net/http`, con h2 | WinHTTP en Windows, HTTP/1.1 propio en Linux. Sin HTTP/2, así que DoH usa HTTP/1.1 con keep-alive |
| Nameserver | Sólo UDP | UDP y TCP |
| Librerías de terceros | miekg/dns, gopacket, maxmind, cobra, text/template, gopsutil, godbus, go-keyring | Ports propios del subconjunto que Portmaster usa, más un cliente D-Bus y un netlink en Odin puro |
| eBPF | cilium/ebpf | Los mismos `.o` precompilados de Go, cargados con la libbpf del sistema |
| netenv en Windows | PowerShell | `GetAdaptersAddresses` |
| Load average en Windows | Cola de PDH | Fracción de CPU ocupada (EWMA); en Linux es el `getloadavg` real |
| Flags agregados | | `--unprivileged` en `portmaster-core`; `--automatic-updates`, `--online-check` y `--geoip` en el hub |
| Pánicos | `recover` contesta 500 o sigue | Sin `recover`: un pánico tumba el proceso |
| Diagnóstico de Go | pprof, stacks de goroutines, memstats del GC, BuildInfo | No existen en Odin; no están |
| Logging estructurado | Atributos y grupos de slog | No están |
| Bugs de Go | | Unos 60 corregidos, entre ellos dos inyecciones SQL en netquery, CNAMEs nunca bloqueados, updates IPv6 del kext v1 rotos, observation-hub que nunca notificaba e íconos de Linux nunca encontrados. Los que cambiarían bytes en disco o en la red quedaron como en Go a propósito (todo en `BUGS.md`) |
| Código no portado | | `api/testclient` (código muerto) y el código generado por bob (reemplazado por sqlite) |

---

Detalle por paquete: `STAGES.md` y `PORTING.md` en el fork; bugs en
[`BUGS.md`](BUGS.md); cómo se hizo cada etapa en los módulos 22–31 de la
[guía del porter](../N1-coreutils/Docs/porter-guide/README.md).
