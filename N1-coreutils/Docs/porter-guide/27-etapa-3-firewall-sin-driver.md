# Módulo 27: Etapa 3 — un firewall de Windows probado sin cargar el driver

La etapa 3 es el corazón de Portmaster: la interfaz con el driver WFP
(`portmaster-kext.sys`, Rust), las tablas de conexiones del sistema, la
identificación de procesos, los perfiles con sus reglas, el entorno de red y
el motor que decide cada veredicto. ~32.000 líneas de Go, más `service/intel`
adelantado de la etapa 4 porque las reglas lo necesitan.

Código (fork `port/odin`): `odin/lib/dns`, `odin/service/network/**`,
`odin/windows_kext/kextinterface`, `odin/service/{intel,netenv,profile,process,compat,interop,firewall}/**`,
`odin/service/{detection/dga,integration,nameserver/nsutil}`.
Commits `4fb754e4` … `9f2a2380`. Informes por paquete en
[`N6-portmaster/notas-agentes/`](../../../N6-portmaster/notas-agentes/).

---

## 1. La regla de la etapa: nada toca el sistema

Un firewall es el caso extremo de "el test cambia la máquina": instalar el
driver, registrar un servicio, cambiar el DNS o el firewall del sistema. Todos
los agentes trabajaron con la misma prohibición
([BRIEF-COMUN](../../../N6-portmaster/notas-agentes/BRIEF-COMUN.md)): no
cargar drivers, no tocar red/DNS/servicios, no pedir admin. Lo que necesita el
driver real se prueba **a nivel de bytes** y queda un test manual documentado,
saltado por defecto (`-define:PM_KEXT_MANUAL=true`).

La pregunta del módulo es cómo se llega a confiar en un firewall así.

## 2. El protocolo del driver: dos archivos que escribieron otros

El árbol Go ya trae dos archivos binarios de test:

* `rust_info_test.bin` — 1000 mensajes del driver escritos por **el encoder
  Rust**;
* `go_command_test.bin` — 5000 comandos escritos por **el encoder Go**.

El port de `kextinterface` decodifica los dos y los re-codifica **idénticos
byte a byte**. Es la mejor evidencia posible sin el driver: los dos lados
reales del protocolo, producidos por implementaciones independientes, contra
el port.

## 3. El kext falso: un named pipe que se hace pasar por `\\.\PortmasterKext`

El módulo de intercepción abre el device con `CreateFileW` overlapped y lee
con `ReadFile`/`GetOverlappedResult`. Un **named pipe** abierto con el mismo
camino de código es indistinguible para ese lector, así que el test levanta
un servidor de pipe que:

1. manda infos de conexión codificadas con el encoder portado (los mismos
   bytes que el driver Rust), partidas en trozos arbitrarios (7, 777 bytes)
   para forzar `ERROR_IO_PENDING` y lecturas parciales;
2. parsea cada comando que recibe con el parser portado de
   `protocol/src/command.rs` y guarda sus bytes exactos;
3. contesta pedidos de estadísticas de ancho de banda y logs.

El test de punta a punta de la etapa:

```
fake kext ──info(conn 601 → host permitido)──▶ interception ──▶ firewall
fake kext ──info(conn 602 → 1.1.1.1, regla de bloqueo)──▶ … ──▶ firewall
fake kext ◀── encode_verdict_command({601, Permanent_Accept})   (bytes exactos)
fake kext ◀── encode_verdict_command({602, Permanent_Block})
   la "UI" guarda una regla nueva en el perfil
fake kext ◀── encode_update_v4_command(…, Permanent_Block)
```

Dos trampas de Windows que salieron armando el fake:

* Un named pipe **no tiene `DeviceIoControl`**: el IOCTL de versión y el
  driver v1 (todo IOCTL) necesitan un transporte inyectable.
* Un servidor de pipe **síncrono** serializa `ReadFile` y `WriteFile` entre
  hilos del mismo handle (el mismo problema del módulo 25, del otro lado):
  el fake que lee y escribe a la vez se trababa. Servidor con
  `FILE_FLAG_OVERLAPPED` y un evento por operación.

## 4. Datos del sistema, sólo lectura

| Go | Odin, sin admin |
|---|---|
| gopsutil (proceso) | `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)`, `QueryFullProcessImageNameW`, `NtQueryInformationProcess(ProcessCommandLineInformation)`, `NtQuerySystemInformation` para creación y padre |
| `netstat`-like vía iphlpapi de Go | `GetExtendedTcpTable` / `GetExtendedUdpTable` con PID |
| dos comandos **PowerShell** (~1 s c/u, texto) para la interfaz por defecto | una llamada a `GetAdaptersAddresses` + `GetIpForwardTable2` |
| ICMP crudo para traceroute | `IcmpSendEcho` |
| winres / go-ico | parser PE propio (VS_VERSIONINFO, RT_GROUP_ICON), decoder ICO/DIB, encoder PNG |

Los tests son "en vivo" pero inofensivos: el test abre un socket loopback y se
busca a sí mismo en la tabla TCP del sistema; lee su propio path, cmdline,
padre y usuario; lee los íconos de `notepad.exe`. En Windows 10
`notepad.exe` no tiene `RT_ICON`: el ícono vive en
`%SystemRoot%\SystemResources\notepad.exe.mun` — Go devolvía "no icon", el
port lo encuentra.

Una trampa de alineación: los buffers del temp allocator están alineados a
byte y `NtQuerySystemInformation` falla con `STATUS_DATATYPE_MISALIGNMENT`
(0x80000002). Todo buffer de salida NT/Win32 con `mem.make_aligned(..., 16)`.

## 5. "El proceso de test es Portmaster"

Un detalle que costó dos agentes: para `service/process`, el ejecutable que
corre los tests **es** Portmaster (PID propio). Recibe el perfil especial
`_portmaster`, `Internal = true`, y `check_portmaster_connection` deja pasar
todo lo suyo — el historial nunca se activa y las reglas no se aplican. Los
tests del firewall atribuyen los paquetes sintéticos al **proceso padre**
(`odin.exe`), que sí recibe un perfil local normal.

## 5b. Una carrera que en Go casi no se ve

El barrido final (todas las suites, 3 veces) encontró un test intermitente en
`service/control`: tras una segunda pausa, el usuario reactiva la SPN y el
auto-resume no ocurre (2 de 3 corridas). El resumer se suscribía a los
cambios de config **dentro** de su worker. En Go eso es una goroutine que
arranca en microsegundos; acá es un hilo del pool, que puede tardar 50–150 ms.
Un cambio de config en esa ventana se perdía. La carrera existe en Go también,
sólo que es casi imposible de pegarle.

Arreglo: la suscripción se crea **sincrónicamente** en `start_resume_worker`,
bajo el lock, y el worker la toma al arrancar (chequeando su ctx bajo el
mismo lock, para que un worker viejo cancelado nunca se quede con la
suscripción del nuevo). 8 de 8 corridas.

Regla para el resto del port: todo lo que en Go se "registra al empezar la
goroutine" (suscripciones, callbacks, timers) y del que depende alguien que
vuelve antes de que la goroutine corra, se registra **antes** de lanzar el
worker.

## 6. Bugs de Go que cambian lo que el firewall hace

Esta etapa encontró los bugs de mayor impacto del port:

| Paquete | Bug | Efecto real |
|---|---|---|
| firewall | `mayBlockCNAMEs` resetea las listas de la entidad y nunca las recarga | un CNAME en una filter list **nunca se bloqueaba** |
| intel/filterlists | la caché de 256 records de la interfaz no se invalida tras un update | se seguían aplicando entradas **borradas o whitelisteadas** |
| interception (v1) | `ipAddressToArray` lee `&ip[i]` en vez de `&ip[i*4]` | cada update de veredicto IPv6 **nombraba otra dirección** |
| interception | `[4]byte(conn.LocalIP)` sobre una IPv4 de 16 bytes | 0.0.0.0 en el comando |
| dnsmonitor | split por espacio simple de `"type:  5 name"` + claves no FQDN | **todos los CNAME** del monitor ETW descartados |
| process | `GetProcessByRequestOrigin` arma el paquete sólo con el origen | `core/my-profile` y la autorización de apps **nunca** funcionaban |
| network | el gauge `connections/active/total` contaba `Ended != 0` | métrica invertida |
| profile | `addActiveProfile` marca outdated al perfil que se re-agrega | recarga desde la DB por cada proceso nuevo |

Y los que se **conservaron** porque deciden qué se bloquea o qué IDs existen:
el XOR de scopes de endpoints (`"LAN,LAN"` se anula), `DeriveProfileID`
ordenando su argumento in place, el regex `\a-z` de dominios `.local` (rango
BEL..'z'), `checkDomainHeuristics` mirando el comienzo del dominio, el
dominio de conectividad sin punto final.

## 7. El gate de las features pagas, resuelto localmente

La [auditoría](../../../N6-portmaster/AUDITORIA-features-de-pago.md) mostró
que History y Bandwidth sólo dependen de `user.MayUse(...)`. En el port:

```odin
deps.may_use: proc(feature: string) -> (allowed: bool, err: Error)  // lo llenará spn/access (etapa 5)
// mientras sea nil: local_may_use
//   "history" → la opción por app que ya existía, history/enable (default false)
//   "bw-vis"  → opción global nueva bandwidth/enable (default false)
```

Ambas apagadas por defecto, como en Portmaster sin cuenta; el usuario las
prende en su configuración. La SPN sigue requiriendo su propia red (etapa 5).

## 8. Lo que no está probado

* El driver real. El test manual existe, compila, y no se corrió: instala el
  servicio, acepta todo por 5 s y lo borra; necesita el `.sys` firmado y admin.
* Reglas de país/ASN en el firewall (sí en `endpoints`, con un MMDB armado en
  el test).
* `lib/dns` contra la fuente de miekg/dns: verificado con capturas y RFCs; se
  revisa en la etapa 4, cuando lo use el resolver.

---

Anterior: [26](26-etapa-2-http-tls-y-firmas-sin-stdlib.md).
