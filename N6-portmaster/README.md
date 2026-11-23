# N6 — Portmaster: de Go a Odin, y notificaciones que no se olvidan

Kata en tres partes:

1. **Portear** [Portmaster](https://github.com/safing/portmaster) (firewall de
   aplicaciones, ~117.000 líneas de Go) a **Odin**, por etapas. Los scripts
   auxiliares (`*.sh`, `*.py`, `*.ps1`) se quedan como están; el driver de
   Windows (`windows_kext/`, Rust) y la UI (`desktop/`, Angular + Tauri) no son
   Go y quedan fuera.
2. **Auditar** las tres features "de pago" (Network History, Bandwidth, SPN):
   [AUDITORIA-features-de-pago.md](AUDITORIA-features-de-pago.md).
3. Sobre el port, hacer que las notificaciones de Portmaster **no sean
   efímeras**, en dos niveles: Portmaster guarda **todo** su historial en su
   propia base (`notifications-history`, SQLite, eventos append-only), y sólo
   las **importantes** (política `core/notifications/globalPolicy`) se mandan
   al historial global del sistema, `notifyd` (kata [N4](../N4-notifications/)).

```
  Portmaster (Odin)                      notifyd (N4, C + SQLite)
  base/notifications ──Notify──────────▶  events: notify / replace / action /
   ▲   (wnotify, pipe por sesión)          close_request / closed  (append-only)
   │                                       notifications: proyección por id
   └──ActionInvoked / NotificationClosed── ◀── display (herbe / dunst / tiramisu)
```

## Dónde está el código

| Pieza | Dónde |
|---|---|
| Port Odin | fork `safing/portmaster` en `DEV/gitclones/portmaster`, rama `port/odin`, directorio `odin/` |
| Convenciones del port | `odin/PORTING.md` |
| Mapa de etapas (qué está, qué falta) | `odin/STAGES.md` |
| Dependencia `safing/structures` | portada en `odin/structures/` (fuente: `DEV/gitclones/structures`) |
| Cliente wnotify en Odin puro | `odin/base/notifications/wnotify/` |
| Display falso para tests | `odin/tests/fakedisplay/` |
| Informes de cada tanda de porteo | [notas-agentes/](notas-agentes/) |

## Etapas

| Etapa | Contenido | Estado |
|---|---|---|
| 1 | `base/` (log, utils, rng, info, database completo, config, runtime, notifications), `safing/structures`, `service/mgr` | ✔ (fork `cca812e4`, `f9c61ee7`, `ef10cbcc`) — ~180 tests |
| 2 | base/api (+ HTTP/websocket propios), metrics, template, apprise, safing/jess, service/core, ui, status, control, broadcasts, updates, configure, sync | ✔ (fork `f057d0f7`, `8e94e41f`, `c29a1894`, `ec014989`) — dependencias de etapas 3–6 como hooks en `deps.odin` |
| 3 | firewall en Windows: kext (protocolo), network, process, netenv, profile (+endpoints), intel (adelantado), compat, ivpn, firewall | ✔ (fork `4fb754e4` … `9f2a2380`) — probado contra un kext falso por named pipe, sin cargar el driver |
| 4 | lib/tls (SChannel), resolver, nameserver, **netquery** (History + Bandwidth), splittun | ✔ (fork `7c66afff`, `5b2e0374`, `90421c45`, `ef48c9e5`) — DNS probado en loopback, sin tocar el del sistema |
| — | bases unix-like: todo paquete pasa `odin check` para linux/darwin (`odin/tests/crosscheck.sh`) | ✔ — se corre sólo en Windows |
| 5 | SPN (`spn/*`, `cmds/hub`, `cmds/spnclient`, hooks servicio↔SPN) + deudas de etapas anteriores | ✔ (fork `9e7028b4` … `e38559f6`, cierre `a56cdaa0`) — red self-hosted en loopback y en un VPS Linux; dos bugs de Odin y un use-after-free del iterador encontrados |
| 6 | binarios `cmds/*` | ☐ |

## Tests

```bash
cd DEV/gitclones/portmaster
export PATH="$PATH:/c/Users/Shifty/DEV/vcpkg/installed/x64-windows/bin"   # sqlite3.dll
for p in $(find odin -name "*_test.odin" -printf '%h\n' | sort -u); do odin test $p -collection:pm=odin -out:$TEMP/t.exe; done
```

End-to-end de notificaciones contra el notifyd real de N4 (sin el `-define` se saltea):

```bash
odin test odin/base/notifications -collection:pm=odin -define:PM_NOTIFYD=<LowLevel-Katas>/N4-notifications/notifyd/notifyd.exe
```

## La guía de porteo

| Módulo | Tema |
|---|---|
| [22](../N1-coreutils/Docs/porter-guide/22-go-a-odin-mapa-del-lenguaje.md) | Go → Odin: errores, interfaces → vtables, `interface{}` → uniones, closures, nombres prohibidos, memoria sin GC |
| [23](../N1-coreutils/Docs/porter-guide/23-concurrencia-sin-goroutines.md) | Concurrencia: pool elástico, `select` → mutex+cond, el bug de los canales sin buffer, `context.Context`, sin `recover`, refcount, bugs de Go encontrados |
| [24](../N1-coreutils/Docs/porter-guide/24-compatibilidad-binaria-con-go.md) | Mismos bytes que Go: dsd, JSON/CBOR, meta gencode, SQLite, config.json |
| [25](../N1-coreutils/Docs/porter-guide/25-notificaciones-persistentes-portmaster-notifyd.md) | Notificaciones: historial completo local + las importantes en notifyd |
| [26](../N1-coreutils/Docs/porter-guide/26-etapa-2-http-tls-y-firmas-sin-stdlib.md) | Etapa 2: HTTP/websocket propios, TLS por WinHTTP, jess, hooks hacia etapas futuras |
| [27](../N1-coreutils/Docs/porter-guide/27-etapa-3-firewall-sin-driver.md) | Etapa 3: firewall probado sin cargar el driver |
| [28](../N1-coreutils/Docs/porter-guide/28-etapa-4-dns-tls-propio-e-historial.md) | Etapa 4: DNS, TLS sobre SChannel, netquery, bases unix-like |
| [29](../N1-coreutils/Docs/porter-guide/29-etapa-5-spn-red-propia-y-bugs-del-compilador.md) | Etapa 5: SPN con red propia, hub en Linux, dos bugs del compilador |
