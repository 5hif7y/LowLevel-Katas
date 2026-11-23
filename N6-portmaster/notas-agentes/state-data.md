# mgr.State.Data fiel (deuda de etapa 2)

## Qué hace Go

- `mgr.State` tiene `Data any`. No lleva tags, así que en JSON es `"Data"`, y el
  export del status (`runtime:system/status` → `Modules[].States[].Data` y
  `WorstState.Data`, porque el `State` va embebido) lo serializa con
  `encoding/json` **cuando se exporta**.
- Quién llena `Data` (busqué todos los `mgr.State{`: hay 14, y sólo 2 setean Data):
  - `base/notifications/module-mirror.go`: `Data: n.EventData`. Por esta vía
    pasan control (`control:paused` / `control:error` con `&c.pauseInfo`), y
    también firewall (prompt), netenv (captive portal), broadcasts, compat,
    resolver, nameserver y captain, todos usando `SyncWithState`.
  - `service/updates/module.go:558`: `Data: u.corruptedInstallation`. Es un
    `*fmt.wrapError` (sale de `fmt.Errorf("...: %w")`) y sólo tiene campos no
    exportados, así que en JSON queda `{}`.
- Quién lo lee: el Go de `service/status` no lo usa. La UI sí:
  `navigation.ts:123` y `security-lock.ts:67` leen
  `GetModuleState(status,'Control','control:paused')?.Data` como
  `ControlPauseStateData {Interception, SPN, TillTime}`.

## Diseño

- `mgr.State.data: rawptr` pasa a ser **`data_json: string`**. Es el valor ya
  codificado como lo haría `encoding/json` (el productor llama a
  `dsd.json_marshal`), y `""` significa Data nil, o sea `null`. Se clona y se
  libera igual que `id/name/message` (`state_clone`, `state_free_strings`), así
  que add, replace, remove, clear, export, eventos y destroy ya lo manejan.
- En `dsd` agregué **`Raw_JSON :: distinct string`** (equivale a `json.RawMessage`):
  - JSON: si está vacío sale `null`. Si no, se valida (si es inválido devuelve
    `Syntax_Error`; en Go sería `MarshalerError`) y se compacta con el escape
    HTML de Go (`< > &` y U+2028/2029 dentro de strings). Si hay indent, se
    re-indenta como `MarshalIndent`.
  - CBOR/MsgPack: se transcodifica desde el texto con `node_to_cbor` /
    `node_to_msgpack`. YAML pasa por JSON. `omitempty` trata `""` como vacío.
  - Decode: el subárbol se vuelve a serializar compacto, y `null` queda como
    `"null"`, igual que `RawMessage`.
- `status.State_JSON.data` pasa a ser `dsd.Raw_JSON` (clonado del `data_json`).
  Saqué el `PORT-TODO(stage-2)` y actualicé la fila de `STAGES.md`.
- Mirror: `data_json = dsd.json_marshal(n.event_data)` con el lock de la
  notificación tomado. Es la misma regla que ya usa `history.odin`. Si falla,
  loguea un warning y queda `null` (Go haría fallar todo el export).
- Updates: `data_json = "{}"`.
- **Divergencia (documentada en `states.odin`)**: Go guarda el valor (muchas
  veces un puntero) y lo codifica en cada export. El port en cambio saca una
  foto cuando se agrega el estado. En control no se nota, porque cada cambio de
  pausa crea una notificación nueva y re-sincroniza. Sólo podría notarse con un
  `control:error` viejo que siguiera vivo.

## Archivos

- `odin/structures/dsd/raw_json.odin` (nuevo), con hooks en `json_encode.odin`,
  `cbor.odin`, `msgpack.odin` y `node.odin`; tests en `raw_json_test.odin` (nuevo).
- `odin/service/mgr/states.odin`; tests en `states_test.odin` (nuevo).
- `odin/base/notifications/module_mirror.odin` y `notifications_test.odin`.
- `odin/service/status/status.odin` y `status_test.odin`.
- `odin/service/updates/module.odin`.
- `odin/STAGES.md`.

## Tests

| Test | Qué prueba |
|---|---|
| `dsd.test_raw_json_encode` | null, compactado con escape HTML y U+2028, indent idéntico a MarshalIndent, inválido y basura al final dan error, top-level |
| `dsd.test_raw_json_binary_formats` | CBOR/MsgPack iguales a `json_to_format`; vacío da nil |
| `dsd.test_raw_json_decode` | re-serialización, `null` da `"null"`, sin leaks |
| `mgr.test_state_data_json_clone_destroy` | clone copia (otro puntero) y destroy libera todo (Tracking_Allocator propio: 0 vivos, 0 bad free) |
| `mgr.test_state_mgr_data_json_ownership` | el buffer del caller se reusa después de add; replace y replace sin data; export independiente; remove y clear |
| `notifications.test_module_mirror_event_data` | EventData del struct da los bytes de Go (orden de campos, escape HTML, nil como null, campos no exportados afuera, time RFC3339Nano); foto; sin EventData da `""` |
| `status.test_status_lifecycle` (ampliado) | byte-compare del estado completo en `Modules` y en `WorstState` con `"Data":{...}`; el hint sin Data da `"Data":null` |

Resultados, 3 corridas en foreground (`-vet`):

| Paquete | 1 | 2 | 3 |
|---|---|---|---|
| structures/dsd (20) | ok | ok | ok |
| service/mgr (28) | ok | ok | ok |
| service/status (2) | ok | ok | ok |
| base/notifications (13) | ok | ok | ok |
| service/updates (15) | ok | ok | ok |
| service/intel/customlists (3) | ok | ok | ok |
| service/intel/filterlists (5) | ok | ok | ok |
| service/profile (6) | ok | ok | ok |
| service/resolver (46) | ok | ok | no compila* |
| spn/captain (6) | ok | ok | no compila* |
| service/control (2) | ok | ok | ok |

\* En la 3ª corrida, `odin/lib/dns/rdata.odin(712)` no compila ("Cannot
index a constant 'B64_STD'"). Es la edición en curso del otro agente en
lib/dns; no lo toqué. Las corridas 1 y 2 de esos dos paquetes pasaron.

`VET=1 bash odin/tests/crosscheck.sh windows_amd64 linux_amd64 darwin_arm64 --
odin/structures/dsd odin/service/mgr odin/service/status odin/base/notifications
odin/service/updates`: los 15 dan ok, con 0 fallas.

Ojo con las herramientas: el Edit/Write del agente decodifica los escapes
`\uXXXX` que van dentro de los literales. Los strings esperados con escapes
los escribí con python (`chr(92)`).
