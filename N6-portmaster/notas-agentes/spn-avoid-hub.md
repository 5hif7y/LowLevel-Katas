# spn: avoid_hub (crew → navigator), cierre del PORT-TODO(stage-5)

Fork `DEV/gitclones/portmaster`, rama `port/odin`. Sin commit ni push.

## Qué hace Go

- `spn/crew/connect.go` `establish`: si `getStickiedHub` devuelve una entrada con
  `Avoid` (un exit hub pegado que falló, `sticky.go` `avoidDestinationHub`), arma
  `[]Endpoint{&EndpointDomain{OriginalValue: hubID, Domain: lower(hubID)+"."}}`
  (match exacto, sin permitted = deny) y lo agrega con `append` a
  `t.connInfo.TunnelOpts.Destination.HubPolicies`; después sigue con `FindRoutes`.
- `TunnelOpts` lo arma `service/firewall/tunnel.go` `DeriveTunnelOptions` por
  conexión (Destination siempre seteado, `HubPolicies = lp.StackedExitHubPolicies()`).
  El append toca solo las opciones de esa conexión.
- El navigator matchea la policy contra las entidades del pin, que llevan como
  dominio el ID del hub en minúscula + "." (`pin.odin` `new_pin_entity`), así que
  el hub queda Denied como destino (exit). Como tránsito sigue valiendo (igual que Go).

## Diseño de ownership

`Hub_Options.hub_policies` sigue siendo un slice prestado (las policies del perfil),
pero `navigator.Options` ahora puede ser dueño de lo que se le agrega:

- `Options.owned_backing[Hub_Type]`: el array detrás de `hub_policies` una vez que se
  le hizo append (copia de las entradas prestadas + la nueva).
- `Options.owned_policies`: las policies agregadas (deep-owned, `destroy_endpoints`).
- `options_append_hub_policy(o, hub_type, policy)`: crea el sub-struct si falta, arma
  un backing nuevo, libera el anterior propio y se queda con la policy.
- `avoid_hub_policy(hub_id)` / `options_avoid_hub(o, hub_id)`: la policy de Go y el
  append al destino.
- `options_destroy` libera backing + policies propias (las prestadas nunca).
- `options_copy` hace copia profunda de lo propio (backing nuevo y clon de cada policy
  propia; las prestadas siguen compartidas), así no hay double free ni dangling.
- Solo vale para opciones de heap (`options_copy` / `default_options`, que es lo que
  vive en `Connection.tunnel_opts`). El matcher sigue copiando `Hub_Options` por valor
  mientras dura el match, sin problema.

Glue (`crew/hooks/navigator_glue.odin`): `nav_avoid_hub(opts: ^network.Opaque_Value, hub_id)`.
Si la conexión ya tiene opciones del navigator, les hace `options_avoid_hub`. Si no hay
opciones (en Go el firewall las setea siempre; en el port `navigator_derive_tunnel_options`
todavía no está cableado), crea `default_options(main map)` (lo mismo que ya usaba
`options_or_default`), las cuelga de la conexión (que las libera al destruirse) y les
hace el append. Si hay un opaque de otro tipo, no toca nada.

El hook de crew cambió de firma a `proc(opts: ^network.Opaque_Value, hub_id: string)`
(tiene que poder setear las opciones). `establish` lo llama con el lock de la conexión
tomado (Go no bloquea; acá evita una carrera con quien reemplace `tunnel_opts`).
El efecto queda acotado a esa conexión: las demás conexiones (u opciones) siguen
eligiendo ese hub.

## Archivos

| Archivo | Cambio |
|---|---|
| `odin/spn/navigator/options.odin` | campos owned en `Options`, `options_hub_options`, `options_append_hub_policy`, `avoid_hub_policy`, `options_avoid_hub`; copy/destroy con ownership; header |
| `odin/spn/crew/hooks/navigator_glue.odin` | `nav_avoid_hub` cableado, PORT-TODO eliminado, header nuevo |
| `odin/spn/crew/deps.odin` | firma `^network.Opaque_Value` y comentarios (wiring y hook) |
| `odin/spn/crew/connect.odin` | llamada con `&conn_info.tunnel_opts` bajo `network.lock` |
| `odin/spn/crew/connect_test.odin` | firma del fake |
| `odin/spn/navigator/map_test.odin` | test (a) `test_find_routes_avoid_hub` |
| `odin/spn/crew/hooks/hooks_test.odin` | test (b) `test_navigator_avoid_hub`; `test_navigator_hooks` ahora espera `avoid_hub != nil`; mutex para los tests que tocan `crew.deps` / `navigator.Main` |
| `odin/spn/navigator/navigator_test.odin` | fix de un flaky que ya estaba en HEAD (ver abajo) |

## Tests nuevos

- (a) navigator, mapa optimizado de test (100 hubs): sin evitar, el mejor exit X; con
  una copia de las opciones + `options_avoid_hub(X)`, ninguna ruta termina en X y sigue
  habiendo rutas; el matcher de destino rechaza X con esas opciones y lo acepta con las
  originales; las demás hubs siguen calificando; la policy prestada del "perfil" queda
  prestada (mismo puntero) y la original no cambia; la copia profunda sigue evitando
  después de destruir el original; un segundo avoid se acumula (3 policies); en
  opciones sin destino se crea el destino.
- (b) crew/hooks, mapa chico armado a mano sin geoip (home + A, B, C conectados entre
  sí, `navigator.Main`): `find_routes` con opciones vacías da el exit X;
  `crew.deps.avoid_hub(&opts, X)` crea las opciones de la conexión con 1 policy;
  `destination_matches` da false para X; 5 búsquedas seguidas nunca salen por X; con
  otras opciones X sigue siendo exit; un segundo avoid se agrega a las mismas opciones y
  ninguna ruta sale por ninguno de los dos.

Fix aparte: `test_update_hub_lanes_and_removal` (navigator) fallaba ~2 de cada 10
corridas en HEAD ("all pins have been disregarded"). Esperaba `.No_Location`, pero eso
depende de si otro test ya cargó geoip (si no hay geoip, el fallback self-hosted usa
locations vacías). Ahora chequea según el estado de geoip, solo si es estable antes y
después de la llamada.

## Resultados

Todo en foreground, con memory tracking (sin leaks ni bad frees).

| Paquete | Corrida 1 | Corrida 2 | Corrida 3 |
|---|---|---|---|
| odin/spn/navigator | 24/24 ok | 24/24 ok | 24/24 ok |
| odin/spn/crew | 11/11 ok | 11/11 ok | 11/11 ok |
| odin/spn/crew/hooks | 3/3 ok | 3/3 ok | 3/3 ok |
| odin/spn/captain | 6/6 ok | 6/6 ok | 6/6 ok |
| odin/tests/spn_e2e | ok (49s) | ok (50s) | ok (46s)* |

\* En la corrida 3, `spn_e2e.exe` estaba bloqueado por otra corrida de e2e que no era
mía (otro agente). Esperé a que terminara y lo corrí con `-out:spn_e2e_avoidhub.exe`
(después lo borré).

Crosscheck `VET=1 ... windows_amd64 linux_amd64 darwin_arm64 -- odin/spn/navigator
odin/spn/crew odin/spn/crew/hooks`: 9/9 ok, 0 failures.

Notas: mientras trabajaba, las ediciones a medias del otro agente en
`odin/spn/access/token` (pblind) rompían la compilación de crew/hooks, captain y e2e.
Iteré en una copia en el scratchpad con `access/token` en HEAD; las 3 corridas de
verificación finales son sobre el árbol real, cuando ya compilaba. No toqué
`access/token`, ni `hub/`, ni `sluice/`, ni `jess/` (también tienen cambios ajenos).
`pblind.exe` en la raíz no es mío.
