# Ronda unix: service/process (+tags), network/state, network/proc, profile/binmeta — informe

## Archivos

- **service/process**: `info_linux.odin` (nuevo: capa de info sobre /proc, reemplaza gopsutil v3.21.11 linux/posix), `process_linux.odin` (nuevo: process_linux.go, líder de grupo con getpgid, `SYSTEM_PROCESS_ID=0`, `SYSTEM_INIT_ID=1`), `process_other.odin` (ahora `#+build !windows` + `#+build !linux` = darwin/BSD, process_default.go + stub con `PORT-TODO(darwin)`), `process.odin` (load_process: UID en Linux, recorte de `" (deleted)"`, Cwd por readlink antes del cmdline como Go; en Windows sigue saliendo del PEB), `find.odin` (find_pid_by_local_endpoint resuelve el PID con `state.check_pid`: en Linux las tablas traen inodo, no PID). Tests: `process_windows_test.odin` (nuevo, `#+build windows`: test_find_own_process, resolver svchost, group leader 404 movidos tal cual), `process_linux_test.odin` (nuevo), `process_test.odin` (Kernel::<SYSTEM_PROCESS_ID>, profile_wiring con preferencia de líder, `itoa` → `@(private)`).
- **service/process/tags**: sólo `tags_test.odin` (7 handlers en Windows, 5 en el resto; ruta del script con filepath.join).
- **service/network/proc** (paquete nuevo `pm_proc`): `tables.odin` (parser portable de /proc/net/{tcp,udp}{,6}), `socket_owners.odin` (caché de dueños, portable), `findpid_linux.odin` (lectura de /proc/net, get_pid/find_pid, pids_by_user, singleflight de int). Tests: `tables_test.odin`, `socket_owners_test.odin` (portables), `findpid_linux_test.odin`.
- **service/network/state**: `system_linux.odin` (nuevo: system_linux.go, check_pid con 5 intentos 5/10/15/20 ms), `system_other.odin` (darwin/BSD, `PORT-TODO(darwin)`), `system_windows.odin` y `lookup.odin` (check_pid se movió a los archivos por OS).
- **service/profile/binmeta**: `find_linux.odin` (nuevo: find_linux.go + locations_linux.go), `find_other.odin` (darwin/BSD, `PORT-TODO(darwin)`), `name.odin` (`:` separa sólo en Windows, como filepath.Split). Tests: `find_linux_test.odin`, `name_windows_test.odin`, `name_other_test.odin` (nuevos), `name_test.odin`, `icons_test.odin` (dir temporal portable, `../` en todos los OS y `..\` sólo en Windows).
- Tests de otros paquetes (sólo supuestos Windows): `service/profile/profile_test.odin` (binario de "Named App" con ruta nativa por OS), `service/firewall/firewall_test.odin` (check_relation con proceso nativo por OS; "otra raíz" = `/usr/share` fuera de Windows). network, nameserver, netquery: sin cambios (fallaban sólo en el setup).

## Portado vs PORT-TODO

Portado todo lo `_linux.go` del alcance: process_linux.go, la parte linux/posix de gopsutil que usa loadProcess, network/proc completo (tables, findpid, pids_by_user, socket_owners + sus tests), state/system_linux.go, binmeta find_linux.go + locations_linux.go (+ find_linux_test.go como test manual). No queda `PORT-TODO(unix)` en el alcance.
`PORT-TODO(darwin)`: (1) capa de info de procesos (gopsutil darwin usa sysctl KERN_PROC/KERN_PROCARGS2 + libproc; no hay /proc); (2) state/system_default.go: la goroutine que al minuto pone `core/enableProcessDetection` en false por defecto; (3) binmeta: Go tampoco tiene implementación darwin.

Detalles gopsutil replicados: CreateTime = `(starttime / CLK_TCK + btime) * 1000` en segundos enteros (la clave "pid-createdAt" tiene resolución de 1 s, igual que Go); Name = comm de status y, si tiene ≥15 chars, se extiende con cmdline[0]; Cmdline = FieldsFunc por NUL unido con " "; Environ = Split por NUL; PidExists = pid ≤ 0 inválido, stat de /proc/<pid>, y si no kill(pid, 0) con EPERM = existe; Username = getpwuid_r del UID real, sin fallback.

## Hooks

Ninguno nuevo. state usa `pm:service/network/proc` directo (importado con alias `netproc`: `proc` es palabra reservada).

## Tests (3 corridas)

Linux (WSL archlinux, `odin test ... -vet`). Como otros agentes tenían a medio editar lib/tls, lib/httpclient, interop/ivpn y firewall/interception (no compilaban en Linux en ese momento), las corridas se hicieron sobre un árbol aislado = HEAD del fork (ccc3736c, ya con netenv/lib/dbus de Linux) + archivos ignorados (vendor, testdata) + mi alcance (scripts `lx3.sh`/`in3.sh` del scratchpad `agent-proc/`).

| Paquete (tests) | 1 | 2 | 3 |
|---|---|---|---|
| service/process (19) | ok | ok | ok |
| service/process/tags (6) | ok | ok | ok |
| service/network/state (6) | ok | ok | ok |
| service/network/proc (10) | ok | ok | ok |
| service/profile/binmeta (11) | ok | ok* | ok |
| service/profile (6) | ok | ok | ok |
| service/firewall (15) | ok | ok | ok |
| service/network (27) | ok | ok | ok |
| service/netquery (14) | falla** | ok | ok |
| service/nameserver (10) | 3 e2e fallan*** | ídem | ídem |

\* La corrida 2 falló por una carrera del test nuevo (`test_get_icon_and_name_linux` y `test_profile_icon_storage` cambian la misma ruta global de íconos): ahora toma `lock_storage()`; después 3/3 ok.
\** `test_netquery_end_to_end`: "history must be enabled for the connection" una vez en ~12 corridas Linux (5 corridas extra seguidas: ok). No es un supuesto Windows del test ni toca mi alcance (la historia sale del perfil en capas del proceso padre); queda anotado como flake a mirar.
\*** Ver "Fallas fuera de alcance". Con el resolver/lib/tls/httpclient en curso de otro agente superpuestos: nameserver 3/3 ok y firewall 3/3 ok.

Windows (árbol aislado equivalente, 3 corridas): process 18, process/tags 9, network/state 6, network/proc 8, profile/binmeta 14, profile 6, firewall 16, network 27, netquery 14: 3/3 ok. nameserver 10: 3/3 ok. Una corrida anterior concurrente con WSL dio "dial tcp: Refused" (puertos de loopback compartidos con WSL2); corriendo solo, ok. Los 10 paquetes = 30/30 ok.

`VET=1 bash odin/tests/crosscheck.sh windows_amd64 linux_amd64 darwin_arm64 -- odin/service/process odin/service/process/tags odin/service/network/state odin/service/network/proc odin/service/profile/binmeta odin/service/profile odin/service/firewall`: 7×3 ok, 0 fallas (antes de que otro agente agregara `firewall/interception/{ebpf,nfq}`; la última pasada da firewall/linux FAIL sólo por imports sin usar en sus `*_privileged_linux_test.odin`, fuera de mi alcance).

## Fallas que quedan fuera de mi alcance (código de producto)

- firewall `test_api_authentication` (última aserción: una request no local debe devolver nil, nil): con el netenv de 32c3f2b3, `netenv.is_my_ip` fallaba en Linux (no había interfaces) y Go, ante error, sigue autenticando. Resuelto por el commit ccc3736c (netenv Linux): ahora pasa.
- nameserver e2e (`test_e2e_failing_upstream`, `test_e2e_firewall_decisions`, `test_e2e_truncation_and_tcp`; `test_failing_queries` cae por arrastre, solo pasa): `resolver/sockets_other.odin` de HEAD da "dial udp: not supported on this platform". Pasa con el resolver que está portando otro agente (superponiendo `service/resolver`, `lib/tls`, `lib/httpclient`, `service/netenv`, `lib/dbus`).

## Bugs de Go

Corregidos:
- binmeta `GetIconAndName` (linux) le pasa a `searchForIcon` la **ruta completa** del binario: la compara (en minúsculas) con nombres de íconos y la mete en `/usr/share/%s`, así que nunca encuentra un ícono para un proceso real. Se busca por el nombre de archivo (el test de Go ya llama con el nombre suelto).
- binmeta `searchDirectory` devuelve el mejor match por prefijo como nombre suelto (`entry.Name()`) sin unirlo al directorio: LoadAndSaveIcon leería una ruta relativa. Se une como el match exacto.
- network/proc `getTableFromSource` parsea UID e inodo con `ParseInt(..., 10, 32)`: los inodos de sockets son u32 (pasan 2^31 en sistemas con mucho uptime) y los UID llegan a 2^32-2, así que esas filas se descartaban y el socket nunca se encontraba. Se parsean como u32.
- process `find_pid_by_local_endpoint` (agregado del port para GetProcessByRequestOrigin) filtraba `pid != Undefined`: en Linux las tablas no traen PID, así que nunca encontraba nada. Ahora resuelve con check_pid.

Conservados: CreateTime con resolución de segundos (es la clave del proceso, tiene que ser compatible); el log de debug del caso "salimos del grupo" en FindProcessGroupLeader muestra el padre de afuera y no el líder elegido; si falla LookupId el usuario queda vacío (warning); `/proc/<pid>` de un TID también "existe".

## Trampas

- `#+build !windows !linux` en una sola línea es error de sintaxis ("Missing ','"): la coma es OR, así que el AND va en **dos líneas** `#+build !windows` / `#+build !linux` (así lo hace core).
- `import "pm:service/network/proc"` → `proc.get_pid` no parsea (`proc` es palabra clave): alias `import netproc "..."`.
- procfs informa tamaño 0: hay que leer hasta EOF (lo hacen os.read_entire_file y mi read_file).
- `/proc/<pid>/stat`: el comm puede tener espacios y paréntesis → los campos empiezan después del **último** `)`; el campo n (1-based) es `rest[n-3]`.
- `when` no abre scope en Odin: dos ramas de `when` que declaran las mismas variables (`env, cwd, eerr :=` / `env, _, eerr :=`) evitan el "declared but not used" de `cwd` con -vet.
- En Linux `get_processes_with_profile(..., prefer_leader=true)` devuelve el **líder** de grupo (un ancestro del runner de tests, p. ej. `timeout`) en lugar del proceso: igual que Go; el test lo contempla.
- Con un mismo mirror de WSL, `tar --keep-newer-files` a veces no actualizaba un archivo recién editado: agregué `cp -ru` del directorio `odin/`. Las corridas de nameserver en Windows y en WSL a la vez pueden pisarse los puertos de loopback (WSL2 reenvía localhost): un "dial tcp: Refused" aislado en Windows desapareció al correr solo.
- Heredocs de Git Bash con python: `'\\'` se convierte en `'\'` → usar `chr(92)` para construir backslashes.
- El vendor sqlite3 enlaza `sqlite3.lib` por ruta relativa (`../../../../../vcpkg/...`): en un árbol copiado fuera de DEV hay que poner la .lib en la ruta relativa equivalente (con `-define:SQLITE3_LIB=C:/...` desde Git Bash llegó `/Users/...`, "Invalid import path"; copiar la .lib fue lo más simple).
