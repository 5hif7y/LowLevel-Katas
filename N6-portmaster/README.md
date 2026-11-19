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
| 2 | base/api, metrics, service/core, updates, broadcasts, status, sync, ui | ☐ |
| 3 | firewall en Windows: interfaz con el kext, network, process, netenv, profile, firewall | ☐ |
| 4 | resolver, nameserver, intel, **netquery** (History + Bandwidth), splittun | ☐ |
| 5 | SPN (`safing/jess` + `spn/*` + `cmds/hub`) — prueba con red propia en VPS | ☐ |
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
| [25](../N1-coreutils/Docs/porter-guide/25-notificaciones-persistentes-portmaster-notifyd.md) | Notificaciones de Portmaster como historial en notifyd |
