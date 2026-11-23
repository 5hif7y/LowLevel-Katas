# Etapa 5 (SPN), ronda C1 — spn/access (+ zona self-hosted, gate de features pagas) — informe

Sin commit. Escrito en `odin/spn/access/*.odin`, `odin/spn/access/hooks/` (nuevo) y un cambio mínimo en `odin/spn/access/account/client.odin`. No toqué `odin/spn/docks` (compila contra mi API).

## Archivos (`odin/spn/access`, `package pm_spn_access`)
errors (unión #shared_nil `Access_Error | token.Error`), zones, op_auth, module, database (User_Record / Auth_Token_Record + caché), client, storage, notify, api, features (íconos SVG de Go byte a byte, generados desde features.go), **selfhosted** + access_test. `hooks/` (`pm_spn_access_hooks`): hooks.odin + hooks_test.
Fuera de lo mío (mínimo): `account/client.odin` — `Client_Request_Options.send_raw` / `recv_raw` (cuerpo ya codificado / cuerpo crudo: los mensajes de tokens no pasan por dsd, big.Int).

## API para docks/crew/captain
`new_access(Instance{data, config, spn_group, is_shutting_down})`, `enable_test_mode()`, `authorize_to_terminal(^terminal.Terminal) -> (^Authorize_Op, terminal.Error)` + `authorize_op_result(op, timeout)` (éxito = `.Explicit_Ack` externo) + `authorize_op_release`. El op "auth" se registra en `@(init)` (como Go `init()`). `expand_and_connect_zones()` devuelve copia; `should_request`, `get_token_amount`, `get_token`, `verify_token`, `get_user`/`release_user`, `login`/`logout`/`update_user`/`update_tokens`, `self_hosted_mode()`.

## Zona self-hosted
Opción `spn/access/selfHostedZoneFile` (string, Stable/Developer, requires restart; override programático `set_self_hosted_zone_file` para el flag del hub, etapa 6). Si está puesta, el start: resetea el registro, carga el archivo (`token.new_scramble_handler_from_config`), lo registra como ÚNICA zona ExpandAndConnect con MayExpand|MayConnect, sin zonas de Safing, sin persistencia de tokens en DB, sin tarea de cuenta; login/update_user/update_tokens → `.Self_Hosted`, health_check → false sin red, `should_request` → false. Cliente = tokens del archivo; hub = sólo verificadores. Archivo roto → start falla con el registro vacío (nunca cae a las zonas de Safing). No desbloquea History/Bandwidth.

## Gate de features pagas
`access.may_use_feature(feature)` = `user.MayUse` como Go (sin usuario → false). `hooks.install_hooks()` llena `network.deps.may_use` (cuenta concede → true; si no, `network.local_may_use`: `history/enable` por app, `bandwidth/enable`) y `broadcasts.deps.get_user` (snapshot JSON del UserRecord, MayUse(""), Modified, FeatureIDs). Self-hosted no tiene cuenta → no concede nada.

## Portado vs PORT-TODO
Todo `spn/access` portado. JSON Go: UserRecord = campos de User inline + `LastNotifiedOfEnd`/`LoggedInAt`; AuthTokenRecord `{"Token":{"Device","Token"}}` (secret + crown jewel); tokens en wrapper RAW, expiración relativa 30 d. URL del servidor de cuentas siempre parámetro (`set_account_server`).
- PORT-TODO / para otras rondas: **captain debe saltear el chequeo de usuario/login en `self_hosted_mode()`** (Go `DisableAccount`), si no el cliente pide login; `spn/enable` lo registra captain (hasta entonces enable/disable_spn sólo loguean); `hooks.install_hooks()` y `Instance` los llena la instancia (etapa 6); en Linux `lib/httpclient` es stub → un hub Linux en modo Safing no puede hacer health check (irrelevante en self-hosted).
- Divergencia: setup/issue de tokens van en CBOR (Go MsgPack). pblind no interopera con Safing (ver A2).

## Hooks
Llenados: `network.deps.may_use`, `broadcasts.deps.get_user` (por `pm:spn/access/hooks`, lo llama la instancia, etapa 6). Nuevos: `access.Instance` (config, spn_group, is_shutting_down; instancia, etapa 6).

## Tests (`export PATH=…vcpkg…/bin`; `timeout 600 odin test odin/spn/access[/hooks|/account] -collection:pm=odin -vet -out:…`)
- access **14/14 ×3 OK** (1,5–2,2 s), hooks **1/1 ×3**, account **5/5 ×3**; sin leaks. `VET=1 bash odin/tests/crosscheck.sh windows_amd64 linux_amd64 darwin_arm64 -- odin/spn/access{,/hooks,/account,/token}`: 12/12 ok.
- Go: TestMain → begin (módulo con zonas Safing, client); TestClient portado, saltado por defecto (`-define:PM_SPN_ACCESS_LIVE=true` + SPN_TEST_USERNAME/PASSWORD).
- Propios (servidor de cuentas falso en loopback efímero): zonas Safing (3 zonas, permisos, should_request, salvaguarda del test zone); fallback (cliente lo saltea con issuer sano, hub lo rechaza con health 200, lo acepta con 500, caché de 30 s); auth op con zona de test (Explicit_Ack, permisos sólo del lado servidor, basura → Malformed, token malo → Permission_Denied, sin tokens → Internal_Error, terminal no-autorizante → Incorrect_Usage); **self-hosted**: cliente toma token del archivo, hub con sólo verificadores lo acepta (MayExpand|MayConnect), token alterado y token `pblind1:` rechazados, zonas Safing no registradas, 0 requests al servidor; archivo roto/inexistente; restart del módulo alternando modos; login/update (rotación de token)/401 (vista + suscripción)/logout shallow/normal/purge/reintento con device 410; update_tokens completo (setup CBOR → sesión → issue → 4 tokens procesados y verificados) y error del issuer; JSON byte a byte de los records; storage store/load/clear; notificación de fin de paquete (Warning 3 días, no repite, Info 20 días en plan largo, nada con auto-renovación); API (features JSON, ícono, profile 401/200, login 401+WWW-Authenticate/200, logout purge).

## Bugs de Go
Corregidos: **VerifyToken rechazaba fallback cuando el health check FALLABA** (`IsFallback() && !healthCheck()`, contrario a su propio texto y al cliente) → se rechaza con el issuer sano; el callback "spn enable check" se agregaba en cada Start → una vez; UpdateAccount / notifyOfPackageEnd leían el usuario sin lock; nil-deref de `user.Device` en Login (log, SaveNewAuthToken) y Logout; storeTokens advertía por borrar una entrada inexistente.
Conservados: reintento de login ante 410 aunque no se haya mandado device; persistentZones en modo test = zonas de Safing (warnings "could not find zone"); `logoutOnAuthError` sin uso.

## Trampas
1. Opción con `release_level = .Experimental` se lee como su default en release Stable → la ruta nunca llegaba → Stable.
2. `%d` de un `time.Duration` imprime "3ns" → `int(until_end / DAY)`.
3. Unión #shared_nil de dos enums con el mismo miembro (`.Encoding_Failed`) → selector implícito inválido → calificar `Access_Error.X`.
4. `worker_mgr_stop` mata el scheduler para siempre y un Worker_Mgr sin acción se autodestruye → `keep_alive` al crearlo, nada de stop en el stop del módulo (Go tampoco).
5. El registro de tokens no es dueño de los handlers y una verificación concurrente puede tenerlos → handlers inmortales (lista `retired`).
6. Cliente y hub en el mismo proceso comparten el registro → el test self-hosted inicializa primero con el archivo del cliente (toma el token) y después con el del hub (verifica).
7. Campos de records liberados reflexivamente (`_owns_fields`): nunca asignar literales (p. ej. el estado "loggedout") → clonar al heap.
8. Esperar `live_terminals == 0` por par en defers anidados se bloquea con el otro par vivo → una espera al final del test.
