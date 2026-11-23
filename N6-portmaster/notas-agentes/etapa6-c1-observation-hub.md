# Etapa 6, ronda C1 — `cmds/observation-hub` (+ text/template, r3labs/diff)

Fork `DEV/gitclones/portmaster`, rama `port/odin`, sin commitear. Todo nuevo (no toqué archivos existentes; en `lib/gotime` sólo agregué `format.odin` + test).

## Qué hace

Un Portmaster "observador": instancia de servicio completa (`cmdbase.service_factory` = `service.New`) con SPN en modo cliente (`conf.enable_client(true)`, `captain.disable_account = true`, `sluice.enable_listener = false`, `api.enable_server = false`) más dos módulos:

- **observer** (`observe.odin`): espera `runtime:spn/status` = connected, toma los pins de `map:main/` como base, se suscribe a cambios y cada 10 s reporta los hubs cuyo cambio tiene más del `-reporting-delay` (default 10m; tope 3× desde el primer cambio). Hub nuevo → "New Hub: name (ABCD-EFGH)"; cambiado → "Hub Changed: …" + changelog (diff de los dos `PinExport`, filtrado por `ignoreChangesIn`/`ignoreStates` salvo `-report-all-changes`; States como "State is X" / "State is NOT X").
- **apprise** (`apprise.odin`): con `-apprise-url` + `-apprise-tag` (obligatorio) renderiza `notifications.tmpl` por tag (`discord-*` → discord-notification; `matrix-*` y resto → matrix-notification) y lo manda con `base/apprise` (markdown, info), 3 intentos por tag; `-apprise-greet` manda el saludo.
- `main.odin`: mismos flags que Go (persistentes `--bin-dir/--data-dir`; `--log-stdout --log-dir --log --print-stack-on-exit --reboot-on-restart`; flag set de Go de cmdbase + los 7 del observer/apprise; comandos `version`, `update`). Agregado Odin: `--unprivileged` (como portmaster-core).

El observer no usa reflect: `pinvalue.odin` arma un árbol `govalue` con los tipos Go de `navigator.PinExport` (Entity, Announcement, Status, Lane, Key, Coordinates) desde el JSON Go del record (`Pin_Export.json`, ya byte-igual a `json.Marshal`). Sobre ese árbol corren el diff y el template.

## Paquetes nuevos

| Paquete | Qué |
|---|---|
| `lib/govalue` | valores Go dinámicos (subconjunto de reflect + fmt): kinds, nombres de tipo como `%T`, `String()` de net.IP / time.Duration / time.Time, métodos `UTC/Format/String/Unix/IsZero`, `%v` de fmt (structs, `&{}` arriba, punteros anidados como dirección, mapas ordenados), `isTrue` del template |
| `lib/gotime/format.odin` | `time.Time.Format` con layout Go (nextStdChunk/appendFormat completo: meses/días, `_2`, `002`, 12h, PM, zonas `MST`/`Z07:00`/`-0700`…, fracciones `.000`/`.999`/`,000`) |
| `lib/gotemplate` | text/template (lexer, parser, exec, builtins) |
| `lib/godiff` | subconjunto de r3labs/diff v3 |
| `cmds/observation-hub` | main, observe, apprise, pinvalue, `notifications.tmpl` (copia LF de upstream, `#load`) |

## Subconjunto de text/template

Lo que usa `notifications.tmpl` (define/template, if, with, `{{-`/`-}}`, cadenas de campos con métodos `.UpdateTime.UTC.Format "…"`, punteros, `len`, funciones con args) **más** lo general: comentarios, pipelines `|`, variables `$x :=`/`=` (dos en range), if/else if/else, with/else with/else, range (slice, map ordenado, int; else, break, continue), block, paréntesis y chains `(x).F`, literales (bool, nil, números dec/hex/oct/bin/float, `"…"` y `` `…` ``), builtins and/or (cortocircuito), not, len, index, print, printf (v s d q t x X o b c U f e g %, flags/ancho/precisión), println, eq/ne/lt/le/gt/ge. Errores con el texto de Go (`template: notifications:19:22: executing "matrix-notification" at <.UpdatedPin.Name>: nil pointer evaluating *navigator.PinExport.Name`). FuncMap con tipos de parámetro Go (validateType: "wrong type for value; expected []string; got string"). Fuera: call, slice, html, js, urlquery, char constants y complejos como valores, opción missingkey (default "<no value>").

## Subconjunto de r3labs/diff

`diff.Diff(a, b)` con opciones default: structs campo a campo (path = nombre), time por UnixNano, punteros (nil↔set = un UPDATE), slices con el algoritmo genérico sin orden (faltantes por índice → CREATE/DELETE/UPDATE), mapas por clave, `structValues`/`mapValues` (crear/borrar un struct entero = un change por campo no cero, swap). Fuera: opciones, tags, identifiers, Patch/Merge, interfaces. Diferencias: claves de mapa en orden (Go: aleatorio); campos no exportados no existen en el árbol → nunca se difean (Go sí difea los de structs alcanzados por campos exportados: `Info.parsedTransports/entryPolicy/exitPolicy`, que igual están en `ignoreChangesIn`, y cachés de Entity). net.IP en forma de 16 bytes (ParseIP/JSON): una IPv4 cambiada sale "Info.IPv4.15 changed from 70 to 71", por byte, igual que Go.

## Bugs de Go

Corregidos:
1. `main.go`: el `err` de `service.New` se pisa con el de `NewApprise` → una cmdline op (`--export-config-options`) nunca se ejecuta y un error real deja instancia nil (panic). El port lo conserva (probado: `--export-config-options` → exit 0).
2. `NewApprise` devuelve `observerModule` y main agrega el observer **dos veces** y apprise nunca → dos workers observer y **Go nunca manda notificaciones** (startApprise no corre). El port agrega observer + apprise.
3. `makeHubName` hace panic con IDs de < 8 bytes → el port usa el ID entero.
4. Al conectar, `latestStatus` queda con el status viejo hasta el próximo update → el port lo refresca.

Conservados: mensaje copy-paste "received invalid pin export" para el status; `reporting-delay` var 5m vs flag "10m"; sin aviso de hub borrado; duerme 5 s también tras el 3er intento fallido (el port: interrumpible por shutdown, variable `apprise_retry_delay`).

Diferencias: select → polling (50 ms); hubs en orden de ID; template con CRLF→LF (un checkout Windows con autocrlf lo embebería con `\r` en Go); `UpdateTime` en UTC (el template sólo usa `.UTC`). PORT-TODO(stage-6): `-apprise-client-cert/-key` (WinHTTP sin certificado cliente) → error claro al iniciar.

## Tests (3× en primer plano, `odin test odin/<pkg> -collection:pm=odin -vet`)

| Paquete | Tests | Qué cubre | r1 | r2 | r3 |
|---|---:|---|---|---|---|
| lib/govalue | 4 | Duration.String, net.IP.String (v4-mapped, ::, RFC 5952), `%v`, métodos de Time | ok | ok | ok |
| lib/gotime | 6 | + layouts Go (ANSIC, RFC3339 con/sin offset, MST sin nombre, fracciones, `002`, "Janet Mond") | ok | ok | ok |
| lib/gotemplate | 4 | ~150 casos de exec_test.go (campos, mapas, dots, variables, if/with/range/else/break/continue, print/printf, and/or, index, len, eq…, pipelines, parens, trim, define/template/block), 14 textos de error exactos (exec y parse), Templates/Lookup/associate, items del lexer | ok | ok | ok |
| lib/godiff | 5 | casos de diff_test.go (slices insert/delete/update/duplicados, mapas, structs, ptr nil↔set, time, mismatch) + formas del observer (ConnectedTo, Lanes, IP) | ok | ok | ok |
| cmds/observation-hub | 8 | makeHubName; format_pin_changes (nuevo, sólo ignorados, report-all, el summary del fixture Go, states DELETE/UPDATE/ignorado, exit, version, EntityV6); timing (delay, tope 3×, merge, borrado, hub nuevo); **golden** matrix+discord de `notifications.tmpl` con el fixture de apprise_test.go y hora fija (esperado derivado a mano de la semántica de text/template, no capturado de Go); apprise contra API falsa en loopback: **body JSON exacto** como lo arma encoding/json (orden, omitempty, escape HTML, UTF-8), saludo, 3 tags, 500 → 3 intentos × tag y texto errors.Join; **integración**: worker real sobre servicetest (sqlite+runtime), DB "map" inyectada sirviendo `pin_export` de un `nav.Map` real, provider de prueba de `runtime:spn/status` (connecting→failed→connected), cambios empujados a la suscripción → 2 POST (Hub Changed + New Hub), borrado sin aviso, worker para al cancelar | ok | ok | ok |
| base/apprise | 3 | (existente) | ok | ok | ok |
| service | 4 | (existente, instancia completa) | ok | ok | ok |

Sin leaks. Binario: `odin build odin/cmds/observation-hub` ok; `version`, `--help` (flags), flag inválido → 1, `--export-config-options` → 0. No corrí el servicio completo (SPN real de Safing / kext): la integración usa un mapa local; con hubs de `tests/spn_e2e` sería el paso siguiente (requiere zona/intel self-hosted en la config de la instancia).

Crosscheck: `VET=1 bash odin/tests/crosscheck.sh windows_amd64 linux_amd64 darwin_arm64 -- odin/lib/govalue odin/lib/gotime odin/lib/gotemplate odin/lib/godiff odin/cmds/observation-hub` → **0 fallas**.

## Trampas

1. **`fmt` de Odin interpreta `{{` / `}}` en el formato** (sale `{`): mensajes como "{{break}} outside {{range}}" y JSON armado con `tprintf` se rompen → pasar como argumento `%s` o concatenar.
2. `matrix` es palabra clave de Odin (no sirve como nombre de variable).
3. sed/heredoc convierten `\n` en salto real dentro de literales → constante `NL` / `chr(92)`.
4. `notifications.tmpl` del working tree está en CRLF (autocrlf=true) → copia tomada de `git show HEAD:` y normalización al cargar.
5. Listas `[dynamic]` de nodos sin allocator → leaks en el tracking allocator → `context.allocator` = arena del template durante el parse.
6. `for c in []T{...} {` no parsea (ambigüedad con el literal) → variable antes.
7. `navigator.sorted_pins` es privado → el test usa `get_pin` por ID.
