# Etapa 6, ronda C2 — instancia SPN fiel + hub sobre cmdbase, QuickSettings SPN, cliflags → cobra

Fork `DEV/gitclones/portmaster`, rama `port/odin`, sin commitear. Nada toca red que no sea loopback, ni el driver, ni admin, ni DNS/firewall del sistema.

## Qué cambió

**1. `spn/instance` = port fiel de `spn/instance.go` (+ `spn/debug.go`).** Reescrito sobre los patrones de `service/instance.odin`: `new_instance(^service.Service_Config)` crea, en el orden de Go, base, database, config, api, metrics, runtime, rng, binary/intel updates (binario con AutoDownload+AutoApply como Go), core, geoip, netenv (+ `netenv.install_hooks` para filterlists), filterlists (creado pero **fuera del grupo**, como Go), access, cabin, navigator, crew, docks, patrol, ships, sluice, terminal, captain; un solo `service_group`. Sin UI ni notifications (Go devuelve nil), `SPNGroup()` = nil. Métodos de Go: start/stop/ready/restart (exit 23)/shutdown (CAS)/ctx/shutdown_ctx/exit_code/set_sleep/get_states/add_states_callback/cmd-line op/get_worker_info/add_worker_info_to_debug_info/get_event_spn_connected. Hooks: core→configure (URLs), core→captain debug info, captain→intel updater (`main-intel.yaml`).
- `service.new_rng_module` pasó a público (lo reusa).
- `service/configure`: `default_binary_index_name` / `default_intel_index_name` (las vars `DefaultBinaryIndexName` de Go; `service/config.odin` las usa). El hub pone "SPN Binaries" y las URLs `spn-*.v3.json`.
- `spn/captain/intel.odin`: si hay **intel local configurado gana sobre el updater** (antes el updater, si existía, tapaba el archivo; con la instancia fiel el hub tiene updater).

**cmds/hub** sobre `lib/cobra` + `cmds/cmdbase` como Go: root `spn-hub` (persistentes `--bin-dir/--data-dir`; `--log-stdout --log-dir --log --print-stack-on-exit --reboot-on-restart`; flag set de Go vía `cmdbase.register_go_flags` → `--api-address --devmode --spn-map --bootstrap-hub --bootstrap-file …`; comandos `version` y `update`), `initialize_globals` = el de Go (info, metrics "hub", user agent, public hub, índices SPN, defaults `/opt/safing/spn`) y `cmdbase.run_service` con `spn_factory` (Service_Instance sobre la instancia SPN). El `start` envuelto escribe `<data>/hub-bootstrap.json` e imprime `HUB_ID/HUB_BOOTSTRAP/HUB_READY`; con `--init` hace shutdown enseguida (exit 0). `build_linux.sh` sin cambios: el `.o` cruzado se genera y sus símbolos indefinidos siguen siendo sólo libc/libm/pthread/ssl/crypto/sqlite3 (mismo gcc).

**cmds/spnclient** se queda con su instancia mínima propia (`client_instance.odin` + `signals.odin`, movidos desde el viejo spn/instance; Go no tiene spnclient) y usa `lib/cobra` para los flags.

**2. QuickSettings SPN (round-C TODOs cerrados).**
- `captain/config.odin`: `QuickSettingsAnnotation = profile.SPN_RULES_QUICK_SETTINGS` ("Loading...") en `spn/homePolicy` y `spn/dnsExitPolicy`.
- `captain/client.odin`: al conectar, `if conf.is_integrated()` → worker "update quick setting countries" = `navigator.update_config_quick_settings(main_map)`.
- `navigator/update.odin`: `update_select_rule_country_list` con **FlagID** (Go `selectCountry`). Nuevo `config.Select_Country` (`using quick_setting` + `flag_id \`json:"FlagID"\``) en la unión cerrada de anotaciones (`[]Select_Country`; clone/equal/destroy).
- `base/config`: una lista vacía generada se serializa `[]` (Go `make(…,0,n)`), no `null` (en Odin un slice vacío tiene data nil → centinela en el clon).
- `service/instance.odin`: comentario actualizado.

**3. `lib/cliflags` fusionado en `lib/cobra`** (lo hizo un fork mío): shorthands (`-p v`, `-pvalue`), `string_var_p/bool_var_p/int_var_p`, `mark_flag_required`, `Command.exact_args: Maybe(int)` (ExactArgs), `data`, `new_command/destroy_command`, `execute_c` (ExecuteC) y `execute` = `.err`, legacyArgs antes del help, `parse_go_flags/print_go_defaults` (paquete `flag`). updatemgr, trafficgen, winkext-test, integrationtest y testsuite usan `pm:lib/cobra`; `lib/cliflags` borrado. Diferencias menores de texto: error de valor en modo Go = `invalid value "x" for flag -n: parse error` (Go), shorthand desconocido `unknown shorthand flag: 'x' in -x` (pflag).

## Flags Odin del hub (no existen en Go)

| Flag | Efecto |
|---|---|
| `--version` | versión y salir (Go: sólo el comando `version`) |
| `--name --ip4 --ip6 --transports --exit-policy` | defaults de `spn/publicHub/*` |
| `--self-hosted-zone-file`, `--intel-file` | zona propia (verificadores), intel local (gana sobre el updater) |
| `--spn-map-scope public\|local\|test` | `conf.main_map_scope` |
| `--bind-ip` | `conf.set_bind_addr` |
| `--patrol=false` | patrol fuera del grupo (HTTPS a Internet) |
| `--geoip=false` | **nuevo**: sin módulo geoip (offline/self-hosted no hay intel; con el módulo el navigator espera 30 s las bases —igual que Go— antes de enganchar el hook de hubs y el e2e falla) |
| `--device-location=false`, `--allow-local-destinations`, `--optimize-delay` | como en la etapa 5 |
| `--automatic-updates=false` | **nuevo**: default false de `core/automaticUpdates` y `core/automaticIntelUpdates` |
| `--online-check=false` | **nuevo**: netenv sin sondas a Internet (portal → 127.0.0.1:9 cerrado, sin consulta DNS) |
| `--init` | arranca, escribe el bootstrap y sale |

Mecánica: los defaults los aplica un módulo Odin `ConfigDefaults` justo después de `Config` (todas las opciones ya registradas). Desvíos: la API sólo escucha con `--api-address` (`api.enable_server=false`; 127.0.0.1:817 choca con un Portmaster local y con un segundo hub); en Windows dirs por defecto `./spn-hub-data` y sin ACLs restrictivas; captain arranca último (sus piers necesitan terminal/docks/crew arriba). El e2e y `manual_local.sh` agregan `-automatic-updates=false -online-check=false -geoip=false`.

## QuickSettings: JSON como lo recibe la UI (`GET /api/v1/config/options`)

Red de test: A (DE, home) y B (AT), ambos Trusted, VerifiedOwner "SelfHosted":
```
spn/homePolicy:     [{"Name":"Exclude Austria (AT)","Value":["- AT"],"Action":"merge-top"},{"Name":"Exclude Germany (DE)","Value":["- DE"],"Action":"merge-top"}]
spn/transitHubPolicy: [{"Name":"Exclude Austria (AT)","Value":["- AT"],"Action":"merge-top"}]
spn/exitHubPolicy, spn/dnsExitPolicy:
  [{"Name":"European Union","Value":["+ AT",…,"+ SK","- *"],"Action":"replace","FlagID":"EU"},
   {"Name":"US and Canada","Value":["+ US","+ CA","- *"],"Action":"replace","FlagID":""},
   {"Name":"Austria (AT)","Value":["+ AT","- *"],"Action":"replace","FlagID":"AT"},
   {"Name":"Europe (C:EU)","Value":["+ C:EU","- *"],"Action":"replace","FlagID":""}]
spn/trustNodes:     [{"Name":"Trust SelfHosted","Value":["SelfHosted"],"Action":"merge-bottom"}]
antes de conectar:  [{"Name":"Loading...","Value":[""],"Action":"merge-top"}]
```

## Bugs de Go

Corregidos: `geoip.New` falla con "create customlist module" (copy-paste, también en spn/instance.go); `shutdown` chequea `ctx.Err()` y después cancela (dos shutdowns paran dos veces) → CAS.
Conservados: filterlists se crea pero nunca entra al grupo (nunca arranca); hub sin datos de geoip espera 30 s en el navigator (de ahí `--geoip=false`); comentarios copy-paste (`Docks()`/`Sluice()` "returns the crew/ships module").

## Trampas

- `odin test` borra el exe al terminar: las 3 corridas son 3 `odin test`.
- `git rm` toca el índice: lo deshice con `git reset -- <paths>` (sólo working tree).
- Diagnóstico del e2e: con geoip presente y sin bases, `hub.get_hub` devuelve el pin bootstrap del mapa (sin clave pública) porque el hook de actualización del navigator se registra después de la espera de 30 s → "hub msg ID … unknown (missing announcement)" en ambos hubs → el cliente no ve el status de B → "all pins have been disregarded". Lo confirmé corriendo el hub viejo (HEAD) en un árbol aparte: no espera y aplica el intel enseguida.
- Heredoc + python: `\n` en un string Odin se volvió salto real (otra vez).

## Tests (primer plano, `odin test … -collection:pm=odin -vet`, 3 corridas)

| Paquete | Tests | r1 | r2 | r3 |
|---|---:|---|---|---|
| cmds/hub (nuevo: flags de Go + Odin, comandos, scopes) | 1 | ok | ok | ok |
| spn/instance (nuevo: orden de Go, hooks, start como hub público en loopback, defaults, API, shutdown exit 0 sin workers) | 1 | ok 5 s | ok | ok |
| spn/captain (+ placeholder de QuickSettings) | 6 | ok | ok | ok |
| spn/navigator (+ `test_config_quick_settings`, JSON por la API) | 25 | ok | ok | ok |
| base/config (+ Select_Country / `[]`) | 17 | ok | ok | ok |
| lib/cobra | 4 | ok | ok | ok |
| cmds/cmdbase | 3 | ok | ok | ok |
| cmds/updatemgr · trafficgen · winkext-test · integrationtest · testsuite | 8 · 4 · 2 · 1 · 1 | ok | ok | ok |
| service · service/configure | 4 · 2 | ok | ok | ok |
| tests/spn_e2e (exes precompilados) | 1 (~41 s) | ok | ok | ok |
| tests/core_e2e (exe precompilado) | 1 | ok | ok | ok |

Sin leaks. Linux: `bash odin/cmds/hub/build_linux.sh <dir>` → `spn-hub.o` (7,6 MB); `llvm-nm -u`: sólo libc/libm/pthread/OpenSSL/sqlite3.
`VET=1 bash odin/tests/crosscheck.sh windows_amd64 linux_amd64 darwin_arm64 -- odin/cmds/{hub,spnclient,cmdbase,updatemgr,trafficgen,winkext-test,integrationtest,testsuite,portmaster-core} odin/spn/{instance,captain,navigator} odin/lib/cobra odin/base/config odin/service odin/service/configure odin/tests/spn_e2e` → **17×3 ok, 0 fallas** (el primer intento falló por `lib/tls/keypair.odin` a medio editar por el otro agente; reintentado cuando compiló). Sin procesos `spn-hub`/`spnclient` vivos al final.
