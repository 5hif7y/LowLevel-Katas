# SPN ronda F — hooks del servicio (cliente SPN dentro de Portmaster)

Fork `DEV/gitclones/portmaster`, rama `port/odin`, sin commitear.

## Qué se hizo

Paquete nuevo **`odin/spn/hooks`** (`pm_spn_hooks`): importa los paquetes de
servicio y los llena (mismo patrón que `crew/hooks` y `access/hooks`, sin
ciclos). `install_hooks()` llena lo suyo y además llama a
`access/hooks.install_hooks` y `crew/hooks.install_hooks`, así la instancia
(etapa 6) llama uno solo.

Sub-paquete **`odin/spn/hooks/tunnelopts`**: el cuerpo de Go
`DeriveTunnelOptions` (navigator + captain, sin firewall) para que
`cmds/spnclient` arme opciones igual que el firewall sin linkear el firewall.

## Hooks

| Hook | Go | Lo llena | Test |
|---|---|---|---|
| `firewall.deps.captain_client_ready` | `captain.ClientReady` | `spn/hooks` → `captain.client_ready` | `test_install_hooks`, `test_firewall_tunnel_path` ("SPN not ready for tunneling") |
| `firewall.deps.captain_is_excepted` | `captain.IsExcepted` | `spn/hooks` | `test_install_hooks` |
| `firewall.deps.navigator_derive_tunnel_options` | `firewall/tunnel.go DeriveTunnelOptions` | `spn/hooks.derive_tunnel_options` (lee perfil) + `tunnelopts.build_tunnel_options` | `test_build_tunnel_options`, `test_home_hub_trust_upgrade`, `test_derive_from_profile`, `test_firewall_tunnel_path`, e2e paso 5 |
| `navigator.deps.derive_tunnel_options_for_profile` (dirección inversa) | `tunnel.go init()` → `navigator.DeriveTunnelOptions` + lookup de perfil de `api_route.go` | `spn/hooks.derive_tunnel_options_for_profile` (llama a `firewall.derive_tunnel_options`) | `test_derive_from_profile` (perfil, `global`, inexistente) |
| `firewall.deps.sluice_await_request` | `sluice.AwaitRequest(conn, crew.HandleSluiceRequest)` | `crew/hooks` (ya estaba; ahora vía `spn/hooks`) | `test_firewall_tunnel_path` (request_tunneling → "sluice for network tcp4 is offline") |
| `broadcasts.deps.get_spn_status_json` | `captain.GetSPNStatus` | `spn/hooks` → `captain.get_spn_status_json` | `test_install_hooks` |
| `broadcasts.deps.get_user` | `access.GetUser` | `access/hooks` (ya estaba) | `access/hooks` test |
| `core.deps.captain_add_to_debug_info` | `captain.AddToDebugInfo` | `spn/hooks` | `test_install_hooks` |
| `network.deps.may_use` | `access.GetUser().MayUse` | `access/hooks` (ya estaba) | `access/hooks` test |
| `network` `tunnel_opts` / `tunnel_context` (Opaque_Value) | `TunnelOpts` / `TunnelContext` | `navigator.options_to_opaque` / `crew.tunnel_context_vtable` (ya existían; comentarios corregidos) | firewall path test, e2e |
| `ivpn.deps.spn_set_bind_addr` | `conf.SetBindAddr` | `spn/hooks` | `test_install_hooks` |

`profile`: no tenía hooks (sólo la constante `FEATURE_HISTORY` y las quick
settings que ya genera `navigator.update_config_quick_settings`); comentarios
actualizados. `tunnel.odin:133` PORT-TODO viejo → comentario real.
`firewall/config.odin`: spn/enable y useCommunityNodes los registra
`captain.prep_config` (comentario corregido; `control` decía `spn/access`, mal).

## Pasó a "instance (6)" con proc proveedora

| Campo `Instance.*` | Proveedor nuevo |
|---|---|
| firewall / resolver `add_spn_connected_callback` | `captain.add_spn_connected_callback(captain_module, name, fn)` |
| firewall `add_account_update_callback` | `access.add_account_update_callback(access_module, name, fn)` |
| interop (+ivpn) `add_spn_connecting_hook` | `captain.add_spn_connecting_hook(captain_module, name, hook, user)` (descarta el Announcement, como lo ignora Go) |
| control / access / captain `spn_group` | `mgr.new_extended_group(cabin, navigator, captain, crew, docks, patrol, ships, sluice, terminal)` (Go `instance.go`) |

Los tres proveedores están testeados (`test_instance_providers`; el
`add_spn_connected_callback` además cableado en el `Instance` del firewall
del test como lo hará la etapa 6).

## Memoria (diferencia de diseño con Go)

Go comparte las políticas por GC. En el port las políticas del perfil se
liberan al reparsear y la global al recargar config → las opciones de un
túnel las **copian**: `profile.clone_stacked_{transit,exit}_hub_policies`
(copia bajo `cfg_lock`) + `navigator.options_take_hub_policies` (las
opciones se adueñan; `options_copy` / `options_destroy` / `options_avoid_hub`
ya las manejan). La política DNS de captain también se copia. El test
verifica que tras borrar la política global las opciones siguen intactas.
El hook bloquea el layered profile (`lock_for_usage`) mientras lo lee.

## Bugs / carreras de Go

* `DeriveTunnelOptions` lee el layered profile sin lock (`requestTunneling`
  no lo bloquea): carrera de datos. El port bloquea.
* `Main.GetHome()` + `homeNode.State` se lee sin el lock del pin: el port usa
  `pin_get_state`.
* API de simulación de rutas con `profile=global`: Go crea un perfil nuevo
  (UUID aleatorio, `pushLayeredProfile` a la base) **por request**; el port
  crea uno y lo reusa (mismo resultado, sin basura).
* (ya resuelto en ronda anterior) el matcher de Go apendea políticas de intel
  al backing array de `HubPolicies` (cap +2) de las opciones compartidas.

## Tests

* `odin/spn/hooks/hooks_test.odin` (6 tests): servicetest + módulo profile +
  firewall (arrancado) + captain (sólo prep). El camino del túnel del
  firewall sin kext: conexión de `network.new_incomplete_connection`, proceso
  con el layered profile de un perfil real → `check_tunneling`
  (spn/enable off; cliente no listo → Failed; app sin SPN; regla de uso que
  deniega; túnel → `Reroute_To_Tunnel`) → `request_tunneling` (opciones
  derivadas del perfil en `conn.tunnel_opts`, entregadas a sluice/crew). Caso
  DNS interno del propio proceso, upgrade por home no confiable (mapa con
  home), DNS exit policy de captain.
* e2e `odin/tests/spn_e2e` paso 5 (nuevo): `spnclient -routing home
  -exit-policy "- 127.0.0.1"` → sube a single-hop y sale por B (path A,B);
  política que niega ambos hubs → sin ruta (exit 4). `spnclient` arma ahora
  todas sus opciones con `tunnelopts.build_tunnel_options`.

## Resultados (3× foreground)

| Paquete | r1 | r2 | r3 |
|---|---|---|---|
| spn/hooks (6) | ok | ok | ok |
| spn/navigator (24) | ok | ok | ok |
| spn/captain (6) | ok | ok | ok |
| spn/crew/hooks (3) | ok | ok | ok |
| spn/access/hooks (1) | ok | ok | ok |
| spn/access (14) | ok | ok | ok |
| service/profile (6) | ok | ok | ok |
| service/broadcasts (4) | ok | ok | ok |
| service/core (7) | ok | ok | ok |
| service/network (27) | ok | ok | ok |
| service/control (2) | ok | ok | ok |
| service/interop (2) | ok | ok | ok |
| service/interop/ivpn (8) | ok | ok | ok |
| service/resolver (46) | ok | ok | ok |
| service/firewall (16) | ok | ok | ok |
| tests/spn_e2e | ver nota | ok | ok |

e2e: la 1ª corrida en el árbol vivo falló en el **paso 3** (hub A se cae al
iniciar la grúa, el cliente nunca queda listo) mientras el otro agente
editaba `spn/access/token`/`pblind`. En una copia scratch con esos dirs en
HEAD pasó 3/3; después, en el árbol vivo pasó 3/3 más (r4–r6, ~47 s).

Crosscheck `VET=1 … windows_amd64 linux_amd64 darwin_arm64` sobre los 18
dirs tocados: **0 fallas**.

## Archivos

Nuevos: `odin/spn/hooks/{hooks,tunnel,hooks_test}.odin`,
`odin/spn/hooks/tunnelopts/tunnelopts.odin`.
Código: `profile/profile-layered.odin` (clone_stacked_*),
`spn/navigator/options.odin` (options_take_hub_policies),
`spn/captain/module.odin`, `spn/access/module.odin` (proveedores),
`cmds/spnclient/main.odin`, `tests/spn_e2e/spn_e2e_test.odin`.
Sólo comentarios: firewall (deps, tunnel, config, module), broadcasts, core,
network, profile, interop(+ivpn), control, resolver, captain/status,
navigator/deps, crew/hooks, access/hooks.
