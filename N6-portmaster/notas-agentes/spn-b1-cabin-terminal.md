# Etapa 5 (SPN), ronda B1 — spn/cabin + spn/terminal — informe

Sin commit. Escrito sólo en `odin/spn/{cabin,terminal}` + 1 arreglo mínimo fuera (abajo). `spn/ships` no tocado.

## Archivos (`odin/spn/…`)
- **cabin** (`pm_spn_cabin`, ~1.650 líneas): identity, keys, config_public, database (códec del record Identity), verification, module, `hostname_windows` (GetComputerNameExW) / `hostname_unix` (`#+build !windows`, gethostname real: el hub corre en Linux) + cabin_test.
- **terminal** (`pm_spn_terminal`, ~5.100 líneas): errors, msgtypes, msg, event (Event + Msg_Queue: reemplazo de chans/select), control_flow (Flow_Control vtable, DFQ, Direct_Delivery), init, terminal, operation, operation_base (+One_Off / Message_Stream), operation_counter, session, rate_limit, permission, defaults, fmt, upstream, module, metrics, testing (Test_Terminal, pares con forwarder con/sin delay, Bare_Terminal) + terminal_test.
- **Fuera del alcance**: `odin/spn/hub/codec.odin` — `decode_hub_fields` y `node_to_json` pasan de `@(private)` a exportados (los usa el códec del Identity). Nada más.

## Portado vs PORT-TODO
Todo portado. terminal **no** importa spn/access (ni docks/crew/navigator/captain): sin hooks, sin deps.odin.
- **Byte-compat**: init = varint(version) + dsd `'C'`+CBOR con claves de los tags json en orden y omitempty (golden: `01 43 a3 61 70 08 62 66 63 01 62 71 73 10` y la variante con los 5 campos); op msg = `[len][id|type][data]`; init de op = bloque(tipo) + `'J'`+JSON de CounterOpts con nombres Go; stop = varint(id de error); DFQ antepone uvarint de espacio (el receptor lee N16, como Go). Tests: bytes exactos que entrega un terminal local a su upstream (None y DFQ), y un terminal remoto que recibe frames armados a mano al estilo Go, arranca el counter op y responde `[2, 8|3, 2]`. Cifrado: sesiones jess `w1` reales (cliente `envelope_wire_correspondence` al signet del Status; servidor `letter_wire_correspondence` con el Identity como trust store), `letter_to_wire`/`letter_from_wire`. Sin vectores Go capturados (no hay Go): goldens derivados de las reglas.
- **cabin**: Identity como record JSON con los nombres Go (`ID, Map, Hub, Signet, ExchKeys{Created,Expires}`), round trip y re-codificación idéntica; como en Go, la clave privada de intercambio no se serializa → al cargar se generan claves nuevas. Verification con JSON `{"c","p","cr","sr"}` (golden).
- PORT-TODO: flag `-debug-unit-scheduling` (cmdbase, etapa 6; var `debug_unit_scheduling`). netenv en Linux sigue siendo stub (otro paquete): en el VPS las IPs del hub se configuran a mano (`spn/publicHub/ip4|ip6`).

## Hooks
Ninguno nuevo. API para las rondas siguientes: `Terminal`/`Operation`/`Flow_Control` son vtables; extensiones (crane, expansion) guardan `^Terminal_Base` y llaman `set_terminal_extension`; `Upstream{send, data, release}`; `register_op_type` antes de arrancar el módulo; DFQ externo (crew op_connect, docks expand) con `new_duplex_flow_queue(ctx, size, submit, data, notify: ^Event)`.

## Tests (`export PATH=…vcpkg…/bin`; `timeout 900 odin test odin/spn/<pkg> -collection:pm=odin -vet -out:$TEMP/b1/spn_<pkg>.exe > log 2>&1`, ×3)
- **cabin 7/7 ×3 OK** (2–5 s): TestIdentity (crear, mantener sin cambios, lanes, export, save → load_identity contra sqlite "core"), TestKeyMaintenance (1000 h simuladas, ≤17 claves), TestVerification (8 casos MITM) + propios: formato del request (golden JSON, challenge < 16 rechazado), códec Identity (round trip byte-idéntico), trust store (sólo privadas, coinciden con las públicas del Status), offline status aplicado, sign_hub_msg/open, base64 RawURL de IDs, hostname.
- **terminal 12/12 ×3 OK** (42–50 s): TestTerminals completo (sin/con cifrado × None(5)/DFQ(16) × los 15 contadores, incl. los 3 stress ×1000; espacios DFQ iguales en ambos lados), TestRateLimit, TestSuspicionLimit, TestConcurrencyLimit (sin el panic/recover de Go; verifica ≤30 simultáneos y ctx cancelado) + propios: framing, errores, Terminal_Opts (goldens CBOR, defaults, JSON, inválidos), wire de init (None/DFQ), remoto parsea frames estilo Go, permisos/tipo desconocido (One_Off), par con delay + errores de setup de cifrado, DFQ deliver (N16, overflow, report forzado). Se verifica que todos los Terminal_Base se liberan (`live_terminals` → 0).
- `odin check -vet -no-entry-point` limpio; `VET=1 bash odin/tests/crosscheck.sh windows_amd64 linux_amd64 darwin_arm64 -- odin/spn/cabin odin/spn/terminal odin/spn/hub`: 9/9 ok. Sólo in-process/loopback (servicetest: API en 127.0.0.1 efímero).

## Bugs de Go
Corregidos: Direct delivery (FlowControlNone) espera sobre el ctx **padre** (una entrega a un terminal abandonado con cola llena bloquea hasta que termina el padre) → ctx del terminal; el sender descarta el buffer sin Finish si falla el cifrado (unidad perdida en el scheduler) → se destruye; getPublicHubInfo con hub no público usa getters nil (panic) → defaults; claves de intercambio quemadas que una sesión jess todavía referencia → se retienen hasta destruir el Identity.
Conservados (afectan el wire o la semántica): padding `(Padding-len)%Padding` con módulo truncado → sólo se rellenan mensajes más cortos que Padding; DFQ escribe el espacio con Pack64 y lo lee con GetNextN16 (>65535 falla); NewLocalBaseTerminal pone Encrypt después de empaquetar (el caller debe pasar `encrypt=true`); TestTerminal.HandleAbandon nunca se llama (no hay SetTerminalExtension); StartOperation que falla deja la op en el mapa (hasta el abandono).

## Trampas
1. Canal sin buffer `sendQueue` → slot único bajo `state_mu`: Send espera slot libre (1 mensaje de buffer en vez de rendezvous estricto); `select` de handler/sender/flow handler → Event (generación + cond, ctx_after_func la señala), condiciones chequeadas sin bloquear y espera con el menor timeout (timer de 20 ms, idle ticker compartido por CAS).
2. GC → refcount en Terminal_Base (creador, cada worker, abandon, stop-workers, cada op) y en Operation (creador, mapa, lookups, workers). Ciclo par de test (upstream de A retiene B y viceversa) → el upstream se suelta al final del abandon (bajo RW lock: espera envíos en curso); ops que no terminaron a tiempo se sueltan ahí también.
3. `Finish()` idempotente de Go → **todo proc que recibe un ^Msg lo consume** (también en error); el contenedor del Msg es dueño de todo lo que referencia: `get_as_container` referencia al padre → se copia al entregar a la op; Consume mueve `owned`.
4. `defer` de Odin es por bloque: `if nil { x = nuevo; defer release(x) }` liberaba en el acto.
5. Test: la `Capture` en el stack del loop se reusaba en la iteración siguiente mientras el abandon del terminal anterior seguía enviando → bytes ajenos; esperar a que `live_terminals` vuelva a 0.
6. `[]string{}` / `make(…,0)` son nil → JSON `null`; Go `[]string{}` → `[]` → slice de cap 1 recortado a 0.
7. `prep_hub` al decodificar adjuntaba Measurements (JSON distinto de Go, que las deja nil hasta GetMeasurements) → no llamarlo.
8. ClientCountTo = mensajes que recibe el cliente (los manda el servidor); `{200}` no es un uvarint válido (bit de continuación).
