# Features de Portmaster: qué está portado y qué se hizo distinto

Estado del port de Portmaster (Go) a Odin al cierre de la ronda unix
(2026-11-24; fork `port/odin` en `5hif7y/portmaster`, hasta `8e05db6f`).

**"Portado"** = el código existe y sus tests corren. Lo que nunca se probó en
vivo (el login con una cuenta de Safing) se marca aparte.

Verificación del cierre:

| Dónde | Resultado |
|---|---|
| Windows, todos los paquetes con tests | 153/154 (el restante, `lib/dbus/dbustest`, es sólo Linux) |
| Linux (WSL Arch), todos los paquetes con tests | 154/154 |
| Linux, tests privilegiados de netfilter y eBPF en network namespaces | 6/6, sin reglas sobrantes; reglas del namespace principal sin cambios |
| `odin check -vet` para windows_amd64, linux_amd64 y darwin_arm64 | 0 fallas |
| En vivo en Linux (VPS Rocky 9, 2026-11-24, `odin/tests/linux_live/run_vps.sh`) | 26/26: NetworkManager, notificaciones, gnome-keyring y `portmaster-core` bajo systemd |
| En vivo contra el servidor de cuentas real de Safing, sin cuenta | health check y login inválido → 401, como Go |
| En vivo en Windows (VM Hyper-V, 2026-11-24, `odin/tests/winvm/run_test.ps1`) | 10/10: driver WFP firmado de Safing v2.1.2 e integración ETW con `portmaster-core.dll` compilada por nosotros |

### Plataformas (para usar varios dispositivos con un mismo SPN)

| Plataforma | Estado | Qué hace falta |
|---|---|---|
| Linux | ✔ probado en vivo | Nada externo: intercepta con nfqueue/eBPF del kernel |
| Windows | ✔ probado en vivo | Un driver WFP firmado. El de Safing es GPL y se usa tal cual; no depende de sus servidores. Driver propio: sólo documentado en `odin/tests/winvm/OWN-DRIVER.md` |
| macOS | Compila (`darwin_arm64`), 14 `PORT-TODO(darwin)`, sin probar | Upstream nunca tuvo cliente de macOS: interceptar necesitaría una Network Extension, desarrollo nuevo |
| Android | No existe | Upstream nunca tuvo cliente: sería una app nueva con `VpnService`, fuera del port |

---

## 1. Features del Portmaster original

| Feature | ¿Portado? | Nota |
|---|---|---|
| Monitor de conexiones por app (Network Activity) | ✔ | El JSON de `Connection` es idéntico al de Go |
| Firewall por app: allow/block, acción por defecto, reglas por dominio, IP, país, ASN y puerto | ✔ | Motor de reglas con los tests de Go tal cual |
| Intercepción en Windows (driver WFP, kext v1 y v2) | ✔ v2 **probado con el driver real** (2026-11-24); v1 sólo contra el kext falso | `portmaster-kext.sys` v2.1.2 firmado, en una VM Hyper-V descartable (`odin/tests/winvm/run_test.ps1`), 9/9: el servicio del driver se crea, arranca, para y borra; la red anda antes, durante y después; el DNS se redirige al nameserver y netquery registra dominio y veredicto. Además, el kext falso por named pipe con los bytes exactos del encoder Rust |
| Intercepción en Linux (nfqueue, conntrack, iptables, eBPF) | ✔ | Probado como root sólo dentro de network namespaces de WSL, con el e2e del firewall |
| Filter lists (ads, trackers, malware) y listas propias | ✔ | El bloom filter es bit a bit el de Go |
| Bloqueo de CNAMEs | ✔ | En Go no funcionaba (ver `BUGS.md`) |
| DNS propio: resolver con UDP, TCP, DoT, DoH, mDNS y caché | ✔ | DoH va sobre HTTP/1.1, no h2 (ver §2) |
| Nameserver local con atribución por proceso | ✔ | Además por TCP (ver §2) |
| Monitor DNS del sistema (ETW en Windows, systemd-resolved en Linux) | ✔ | ETW probado en vivo en la VM con `portmaster-core.dll` compilada por nosotros desde `windows_core_dll/` (user-mode, no necesita firma): ve el dominio de self-check |
| Network History y Bandwidth (netquery) | ✔ | Abre el `history.db` de Go |
| Perfiles de apps, detección de procesos, íconos y nombres | ✔ | Windows por Win32 sin admin; Linux por `/proc` e íconos XDG |
| Tags de proceso (svchost, Windows Store, Flatpak, Snap, AppImage, intérpretes) | ✔ | |
| Geoip (país y ASN) | ✔ | Lector MMDB propio |
| Detección de DGA | ✔ | |
| Estado online y portal cautivo (netenv) | ✔ | En Linux, NetworkManager por D-Bus probado en vivo en el VPS (conectividad y DNS iguales a `nmcli`) |
| SPN: cliente multi-hop y hub | ✔ | Probado en local y contra el VPS |
| SPN: tokens, acceso y cuenta de Safing | ✔ | Contra el servidor real de Safing sólo lo que no pide cuenta (health check, login inválido → 401). El login real, los tokens y el perfil, sólo contra un servidor falso |
| Split tunneling | ✔ | |
| Interop con IVPN (Windows y Linux) | ✔ | En Linux los comandos nft/ip sólo se verifican en tests, nunca se ejecutan |
| Self-check de compatibilidad | ✔ | |
| Notificaciones de escritorio y prompts | ✔ | Windows contra el notifyd real de N4. Linux contra un daemon real (xfce4-notifyd en el VPS: dunst no está en Rocky 9): app_icon, image-path, image-data y la señal de cierre |
| Updates automáticos con índices firmados | ✔ | Índices compatibles byte a byte con los de Go |
| API REST y websocket para la UI | ✔ | La UI del desktop no se portó (fuera de alcance) |
| Config global y por app, exportar configuración | ✔ | |
| Sync de settings y perfiles | ✔ | |
| Broadcasts de Safing | ✔ | |
| Métricas y push de Prometheus | ✔ | |
| Info de debug exportable | ✔ | Sin stacks de goroutines (ver §2) |
| Keyring (Credential Manager; Secret Service en Linux) | ✔ | Linux contra gnome-keyring real en el VPS (set, get, reemplazo, delete, self-check del trust store) |
| Modo servicio (SCM en Windows; señales en Linux) | ✔ | En Linux, `portmaster-core` real bajo systemd con el sandbox de la unit de Portmaster: arranque, reinicio por la API (exit 23), reinicio tras un crash, parada limpia. Sin intercepción, DNS en 15353, reglas del firewall del VPS sin cambios |
| `recover-iptables` | ✔ | Probado en un namespace |
| Herramientas: observation-hub, updatemgr, trafficgen, integrationtest, testsuite, rng/test | ✔ | |
| winkext-test | Compilado, nunca ejecutado | Habla el protocolo v1 y el driver actual es v2. Se puede correr a mano en la VM (`in_vm.ps1 -WinkextTest`) |
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
| Certificados de cliente (mTLS) | RSA, ECDSA, Ed25519 | RSA y ECDSA en Windows y Linux; Ed25519 sólo en Linux (en Windows, aceptado: ECDSA alcanza). En Windows, ECDSA vive en un contenedor CNG con nombre único del almacén del usuario, que se borra al destruir el certificado (y se barre si el proceso muere): SChannel firma dentro de LSASS y no puede usar una clave efímera. Ed25519 no se puede en Windows: CNG rechaza la clave (`NTE_BAD_ALGID`, medido en Windows 10 22H2). **Pendiente**, para otra sesión: Ed25519 en Windows usando OpenSSL en vez de SChannel para esas conexiones (`PORT-TODO(ed25519-windows)`; un proveedor de claves propio no sirve porque SChannel no negocia ed25519) |
| Certificados Ed25519 del servidor remoto | Los acepta | En Windows, SChannel no negocia Ed25519: falla cualquier TLS (DoT/DoH, cliente HTTP, `patrol`, proxy de split tunneling) contra un servidor con certificado Ed25519. Las CAs públicas sólo emiten RSA/ECDSA, así que sólo afecta a servidores propios con certificado Ed25519 hecho a mano (usar ECDSA). Deducido de SChannel, sin probar en vivo. El Ed25519 de jess (identidades del SPN, firmas de updates) no pasa por TLS y anda en Windows |
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
