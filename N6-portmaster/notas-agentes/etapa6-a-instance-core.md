# Etapa 6, ronda A — `service` (instancia), `cmds/cmdbase`, `cmds/portmaster-core`

Fork `DEV/gitclones/portmaster`, rama `port/odin`, sin commitear. Ya hay un **portmaster-core de verdad**: crea los 41 módulos en el orden de Go, los cablea, arranca/para en grupos, atiende la API y el DNS, habla con el kext (falso en los tests) y sale con el código que corresponde.

## Archivos

Nuevos:
- `odin/service/{instance,config,debug}.odin` (`package pm_service`, port de `service/*.go`) + `instance_test.odin` (`#+build windows`), `config_test.odin`.
- `odin/lib/cobra/{flags,command}.odin` + `cobra_test.odin`: subconjunto de spf13/cobra + pflag (+ `flag.CommandLine` global). **Los flags de `spn/instance` se movieron acá**; `spn/instance/flags.odin` quedó como alias (hub y spnclient no cambian).
- `odin/cmds/cmdbase/{service,service_windows,service_other,update,version,go_flags}.odin` + `cmdbase_test.odin`.
- `odin/cmds/portmaster-core/{main,main_windows,main_other}.odin`.
- `odin/tests/core_e2e/core_e2e_test.odin` (`#+build windows`): el exe real como proceso hijo.

Tocados (código): `spn/captain/{module,intel}.odin` (nuevo `Instance.add_intel_updated_callback`), `service/mgr/manager.odin` (nuevo `wait_for_other_workers`), `base/rng/rng.odin` y `service/firewall/module.odin` (lo usan, ver bugs del port), `service/process/module.odin` (formato del error). Sólo comentarios (PORT-TODO(stage-6) resueltos o retaggeados): base/{api,config,metrics}, service/{core,core/base,nameserver,resolver,firewall(+deps, interception, dnsmonitor),integration,interop(+ivpn),netenv,network,splittun,control,compat,status}, spn/{conf,terminal,navigator,captain,access,hooks,crew,instance}.

`grep "PORT-TODO(stage-6)"` (fuera de lo del otro agente) ya no lista nada; quedan sólo los `PORT-TODO(stage-6, round C)` de QuickSettings. Dos pasaron a `ACCEPTED`: pprof (`-cpuprofile` se acepta e ignora) y BuildInfo de Go.

## Qué hace `service.new_instance`

1. `service_config_init` (defaults por OS: Windows bin = carpeta del exe, data = `%ProgramData%\Portmaster`, logs = data\logs; Linux /usr/lib, /var/lib, /var/log; expansión `$VAR`/`${VAR}` como `os.ExpandEnv`; nivel de log inválido → warning y default).
2. Data dir (ACL pública salvo `apply_directory_acls=false`).
3. `install_package_hooks()` (el equivalente a los `init()`/imports de Go): coreupdater (configure), compat, resolver, process, netquery, firewall, **spn/hooks** (que a su vez corre access/hooks y crew/hooks) y `captain.deps.get_intel_update`. netenv se instala justo después de `new_net_env` y antes de `new_filter_lists` (llena el callback de online status de filterlists).
4. Los 41 módulos en el orden de `instance.go New` (base, database, config, api, metrics, runtime, notifications, rng, binary/intel updates, core, integration, geoip, netenv, ui, profile, network, netquery, firewall, filterlists, interception, dnsmonitor, customlists, status, broadcasts, compat, nameserver, process, resolver, sync, control, interop, access, splittun, cabin, navigator, captain, crew, docks, patrol, ships, sluice, terminal).
5. Grupos como Go: `Interception Group` (interception, dnsmonitor, compat) dentro del grupo de servicio (orden de Go, interop antes del grupo de intercepción, access último) y el **extended group SPN** (cabin, navigator, captain, crew, docks, patrol, ships, sluice, terminal), que arranca/para access según `spn/enable` (igual que Go; `stop` de la instancia sólo para el grupo de servicio).

Métodos: `start/stop/ready/restart/shutdown` (shutdown async en un manager "instance", exit 0; restart → evento `core.event_restart`, `should_restart`, exit 23), `ctx/is_shutting_down/wait_shutting_down`, `shutdown_ctx/is_shut_down/wait_shutdown_complete`, `exit_code`, `get_states/add_states_callback`, `set_sleep` (metrics, network, captain), `get_binary_update_file`, `set_cmd_line_operation` + `command_line_operation_*`, `add_module`, `get_worker_info/add_worker_info_to_debug_info` (debug.go).

## Cableado `Instance.*` → proveedor

| Módulo.campo | Go | Proveedor |
|---|---|---|
| base/config/api `set_cmd_line_operation` | `SetCmdLineOperation` | `set_cmd_line_operation(i)`; New devuelve `Group_Error.Execute_Cmd_Line_Op` (config/api lo piden) |
| base/config/dbmodule/metrics/ui/profile/netquery/core `data_dir` | `DataDir()` | `i.data_dir` |
| api/config/core/firewall/customlists/profile/resolver/control/interop/access/splittun/captain `config` | `Config()` | `i.config` |
| api `ready` | `Ready()` | `group_ready(service_group)` |
| updates `restart/shutdown/notifications/ui_*_upgrade_lock` | Restart/Shutdown/Notifications/UI | métodos de la instancia, `ui.enable/disable_upgrade_lock` |
| core `shutdown/restart/add_worker_info_to_debug_info/bin_dir/log_dir` | idem | instancia, debug.odin |
| core/geoip/broadcasts/filterlists `binary_updates/intel_updates` (`^core.Updater`) | `BinaryUpdates()/IntelUpdates()` | `coreupdater.new_core_updater(updater)` |
| process `binary_updates` | `BinaryUpdates()` | `^updates.Updater` |
| integration | `BinaryUpdates()` | `integration.instance_from_updater(binary)` |
| interception `get_binary_file`, ui `get_binary_update_file` | `GetFile(name).Path()` | `get_binary_update_file` |
| dnsmonitor `get_etw_interface / add_on_initialized_callback` | `OSIntegration()` | `integration.get_etw_interface`, callback en `on_initialized_event` |
| netenv `submit_intel_resources_updated` | `IntelUpdates().EventResourcesUpdated.Submit` | `event_mgr_submit(intel.event_resources_updated)` |
| filterlists `add_online_status_callback` | `NetEnv().EventOnlineStatusChange` | `netenv.install_hooks(&i.filterlists_instance)` |
| network/profile, netquery/profile, firewall/profile | `Profile()` | `i.profile` |
| network `dns_monitor` | `DNSMonitor()` | `i.dnsmonitor` |
| network/firewall/compat `resolver_is_disabled` | `Resolver().IsDisabled()` | `resolver.is_disabled(i.resolver)` |
| firewall `bin_dir`, `binary_updates_main_dir`, `network` | BinDir / GetMainDir / Network | instancia / `updates.get_main_dir` / `i.network` |
| firewall/resolver `add_spn_connected_callback` | `Captain().EventSPNConnected` | `captain.add_spn_connected_callback(i.captain, …)` |
| firewall `add_account_update_callback` | `Access().EventAccountUpdate` | `access.add_account_update_callback(i.access, …)` |
| resolver `shutting_down`, control/access `is_shutting_down` | `ShuttingDown()` | `is_shutting_down(i)` |
| resolver/compat/captain `net_env` | `NetEnv()` | `i.netenv` |
| status `get_states/add_states_callback` | `GetStates/AddStatesCallback` | instancia (bajando al grupo de intercepción, ver bugs) |
| control `interception_group` | `InterceptionGroup()` | `i.service_group_interception` |
| control/access/captain `spn_group` | `SPNGroup()` | `i.spn_group` |
| interop `interception`, `add_spn_connecting_hook` | `Interception()`, `GetHookSPNConnecting().AddHook` | `i.interception`, `captain.add_spn_connecting_hook` |
| captain `patrol` | `Patrol()` | `i.patrol` |
| **captain `add_intel_updated_callback`** (nuevo) | `IntelUpdates().EventResourcesUpdated.AddCallback("update SPN intel")` | callback en el evento del intel updater → `update_spn_intel` |
| `captain.deps.get_intel_update` | `IntelUpdates().GetFile("main-intel.yaml")` | `updates.get_file` (path + versión) |

Flags de Go (`flag.CommandLine`, registrados por `cmdbase.register_go_flags` apuntando a las variables de cada paquete y agregados al root como `AddGoFlagSet`): api-address, export-api-endpoints, devmode, export-config-options, metrics-instance/-comment, push-metrics, cpuprofile (aceptado, sin profiler), network-service, write-packet-metrics, disable-interception, **nameserver-address**, **allowed-clients** (`flag.Var`, repetible → `firewall.add_allowed_client`), bootstrap-hub/-file (setters de captain), **spn-map**, **debug-unit-scheduling**.

## cmds/cmdbase y portmaster-core

- `lib/cobra`: Command (use/short/long, run/run_e, persistent_pre_run), flags locales y persistentes, subcomandos, `help`/`-h`, error "unknown command … for …", usage tipo cobra, `flag_func` (flag.Var). Sintaxis tolerante `-x v`/`--x=v` (Go flag y pflag no coinciden; el set de Go se usa con un guion). Sin shorthands (Portmaster no define).
- `run_service` = RunService: log → factory → cmdline op (exit 0/3) → system service en un hilo → espera ShuttingDown → ShutdownComplete con 3 min (si no: "stack" + `on_shutdown_timeout` = `interception.emergency_stop(10s)`) → restart del servicio (si era servicio y `should_restart`; `reboot_on_restart` sólo hace algo en Linux, como Go) → `log.shutdown`, 100 ms, exit con el código de la instancia. `run_service_code` devuelve el código (testeable).
- Windows: `is_windows_service` = padre `services.exe` en sesión 0 (lo mismo que `svc.IsWindowsService`, con toolhelp + `ProcessIdToSessionId`). Como servicio: `StartServiceCtrlDispatcherW` → `RegisterServiceCtrlHandlerExW` → Execute con `SetServiceStatus` (StartPending/Running/StopPending/Stopped, Stop/Shutdown/Interrogate). Desde consola ("debug execution"): `SetConsoleCtrlHandler` (Ctrl+C/Break = interrupt; close/logoff/shutdown = SIGTERM, bloquea el handler como el runtime de Go). Forzado tras 5 señales. Restart por el script de PowerShell de Go. `ensureQuotedServiceImagePath` (CWE-428) portado con RegGetValueW/RegSetValueExW, **sólo como servicio**.
- Unix (`service_other.odin`): señales posix (HUP/INT/TERM/QUIT, USR1 = stack), servicio si ppid == 1 (gopsutil → `os.get_ppid`), `PORTMASTER_RESTART_COMMAND` o `systemctl restart portmaster`. Compila en linux/darwin, **nunca corrido** (PORT-TODO(unix)). `recover-iptables` = stub con error (nfqueue no portado).
- portmaster-core: mismos flags que Go (persistentes `--bin-dir/--data-dir`; `--log-stdout --log-dir --log --version --print-stack-on-exit`; comandos `version`, `update`), `info.set`, metrics namespace, user agent "Portmaster Core (windows amd64)", `conf.enable_client/enable_integration`, trust stores de configure. **Agregado Odin: `--unprivileged`** (ver desvíos).

## Qué toca el sistema y cómo se evita (inyectable)

| Arranque de Go | Inyección (test / corrida manual) |
|---|---|
| kext: crea/arranca el servicio del driver (admin) | test: `windowskext2.device_config` → pipe de `fakekext`, `skip_service`; manual: `--disable-interception` |
| nameserver en `0.0.0.0:53` | `--nameserver-address` (test: `127.0.0.1:0`, `nameserver.listen_port()`) |
| API en `127.0.0.1:817` | `--api-address` (test: `127.0.0.1:0`) |
| ACL de data/databases/history/profiles/updates (en Windows reemplaza el DACL: un proceso no elevado se queda sin acceso a sus propios dirs; lo comprobé) | **`service.disable_directory_acls()`** / `--unprivileged` |
| updates automáticos (Internet a los 15 s) | `config.json`: `core/automaticUpdates`, `core/automaticIntelUpdates` = false |
| resolver a DoH/DNS de Internet, mDNS | `config.json`: `dns/nameservers` = puerto loopback cerrado, `noAssignedNameservers`, `noMulticastDNS` |
| notificaciones al notifyd del escritorio | `WNOTIFY_PIPE` privado |
| compat self-check: paquete raw (admin) + lookup por el DNS del sistema | `compat.deps` (test) |
| netenv portal check a 192.0.2.1 | `netenv.portal_test_url` (test: loopback cerrado) |
| auth de la API por proceso en bin dir | `--devmode` (o correr el cliente desde el bin dir / `--allowed-clients`) |

Nada carga drivers, cambia DNS/red/firewall ni pide admin.

## Queda para la ronda C

QuickSettings de las reglas SPN (`UpdateConfigQuickSettings`, FlagID de los países; `captain/client.odin`, `captain/config.odin`, `navigator/update.odin`), `cmds/observation-hub` (necesita `text/template`), migración del hub. `spn/instance` queda como instancia mínima del hub/spnclient a propósito.

## Bugs de Go encontrados

Corregidos:
1. **`GetStates`/`AddStatesCallback` no bajan al `GroupModule`** (no es StatefulModule): los estados de compat (self-check) e interception nunca llegaban a `runtime:system/status`. La instancia baja al grupo de intercepción.
2. **Windows: si `Start` falla**, `Execute` vuelve, `svc.Run`/`debug.Run` vuelven y `RunService` queda esperando un shutdown que no llega: el proceso cuelga. El port sale con 1 (como Linux).
3. `shutdown`: chequea `ctx.Err()` y después cancela → dos shutdowns concurrentes paran los módulos dos veces. CAS.
4. Mensajes copy-paste: notifications dice "create runtime module", geoip "create customlist module".
5. `MakeUpdateConfigs` en otros OS devuelve configs nil y `New` las desreferencia (panic): el port devuelve error.

Conservados: `Init` en otros OS repite "binary directory must be configured" para el data dir; `debug.Run` + `signal.Notify` entregan un Ctrl+C dos veces (señal + Stop) → la cuenta de forzado queda en 4 (el port cuenta uno).

## Bugs del port encontrados (por la instancia completa)

- **`rng.stop` y `firewall_stop` se esperaban a sí mismos**: el grupo corre `stop` como worker del propio manager (`manager_do`), y `wait_for_workers` contaba ese worker → rng fallaba a 1 min ("Not_Ready", el grupo quedaba Invalid) y el firewall perdía 5 s. Nuevo `mgr.wait_for_other_workers` (descuenta al llamador si es worker del manager).
- `make_update_configs`: `ignore = []string{"uninstall.exe"}` era un slice en la pila (basura en el updater) → array del paquete.
- process: el error del path de la UI se imprimía con `%v` (un buffer de 512 bytes en el log).
- Los `start/stop` de los módulos corren en el hilo del llamador: la instancia fija `context.allocator = heap` (si no, en tests caían en el tracking allocator).

## Desvíos

- `lib/cobra` (sintaxis tolerante, sin shorthands). Ojo: el otro agente creó en paralelo `odin/lib/cliflags` (updatemgr); conviene unificar con `lib/cobra` (que ya tiene `flag.CommandLine`, subcomandos y `flag_func`).
- `--unprivileged` / `apply_directory_acls`: sólo Odin, para corridas manuales/test no elevadas.
- `print_stack_to`: sin stacks de otros hilos; imprime el worker info de todos los managers.
- `Rng_Module` adapta base/rng (no es mgr.Module en el port).
- Hooks instalados en `new_instance` (no hay `init()` con acceso a otros paquetes); flags de Go registrados por cmdbase.
- Detección de servicio por toolhelp (misma regla que x/sys/windows/svc).

## Tests (3× en primer plano, `odin test … -collection:pm=odin -vet`)

| Paquete | Tests | r1 | r2 | r3 |
|---|---:|---|---|---|
| lib/cobra | 2 | ok | ok | ok |
| spn/instance | 1 | ok | ok | ok |
| **service** (wiring + instancia completa + config) | 4 | ok 6 s | ok 3,6 s | ok 3,7 s |
| **cmds/cmdbase** (flujo RunService con instancia falsa, flags de Go, detección de servicio) | 3 | ok | ok | ok |
| **tests/core_e2e** (exe real; r3 compilándolo él mismo) | 1 | ok | ok | ok |
| base/rng · service/mgr · spn/captain · service/process | 8 · 28 · 6 · 17 | ok | ok | ok |
| service/firewall · resolver · nameserver · control | 16 · 46 · 10 · 2 | ok | ok | ok |
| spn/hooks · tests/spn_e2e (exes precompilados) | 6 · 1 (~41 s) | ok | ok | ok |
| tocados sólo en comentarios: base/{api,config,metrics}, service/{core,core/base,firewall/interception,dnsmonitor,integration,interop,ivpn,netenv,network,splittun,status,compat,updates/coreupdater}, spn/{conf,terminal,navigator,access} | 20 paquetes | ok | ok | ok |

`test_instance` (una sola instancia por proceso): orden exacto de los grupos con los nombres de manager; **reflexión sobre cada `Instance` de 30 módulos: todo proc no nil**; `deps` de core/firewall/network/broadcasts/status/captain/crew/navigator instalados; start → `ready`, grupo SPN apagado; API: ping, ready, `config/options`, websocket `get runtime:system/status` y `get config:core/automaticUpdates` (Value false); el nameserver responde una consulta DNS; el fake kext se conecta y una conexión de él recibe un verdict; worker info; `shutdown` → exit 0, grupo Off, **ningún worker vivo** (salvo los worker managers on-demand de captain, que Go también crea en New), puertos de API y DNS cerrados, kext desconectado. ~3,5 s.
`core_e2e`: `version` → 0, comando inválido → 1, `--export-config-options` → 0 (cmdline op), servicio con flags seguros → ping/ready/DNS, `POST /api/v1/core/shutdown` → exit 0. Hijo parado por handle si algo falla. Sin dirs temporales ni procesos al final.

Crosscheck: `VET=1 bash odin/tests/crosscheck.sh windows_amd64 linux_amd64 darwin_arm64 -- <39 dirs tocados>` → **0 fallas**.

## Cómo correr portmaster-core a mano sin tocar el sistema

```
odin build odin/cmds/portmaster-core -collection:pm=odin -out:portmaster-core.exe
mkdir T\data T\bin
T\data\config.json:
  {"core": {"automaticIntelUpdates": false, "automaticUpdates": false},
   "dns": {"nameservers": ["dns://127.0.0.1:59999"], "noAssignedNameservers": true, "noMulticastDNS": true}}
set WNOTIFY_PIPE=\\.\pipe\pm-manual-core      (no molestar al notifyd del escritorio)
portmaster-core --data-dir T\data --bin-dir T\bin --log-stdout --log info ^
  --unprivileged --disable-interception --devmode ^
  --nameserver-address 127.0.0.1:15353 --api-address 127.0.0.1:18170
curl http://127.0.0.1:18170/api/v1/ping                 → Pong.
curl -X POST http://127.0.0.1:18170/api/v1/core/shutdown → sale con 0 (Ctrl+C también)
```
`sqlite3.dll` en el PATH (vcpkg). Elegir puertos fuera de los rangos excluidos de Windows (`netsh int ipv4 show excludedportrange protocol=udp`: acá 53397–53896 está reservado y da `bind: Invalid_Argument`). Sin `--disable-interception` intentaría crear el servicio del driver (falla sin admin; **no hacerlo**). Sin `--nameserver-address` bindea 0.0.0.0:53. Matar sólo por PID.
