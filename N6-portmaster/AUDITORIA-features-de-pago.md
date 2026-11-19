# Auditoría: las tres features "de pago" de Portmaster

Fuente: `safing/portmaster` @ `e9ca8c45` (2026-11-17, v2.2.9), clonado en
`DEV/gitclones/portmaster`. Licencia del repo: GPLv3.

Resumen: **las tres están en el código abierto**. Lo "de pago" no es código
oculto sino una **comprobación de cuenta en el cliente** contra el servidor de
cuentas de Safing, más (en SPN) la red de nodos y los tokens de acceso que
opera Safing.

## El mecanismo común: `MayUse(featureID)`

`spn/access/account/types.go`

```go
FeatureSPN           = FeatureID("spn")
FeatureSafingSupport = FeatureID("support")
FeatureHistory       = FeatureID("history")
FeatureBWVis         = FeatureID("bw-vis")
FeatureVPNCompat     = FeatureID("vpn-compat")
```

`User.MayUse(id)` devuelve `true` sólo si: hay usuario logueado, `State ==
approved`, hay `Subscription` con `EndsAt` en el futuro, hay `CurrentPlan` y
`id ∈ CurrentPlan.FeatureIDs`. El `User` sale de
`https://api.account.safing.io` (`spn/access/client.go:20`, login + perfil),
se guarda en la base local y se refresca periódicamente.

Las opciones de configuración llevan además la anotación
`config.RequiresFeatureIDAnnotation` (`"safing/portmaster:ui:config:requires-feature"`),
que sólo usa la UI para mostrarlas bloqueadas. **No hay verificación
criptográfica** en History ni en Bandwidth: es un `if` en el cliente.

## 1. Network History ($)

| Pieza | Dónde |
|---|---|
| Base de datos de historial | `service/netquery/database.go` — SQLite en memoria (`main`) + `history.db` en disco (`<data>/databases/history.db`) adjunta con `ATTACH DATABASE ... AS history`, WAL, `synchronous=NORMAL` |
| Búsqueda | `service/netquery/query.go`, `query_handler.go`, `query_request.go` — API `/api/v1/netquery/query` con un lenguaje de consulta JSON → SQL |
| Borrado automático | opción `history/keep` (`CfgOptionKeepHistoryKey`, por defecto 30 días, 0 = para siempre) en `service/profile/config.go:281`; purga periódica en `service/netquery/manager.go`/`database.go` (`Cleanup`, `VacuumHistory`) |
| Borrado a demanda | `RemoveAllHistoryData`, `RemoveHistoryForProfile` (`database.go:460+`) expuestos en `module_api.go` |
| **El gate** | `service/network/connection.go:686`: `case user.MayUse(account.FeatureHistory): conn.HistoryEnabled = lProfile.EnableHistory()` |

Sin cuenta, `HistoryEnabled` queda en `false` y la conexión sólo vive en la
tabla en memoria (que se pierde al reiniciar). Todo lo demás —esquema,
escritura, purga, búsqueda— se ejecuta igual.

## 2. Bandwidth Visibility ($)

| Pieza | Dónde |
|---|---|
| Captura en Windows | `service/firewall/interception/windowskext/bandwidth_stats.go`, `windowskext2/handler.go` (el driver reporta bytes por conexión) |
| Captura en Linux | `service/firewall/interception/ebpf/bandwidth/` (programa eBPF) |
| Agregación | `service/firewall/packet_handler.go:934` `bandwidthUpdateHandler` → `Connection.BytesReceived/BytesSent` |
| Persistencia | `service/netquery/database.go:112` columnas `bytes_received`, `bytes_sent`; tabla `main.bandwidth` (`:328`), índice por `conn_id` |
| Gráficos | `service/netquery/bandwidth_chart_handler.go` |
| **El gate** | `service/network/connection.go:695`: `conn.BandwidthEnabled = user.MayUse(account.FeatureBWVis)` |

Igual que History: un booleano por conexión decidido en el cliente.

## 3. SPN — Safing Privacy Network ($$)

Acá sí hay una parte que no está en el cliente: **la red** (nodos, mapa de
nodos firmado, emisor de tokens). Pero el **software del nodo es el mismo
repo**:

| Pieza | Dónde |
|---|---|
| Binario del nodo ("Hub") | `cmds/hub/main.go` (+ `build`, `pack`) |
| Cliente/nodo: rutas, mapa | `spn/navigator` (6,2 k líneas), `spn/captain` (bootstrap, intel, conexión al home hub) |
| Transporte y cifrado por capas | `spn/docks` (cranes), `spn/terminal`, `spn/crew`, `spn/ships`, `spn/cabin` (identidad/claves del hub), sobre `safing/jess` |
| Salidas | `spn/crew` (connect op en el exit hub), `spn/sluice` (entrada en el cliente) |
| Mapa de prueba propio | `spn/testing/simple/` — docker-compose de una red de test, `--spn-map test`, `--bootstrap-file` |
| **El gate** | `spn/access/zones.go` — para expandir o conectar por un hub hay que presentar un token de una de las zonas `pblind1` (firmas ciegas P-256, clave pública de Safing embebida), `alpha2` o `fallback1` (scramble BLAKE2b con verificadores embebidos). Los tokens los emite `api.account.safing.io` a cuentas con `FeatureSPN` |

A diferencia de History/Bandwidth, el gate de SPN **sí es criptográfico y lo
verifica el otro extremo** (el hub), así que no se "desbloquea" la red de
Safing tocando el cliente — y no corresponde hacerlo.

### SPN propio en tu VPS (lo que habría que hacer)

Es legítimo levantar **tu propia** red SPN con el código GPL:

1. Compilar `cmds/hub` (o su port Odin cuando llegue la etapa SPN) y correrlo
   en el VPS como hub público.
2. Mapa propio: `--spn-map <nombre>` en hub y cliente, y un
   `--bootstrap-file` con la dirección del hub (el hub lo genera si no existe).
3. Zona de tokens propia: reemplazar en `spn/access/zones.go` las zonas de
   Safing por una zona tuya (el patrón ya está en `initializeTestZone()`:
   scramble BLAKE2b con tokens/verificadores generados por vos) **en el hub y
   en el cliente**, o un emisor pblind con tu clave.
4. Intel del mapa: el cliente espera intel firmada del updater de Safing
   (`5ee8e283 feat(updates): require a signed intel index`); para una red
   propia hay que inyectar la intel local como hace
   `spn/testing/simple/inject-intel.sh`.
5. Con un único hub, ese hub es home y exit a la vez (1 salto): sirve para
   probar el protocolo, no da la privacidad de varios saltos. Dos VPS en
   regiones distintas ya permiten home ≠ exit.

Esto queda para la etapa SPN del port (ver [STAGES](README.md#etapas)); antes
de tocar el VPS se confirma host, puertos y qué se instala.

## Qué hace el port Odin con esto

* History y Bandwidth: el gate `MayUse` se mantiene como **punto de extensión**
  (`account.may_use`), pero la decisión local se toma por configuración
  (`history/enable`, `bandwidth/enable`), porque el port no habla con el
  servidor de cuentas de Safing.
* SPN: se portea el protocolo y el hub; la zona de tokens es configurable para
  correr una red propia. Nunca se intenta usar la red de Safing sin cuenta.
