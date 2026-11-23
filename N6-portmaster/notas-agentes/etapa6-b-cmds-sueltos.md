# Etapa 6, ronda B — cmds sueltos (updatemgr, trafficgen, winkext-test, integrationtest, testsuite) + KeyringTrustStore

Nada commiteado. Todo en `port/odin` del fork. Ningún test toca red que no sea loopback, ni el driver, ni admin, ni DNS/firewall del sistema.

## Archivos

- `odin/lib/cliflags/` (nuevo): subconjunto de cobra (árbol de comandos, `Use`/Short/RunE, ExactArgs, flags locales y persistentes, `--x v`/`--x=v`/`-s v`/`-sV`, MarkFlagRequired, -h) y del paquete `flag` de Go (`go_style`). **Provisorio**: los 5 binarios lo usan en vez de cobra; se puede mover a `cmds/cmdbase` (o al `lib/cobra` que apareció en paralelo) cuando esté.
- `odin/jess/truststores/keyring.odin`, `keyring_windows.odin`, `keyring_other.odin` + tests. Se sacó el `PORT-TODO(stage-6)` de `dir.odin`.
- `odin/cmds/updatemgr/{main,convert,scan,sign,mirror,download}.odin` + `updatemgr_test.odin`.
- `odin/cmds/trafficgen/main.odin` + test.
- `odin/cmds/winkext-test/{handle,main_windows,main_other}.odin` + test.
- `odin/cmds/integrationtest/{main,netstate}.odin` + test.
- `odin/cmds/testsuite/{main,db,login,report_healthcheck}.odin` + test.
- `odin/service/updates`: sólo se hicieron públicos `index_unmarshal` e `index_scan_config_init` (los usa updatemgr). Nada más tocado fuera de mi alcance.

## Por programa

**KeyringTrustStore** (jess `truststores/keyring.go` + la API de zalando/go-keyring). Signets/envelopes en base58 bajo servicio `jess:<nombre>`, usuario `<id>.signet|.recipient|<n>.envelope`; self-check `_selfcheck` como Go (queda escrito, igual que en Go). Backend inyectable (`Keyring_Backend`: set/get/delete): `system_keyring_backend()` = Credential Manager por `advapi32` (`CredWriteW/CredReadW/CredDeleteW/CredFree`, CRED_TYPE_GENERIC, CRED_PERSIST_LOCAL_MACHINE, target `servicio:usuario`, límites de go-keyring: secreto ≤ 2560 B, servicio < 512) — por usuario, sin admin. `Mem_Keyring_Backend` = MockInit de go-keyring. Stub `#+build !windows` → `unsupported platform`, `PORT-TODO(unix)` al Secret Service por D-Bus (keyring_unix.go) y `security` de macOS. Select/AllEnvelopes → `.Not_Supported_By_Trust_Store` como Go. Errores `could not find signet: secret not found in keyring` (wrap `%w: %w` de Go).
Test real contra Credential Manager: servicio único `jess:pm-odin-unittest-<random>`, borra todo antes de salir y verifica que no quede nada (`cmdkey /list` limpio después de las corridas).

**updatemgr** — `scan` (dir → índice JSON), `sign` (firma un índice con jess usando `--tsdir`/`JESS_TS_DIR`/`JESS_TSDIR` o `--tskeyring`/`JESS_TS_KEYRING`), `mirror` (baja un índice v1/v2/v3 + artefactos a un dir con la misma estructura, `--check`, `--sigs`), `download` (Updater de service/updates con `ForceUpdate`). Portado entero, incluso `getSignedByMany/Single` (código muerto en Go). User-Agent `Portmaster Update Mgr (windows amd64)`.
Corrido: sólo por los tests (servidor HTTP local en 127.0.0.1:0, keyring en memoria). El binario no se ejecutó contra updates.safing.io.

**trafficgen** — HEAD a `-url` y consultas A a `-lookup` contra `1.1.1.1:53` (constante como en Go), `-n` veces cada `-w` ms. `dns.Exchange` → `dns_exchange` propio (UDP no bloqueante, 2 s, ignora IDs que no coinciden como miekg); HTTP con `redirect = .Never` y un cliente por request. Tests: servidor DNS UDP y HTTP en loopback (respuesta vieja con otro ID ignorada, NXDOMAIN, sin servidor → falla rápido, 302 no se sigue). El resolver público nunca se contacta; el binario no se corrió.

**winkext-test** — carga el kext v1 (`windowskext.init/start`, instala el servicio del driver) y acepta/reroutea paquetes. Compila (Windows) + stub `!windows`. Test de la lógica pura (`decide`, `handle_packet`) con un paquete falso. **NUNCA se ejecutó el binario** (necesita driver real y admin).

**integrationtest** — `netstate` imprime las tablas TCP/UDP del sistema (iphlpapi, sólo lectura) con PID y ejecutable (`process_exe` = QueryFullProcessImageNameW, en vez de gopsutil); `netstate monitor` lo repite cada 10 ms imprimiendo sólo lo nuevo. Sin admin, nada system-wide → **se corrió**: `netstate` (89 líneas, rc 0) y `monitor` 3 s con `timeout` (111 líneas, terminó, sin procesos colgados). Test: formatos, dedup y que un listener propio en 127.0.0.1 aparezca con el PID y el .exe del test.

**testsuite** — `login`: login al servidor de cuentas, chequea device ID, rotación del auth token, pide tokens; reporta a un healthchecks.io. No es self-contained (servidor real de Safing + credenciales) → el binario **no se corrió**; el test corre el mismo código (`cli.execute` con los flags) contra un servidor de cuentas falso en loopback (`access.set_account_server`) y un healthcheck local: login con el device ID del usuario inicial, token rota (tok-1 → tok-2), el setup de tokens falla (el falso no emite pblind) → `failed to update tokens: unexpected reply`, reportado a `/fail` con ese texto; contraseña incorrecta; reporte de éxito. Bases `core`/`cache` en hashmap como Go.

## Bugs de Go

Corregidos:
- updatemgr `convertEntries` usa el `scanConfig` global sin `init()` (los globs sólo se compilan dentro de `GenerateIndexFromDir`, sobre una copia) → en Go la conversión v1/v2 nunca ignora nada ni desempaqueta. El port compila el config antes (`prepare_scan_config`, una vez). Ojo: el unpack se evalúa contra el *filename* (como Go) y `**/*.gz` pide una `/`, así que igual no matchea; se conserva.
- updatemgr `mirror`: el SHA256 de un artefacto con Unpack es del contenido desempaquetado pero Go compara el archivo empaquetado → nunca reconoce un .gz/.zip ya bajado y `--check` siempre falla para ellos. El port desempaqueta antes de chequear.
- integrationtest: los binds UDP4 se imprimían como `IPv6`.
- winkext-test: `-get-payload` imprimía `Payload()` sin `LoadPacketData` (siempre vacío); el port lo carga.

Conservados: winkext-test `-max-packets N` corta después de N+1 paquetes (`>`); testsuite loguea `err` en vez de `cleanUpErr` al fallar la limpieza; updatemgr `download` sólo acepta índices HTTPS (Config.Check); los globs con `os.PathSeparator` en Windows (patrones con `/` no matchean rutas relativas con `\`, ya anotado en updates).
Odin: artefactos convertidos ordenados por identificador (Go: orden de map).

## Trampas

- `fmt` de Odin interpreta `{` `}` (formato posicional): JSON en `tprintf` hay que escaparlo `{{ }}` o concatenar. Me rompió el JSON del servidor falso ("failed to parse response").
- `dns.msg_append` toma posesión del RR: con un nombre prestado de otro mensaje → doble free (crash sin mensaje, rc 127). Usar `msg_append_copy`.
- `free_all(context.temp_allocator)` dentro de un loop que recibe strings temp del llamador los invalida → `DEFAULT_TEMP_ALLOCATOR_TEMP_GUARD()` por vuelta.
- Las variables de flags son globales (como en cobra): dos tests que arman el árbol en paralelo se pisan → un solo test por paquete que use `build_commands`, o procs con parámetros.
- `CREDENTIALW.TargetName` es `LPWSTR` y `utf8_to_wstring` devuelve `cstring16`: cast por `rawptr`.
- heredocs de python manglean `\n` (confirmado otra vez).

## Tests (foreground, 3 corridas)

`export PATH=$PATH:/c/Users/Shifty/DEV/vcpkg/installed/x64-windows/bin; timeout … odin test odin/<pkg> -collection:pm=odin -vet`

| Paquete | Tests | 1 | 2 | 3 |
|---|---:|---|---|---|
| cmds/updatemgr | 8 | ok | ok | ok |
| cmds/trafficgen | 4 | ok | ok | ok |
| cmds/winkext-test | 2 | ok | ok | ok |
| cmds/integrationtest | 1 | ok | ok | ok |
| cmds/testsuite | 1 | ok | ok | ok (tras arreglar la carrera de flags) |
| lib/cliflags | 2 | ok | ok | ok |
| jess/truststores | 4 | ok | ok | ok |
| jess | 14 | ok | ok | ok |
| service/updates | 15 | ok | ok | ok |
| lib/dns | 173 | ok | ok | ok |

Sin leaks reportados. `VET=1 bash odin/tests/crosscheck.sh windows_amd64 linux_amd64 darwin_arm64 -- odin/lib/cliflags odin/jess/truststores odin/jess odin/service/updates odin/service/updates/coreupdater odin/cmds/{updatemgr,trafficgen,winkext-test,integrationtest,testsuite}` → 30/30 ok.

Cobertura de updatemgr: salida de `scan` byte a byte igual a `json.MarshalIndent` de Go (Published fijo; escape HTML `<&>` incluido); sign (CLI con `--tsdir`) → verificación con la clave pública, índice alterado rechazado, índice sin versión rechazado antes de firmar; sign con keyring (backend en memoria); conversión v1/v2 con errores de Go; mirror e2e (v1→v3 con checksums, firmas, segunda corrida sin descargas, `--check`, archivo corrupto re-bajado, 404, extensión inválida); download e2e (filtro de plataforma; URL http rechazada por Config.Check, el test la cambia después con `updates.configure`).

## Pendiente / para el coordinador

- STAGES: cmds/updatemgr, trafficgen, winkext-test, integrationtest, testsuite y jess KeyringTrustStore → ✔. `lib/cliflags` puede fusionarse con cmdbase / `lib/cobra`.
- `PORT-TODO(unix)`: keyring (Secret Service / `security`), winkext-test (Windows por naturaleza).
