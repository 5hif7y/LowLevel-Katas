# Bugs encontrados durante el port de Portmaster a Odin

Registro local. **Nada de esto fue reportado** a Safing ni a Odin: queda
para que decidas qué reportar y cuándo. El detalle de cada uno está en el
informe del agente que lo encontró (`notas-agentes/<archivo>.md`, indicado
en cada sección) y en el código del fork (`port/odin`), donde cada
corrección lleva un comentario.

Tres grupos:

1. [Bugs del compilador / core de Odin](#1-odin-dev-2026-10) — con repro mínima.
2. [Bugs de Portmaster (Go)](#2-portmaster-go) — los graves primero, después el resto por área.
3. [Bugs del propio port](#3-bugs-del-port-encontrados-y-corregidos) — los que encontraron los tests.

---

## 1. Odin dev-2026-10

> **2026-11-24, el port pasó a Odin dev-2026-11 (`9a69916`).** 1.1 (ChaCha20
> AVX2): el arreglo upstream ya viene incluido, pero el workaround se mantiene
> hasta que termine la implementación SIMD de otra sesión. 1.2 (P-384
> `fe_sqrt`): el archivo es idéntico, el bug sigue y el workaround también.

Odin en `C:\Software\Odin-oct-2026`. Los dos tienen workaround en el fork y
se reproducen sin una línea de Portmaster.

### 1.1 ChaCha20 AVX2: ABI distinto entre llamador y llamado

* **Dónde**: `core/crypto/_chacha20/simd256/chacha20_simd256.odin`,
  `_dq_round_simd256` (`#force_inline proc "contextless"` que devuelve 4
  `simd.u32x8`).
* **Qué pasa**: cuando el proc **no** se inlinea (`-debug`, `-o:none`), el
  llamador (`stream_blocks`) pasa 3 punteros de resultado por la pila
  (`[rsp+20h..30h]`) y el llamado lee 4 (`[rbp+180h..198h]`). El cuarto es
  lo que haya quedado en la pila: casi siempre un valor viejo válido, a
  veces 0 → `movaps xmm0, [rax]` con `rax = 0`. Si el valor viejo no es 0
  pero es otro, el keystream sale mal en silencio.
* **Síntoma en Portmaster**: segfault intermitente en `jess.session_close`
  (tests de `spn/captain`), y un test colgado 6 h 37 min (el runner mató el
  hilo del test que crasheó con el mutex de `servicetest` tomado).
* **Repro**: `odin/tests/chacha20_simd/chacha20_simd_test.odin` en el fork.
  Compara cada implementación contra `.Portable` a 64 profundidades de pila.

  | Build | Simd128 | Simd256 |
  |---|---|---|
  | `-debug` | OK | segfault |
  | `-o:none` | OK | segfault |
  | `-o:minimal`, `-o:speed` | OK | OK |

  `odin test odin/tests/chacha20_simd -debug -define:CHECK_SIMD256=true`
  crashea mientras el bug exista.
* **Workaround**: jess usa `.Simd128` (`odin/jess/tools/gostdlib/ciphers.odin`,
  `CHACHA20_IMPL`). Volver al default cuando el test pase con Simd256 en
  `-debug` y `-o:none`.
* **Probablemente general**: cualquier proc `#force_inline` con varios
  resultados SIMD de 256 bits; `_dq_round_simd128` (misma forma, 128 bits)
  se llama bien.
* **Ya reportado (2026-11-24): no hace falta informe.** Es
  [odin-lang/Odin#7780](https://github.com/odin-lang/Odin/issues/7780)
  (abierto desde 2026-10-06, de otra persona, con un comentario que lo
  reproduce en Windows). El commit `5588efc8d` (2026-10-13, posterior a
  nuestro nightly `84bc3fc`) agrega `@enable_target_feature` a
  `core:crypto/_chacha20`. Al actualizar Odin: correr el test de arriba y,
  si pasa, sacar el workaround.

### 1.2 `fe_sqrt` de P-384 calcula otra potencia

* **Dónde**: `core/crypto/_fiat/field_p384r1/field.odin`, `fe_sqrt`.
* **Qué pasa**: la cadena de adición dice derivar x^((p+1)/4) pero calcula
  x^(p−3). Rechaza toda entrada: `fe_sqrt(4)` devuelve `ok = 0`. Por eso
  core no puede descomprimir ningún punto P-384.
* **Repro** (verificado a mano el 2026-11-23):

  ```odin
  import p384 "core:crypto/_fiat/field_p384r1"
  b: [48]byte; b[47] = 4
  x, r, sq: p384.Montgomery_Domain_Field_Element
  _ = p384.fe_from_bytes(&x, b[:])
  ok := p384.fe_sqrt(&r, &x)      // 0  (P-256 con el mismo código: 1)
  p384.fe_square(&sq, &r)          // sq != x
  ```
* **Workaround**: exponenciación propia en
  `odin/spn/access/token/pblind/hash.odin` (`field_sqrt`), sólo para P-384 y
  sólo sobre datos públicos.
* **Causa exacta (2026-11-24)**: reproduciendo las 29 operaciones de la
  cadena sobre los exponentes, da exactamente x^(p−3). El paso 395 ya
  vale x^((p+1)/4 − 1): cambiar el `fe_pow2k(out1, out1, 2)` final por
  `fe_mul(out1, &xx, out1)` lo arregla. Probado sobre una copia del paquete:
  de los 153 residuos cuadráticos en 1..200 hoy acepta 1, y con el arreglo
  los acepta todos, cada uno con su raíz correcta. El archivo es idéntico en
  `master` (`090cb3f0b`) y no hay ningún issue ni PR al respecto. Borrador
  del issue, sin enviar: `informes-locales/odin-p384-fe-sqrt.md`.

---

## 2. Portmaster (Go)

El port corrige los bugs que no cambian bytes en disco o en la red; los que
sí, se **conservan** y se documentan (cambiarlos rompería la compatibilidad
con el Portmaster de Go o con la red SPN).

### 2.1 Los graves

| Área | Bug | Efecto | Informe |
|---|---|---|---|
| netquery (API) | alias `as` validado con `[a-zA-Z]+` **sin anclar** → se concatena al SQL | **inyección SQL** desde cualquier proceso local con un token de la UI | `netquery.md` |
| netquery (API) | `databases` concatenado sin validar | **inyección SQL** (idem) | `netquery.md` |
| spn/access | `VerifyToken` rechaza tokens fallback con `IsFallback() && !healthCheck()` | al revés de su propio mensaje y del cliente: rechaza fallback justo cuando el emisor está **caído** y los acepta con el emisor sano | `spn-c1-access.md` |
| firewall | `mayBlockCNAMEs` resetea las listas de la entidad y nunca las recarga | **un CNAME en una filter list nunca se bloquea** | `firewall.md` |
| interception/dnsmonitor | split de `"type:  5 name"` por espacio simple → tipo vacío; claves por nombre crudo | **descarta todos los CNAME** del monitor DNS de Windows | `interception-kext-dnsmonitor.md` |
| interception (kext v1) | `ipAddressToArray` lee las palabras IPv6 de `&ip[i]` en vez de `&ip[i*4]` | **cada update de veredicto IPv6 nombra la dirección equivocada** | `interception-kext-dnsmonitor.md` |
| process | `GetProcessByRequestOrigin` arma el paquete sólo con Src/SrcPort y la búsqueda TCP compara el extremo remoto | **nunca encuentra el proceso**: `core/my-profile` y la autorización de apps fallan siempre | `process-tags.md` |
| nameserver | `writeDNSResponse` con la condición invertida | devuelve error justo cuando el reintento sin la sección extra **funcionó** | `nameserver.md` |
| database | `FlushCache` con la condición invertida | nunca hace nada | `database-core-runtime.md` |
| intel | la interfaz cachea 256 records y los batch writes la saltean | sigue sirviendo entradas **borradas / whitelisteadas / purgadas** tras un update de filter lists | `intel.md` |
| base/rng | el disparador de reseed de 1 MB nunca dispara (contador sin incrementar); `SupplyEntropyIfNeeded` invertido; sesgo de módulo; división por cero en `number(0)` | el RNG no se re-siembra como dice | `log-utils-rng-info.md` |
| network | el gauge `network/connections/active/total` cuenta las conexiones **terminadas** | métrica al revés | `network-nsutil.md` |
| cmds/observation-hub | `NewApprise` devuelve el módulo observer: main agrega el observer **dos veces** y apprise nunca | **el observation-hub de Go nunca manda una notificación** (dos observers corriendo) | `etapa6-c1-observation-hub.md` |
| service (instance) | `GetStates` / `AddStatesCallback` no bajan al grupo de interception | los estados del self-check de compat y de interception **nunca llegan** a `runtime:system/status` | `etapa6-a-instance-core.md` |
| service (Windows) | si `Start` falla, `RunService` espera un shutdown que nunca llega | el servicio **queda colgado** en vez de salir con error | `etapa6-a-instance-core.md` |
| cmds/updatemgr | `mirror` compara el archivo empaquetado contra el SHA-256 del contenido desempaquetado | nunca reconoce un `.gz`/`.zip` ya bajado; `--check` siempre falla para ellos | `etapa6-b-cmds-sueltos.md` |

### 2.2 El resto, por área (corregidos en el port)

* **api / http** (`http-websocket-api.md`): `ready` invertido; `RecordFunc`
  chequea el request en vez del record (panic con nil); `handleInsert` con
  accessor nil en records no-JSON; `checkAPIKey` hace `key[:4]` (panic con
  claves < 4 bytes); `qsub` filtra la suscripción si la query inicial falla.
* **core / ui / status / broadcasts** (`core-ui-status-control-broadcasts.md`):
  `getUpdateResource` devuelve JSON con `Content-Type: application/yaml`;
  `DataError{Error: err}` serializa `{}` y pierde el mensaje;
  `broadcastNotify` borra estados en memoria pero nunca los guarda; entrada
  de broadcast null → panic; `authorizeApp` espera la desconexión aun con el
  prompt vencido; `ui.setArchive` pisa un archivo abierto por otra request.
* **database** (`database-core-runtime.md`): `getController` puede arrancar el
  mismo storage dos veces (carrera); `Interface.Delete` no bloquea el record
  y la caché sigue sirviendo el borrado; `Migrate` traga errores.
* **metrics** (`httpclient-zip-metrics.md`): `WritePrometheus` descarta todas
  las líneas runtime cuando sólo hay namespace (Portmaster pone "portmaster").
* **log / utils / info** (`log-utils-rng-info.md`): el stable pool saltea el
  slot index−1; `START_PENDING` mapeado a StopPending; caché de nombres de
  servicio sombreada; `RunTerminalCmd` con `bytes.IndexAny` matchea la "t"
  de "Active code page".
* **mgr** (`mgr.md`): `SleepyTicker.SetSleep(false)` cierra un canal nil si
  `sleepDuration > 0` (no copiado).
* **updates / sync** (`updates-configure-sync.md`): `ImportProfile` usa
  `r.Export.Source` (opcional) → con source vacío rechaza todo mergedFrom;
  `ExportProfile` limpia `PresentationPath` en el perfil cargado en vez del
  exportado; `GenerateIndexFromDir` arma URLs con backslashes en Windows.
* **intel** (`intel.md`): deadlock posible en `updateListIndex` (lock no
  reentrante re-tomado); `GetCountryInfo` con location nil; recursión sin
  límite en `getCategorySources` (ciclo → stack overflow);
  `ring.UnmarshalBinary` acepta blobs truncados.
* **netenv** (`netenv.md`): `DeviceLocations.Copy()` ignora el receptor y
  copia el global.
* **profile** (`profile-endpoints.md`): `parseEndpoint` corta el comentario
  antes de recontar campos (panic con `"+ #x"`); `addActiveProfile` marca
  outdated al mismo perfil que re-agrega (cada proceso nuevo fuerza recarga
  y sube la revisión); `addEndpointEntry` sobre Config nil → panic.
* **process** (`process-tags.md`): `GetExecHash` escribe en un map nil;
  winstore: `os.ExpandEnv` no expande `%VAR%`; cmdline vacía → panic;
  `IsPortmasterUi` con module nil.
* **interception / kext** (`interception-kext-dnsmonitor.md`,
  `kext-binmeta-dga-integration.md`): `[4]byte(conn.LocalIP)` da 0.0.0.0 con
  una IPv4 de 16 bytes; el handler v2 se detiene para siempre con
  `ErrUnknownInfoType`; envíos que bloquean para siempre con el canal lleno;
  ETW espera 15 s por un trace nunca iniciado; `KextFile.Read` en loop
  infinito con 0 bytes; I/O overlapped sin evento (IO_PENDING como error);
  `RecvInfo` lee de más y aloca lo que diga el u32; busy-loop en
  `waitForServiceStatus`; `LoadAndSaveIcon` pasa ".png" con punto (falla
  siempre); la DLL de integración se recarga en cada update (leak).
* **firewall** (`firewall.md`): `resetConnectionVerdict` actualiza dos veces
  y manda un update falso para requests DNS; `checkTunneling` /
  `checkSplitTunneling` sin el read lock que exigen; `RunInspectors` lee el
  registro sin lock; `Resolver().IsDisabled()` sin resolver → nil deref.
* **compat / interop / ivpn** (`compat-interop-ivpn.md`): IVPN crea una
  suscripción nueva en cada vuelta del loop (leak + cambios perdidos);
  callback de cambio de red agregado en cada Start; lecturas sin lock.
* **network** (`network-nsutil.md`, `dns-network-leaves.md`): sin default
  firewall handler → llamada a func nil; el lookup UDP dual-stack mira el
  `lastUpdateAt` de la tabla IPv4; iphelper puede leer más allá del buffer.
* **resolver / transportes** (`resolver-core.md`, `tls-transports.md`):
  `AddToDebugInfo` hace panic sin resolvers globales; data race en
  `resolverInitDomains`; el handler TCP elige al azar entre `ctx.Done` y la
  respuesta (se puede perder una respuesta); nil sin chequear en
  `envResolverConn`.
* **nameserver** (`nameserver.md`): reiniciar el módulo falla (listeners,
  matcher y métricas registrados dos veces); `FilterResolvedDNS` sin
  proceso/perfil → panic, sin respuesta (ahora SERVFAIL).
* **lib/dns = miekg** (`pre4-frente-C-dns-bbolt-serpent.md`,
  `dns-presentation.md`): un error de valor SVCB pierde su texto.
* **SPN** — hub (`spn-a1-hub.md`), tokens (`spn-a2-tokens.md`),
  cabin/terminal (`spn-b1-cabin-terminal.md`), ships (`spn-b2-ships.md`),
  access (`spn-c1-access.md`), docks (`spn-c2-docks.md`), navigator
  (`spn-d1-navigator.md`), crew (`spn-d2-crew.md`), captain
  (`spn-e-captain-hub.md`), hooks (`spn-f-service-hooks.md`). Lo más notable:
  * carreras: Scramble `ProcessIssuedTokens` reemplaza Storage con sólo
    RLock; `Apply*` escribe fuera del lock; `AddLane`/`RemoveLane` mutan un
    slice compartido; `DeriveTunnelOptions` lee el perfil sin lock;
    `ConnectedHub` reasignado sin lock; `gossipRelayMsg` envía bajo RLock;
  * deadlocks: `PrePut` toma mapa y hub en orden inverso al resto;
    `SaveMeasuredHubs` guarda bajo RLock y el hook pide Lock;
  * lógica: `removeHTTPHandler` invertido (el test de Go pasa por
    casualidad); `unregisterCrane` borra la lane viva de otro crane; el relay
    terminal del expand op nunca se saca; auth "aborted" con la condición
    invertida (devuelve sin error ni pin); entrega directa que espera sobre
    el ctx padre; ship denegado por la entry policy que no se hunde;
    callbacks que se agregan en cada start; el worker "delete hubs" lanzado
    una vez por pin; la simulación de ruta crea un perfil con ID aleatorio y
    lo guarda en cada request `?profile=global`;
  * panics por nil: varios en hub, tokens, access (`user.Device`), docks,
    navigator, crew.

* **etapa 6 — binarios** (`etapa6-a-instance-core.md`, `etapa6-b-cmds-sueltos.md`,
  `etapa6-c1-observation-hub.md`, `etapa6-c2-hub-quicksettings.md`):
  `shutdown` chequea `ctx.Err()` y después cancela (dos shutdowns
  concurrentes paran los módulos dos veces); `MakeUpdateConfigs` en otros OS
  devuelve configs nil que `New` desreferencia; mensajes copy-paste
  (notifications "create runtime module", geoip "create customlist
  module"); observation-hub: el error de `service.New` se pisa (una cmdline
  op nunca corre, un error real deja la instancia nil), `makeHubName` con IDs
  de < 8 bytes, status viejo tras conectar; updatemgr: la conversión v1/v2
  usa un scan config sin inicializar (nunca ignora ni desempaqueta);
  integrationtest imprime los binds UDP4 como IPv6; winkext-test imprime el
  payload sin cargarlo (siempre vacío); `base/rng/test` (la herramienta para
  `dieharder`) no puede funcionar tal como está: `main` vuelve apenas arranca
  los workers (sale sin escribir nada) y nunca arranca el módulo rng
  (`rng.Bytes` fallaría con "RNG is not ready yet") (`rng-test-tool.md`).

### 2.2.1 Ronda unix (Linux): corregidos en el port

* **interception Linux** (`unix-interception.md`): `StartNfqueueInterception`
  marca `isRunning` antes de activar y no lo resetea si falla (el siguiente
  start dice "already running" sin nada corriendo); el monitor de
  systemd-resolved guarda los CNAME con el nombre crudo y `AddCNAMEs` busca
  con el FQDN (**nunca registra un CNAME**), toma como CNAME cualquier
  respuesta con campo `name` (NS, PTR) y desreferencia nil si falta `rr`;
  go-conntrack `Get` pide ACK y lee un solo datagrama (el ACK queda para la
  request siguiente); `InitNFCT` dos veces pierde el handle viejo.
* **netenv** (`unix-dbus-netenv.md`): la conexión D-Bus a NetworkManager
  nunca se renueva (si el bus se reinicia, todas las consultas fallan hasta
  reiniciar Portmaster); el warning de `ipv6_route` imprime otro campo.
* **binmeta / network/proc** (`unix-process-state.md`): en Linux
  `GetIconAndName` busca el ícono con la ruta completa (**nunca encuentra uno
  para un proceso real**) y el mejor match por prefijo pierde el directorio;
  UID e inodo de `/proc/net` parseados como int32 con signo (se pierden los
  sockets con inodo ≥ 2^31).
* **keyring** (go-keyring, `unix-keyring.md`): `handlePrompt` abandona ante
  la primera señal que no es `Completed`.

### 2.3 Conservados a propósito

Cambiarlos rompe compatibilidad (bytes en disco o en la red) o la
optimización de toda la red SPN. Están en la sección "Conservados" de cada
informe; los más llamativos:

* sqlite: `auto_vacuum=INCREMENTAL` se setea después de crear la primera
  tabla → SQLite lo ignora y `incremental_vacuum` no hace nada.
* ships: `for sub := range subtract` resta **índices** → load size
  1440/1420, del que el crane deriva tamaño y padding.
* cabin/terminal: padding con módulo truncado (sólo rellena mensajes más
  cortos que Padding); DFQ escribe con Pack64 y lee con GetNextN16.
* docks: el crane nunca rellena (las opts del controller tienen Padding 0);
  buffer de longitud de 2 bytes en el unloader.
* navigator: `markSuggestedReachable` nunca setea distancias → la
  restricción regional de hops siempre se "cumple".
* tokens: `shouldRequest` con `BatchSize/len > 10` (dispara bajo ~9 %, no
  "≤10 %" como dice el comentario).
* captain: el bootstrap file escribe IPv6 entre corchetes que
  `ParseBootstrapHub` no parsea.
* eBPF `bandwidth.c` (objeto precompilado que el port carga tal cual): en
  las sondas UDP IPv6 usa la dirección de origen como destino, así que esas
  actualizaciones nunca coinciden con una conexión (arreglarlo exige
  recompilar el objeto).
* ivpn `hook_linux.go`: cuenta como default las rutas con gateway 0.0.0.0
  (wg-quick) y falla si hay más de una.

---

## 3. Bugs del port encontrados y corregidos

Encontrados por los barridos de tests (3 corridas de todo el árbol al
cerrar cada etapa). Todos tienen test de regresión.

| Etapa | Bug | Cómo apareció |
|---|---|---|
| 2 | `status` guardaba como clave de map un puntero a un `State_Update` ya liberado | worst state basura, segfault ocasional |
| 2 | `sync.Once` re-entrante en `ensure_routers` | deadlock |
| 2 | jess `suites()` devolvía una vista del array global, liberado bajo el iterador | segfault intermitente |
| pre-4 | `destroy_action` llamaba `destroy_value` con un `any` no-puntero | no liberaba nada (leak de strings del payload) |
| pre-4 | chequeo DNS de netenv con `SO_RCVTIMEO` | en Windows, un recv vencido deja el socket en estado indeterminado (ahora toda espera es `WSAPoll`) |
| 4 | dos tests generaban claves CNG con el mismo nombre (timestamps en ns + `NCRYPT_OVERWRITE_KEY_FLAG`) | handshake TLS con `SEC_E_INTERNAL_ERROR` una de cada tantas |
| 4 | `tls.DialContext(ctx)` guardaba un ctx que sólo valía para el dial | lector usando un contexto liberado |
| 5 | **iterador de la base de datos liberado por el consumidor mientras el hilo de la query de sqlite seguía en `finish`** (en Go lo sostiene el GC) | el hub SPN moría ~1 de cada 40 arranques con `0xC0000005`, sin log, y nunca bajo un debugger; se encontró con minidumps escritos por el propio proceso (`lib/crashdump`). Ahora refcount consumidor + storage |
| 5 | navigator: un test esperaba `.No_Location`, que sólo ocurre si otro test ya cargó geoip | fallaba ~2 de cada 10 corridas |
| 5 | `base/utils`: el test del limiter afirmaba algo que Go no afirma (tamaño del lote) | dependía de la velocidad de arranque de 100 hilos del SO |
| 6 | `rng.stop` y el stop del firewall esperaban a su propio worker de stop (el grupo corre `stop` como worker del manager) | rng fallaba al minuto ("Not_Ready", grupo Invalid) y el firewall perdía 5 s al apagar; nuevo `mgr.wait_for_other_workers` |
| 6 | `make_update_configs`: la lista `ignore` era un slice en la pila | basura en el updater |
| 6 | el servidor TLS de test deja su clave CNG persistida si el proceso muere antes de limpiar | claves `pm-tls-test-*` acumuladas en el almacén de claves del usuario; ahora cada corrida barre las de procesos muertos |
| 6 | tests de `base/api` y `netquery` sin limpieza de temporales | ~175 directorios en `%TEMP%`; ahora barren los de corridas anteriores |
| unix | `dial_tcp` cerraba con `defer` un resultado con nombre que `return {}, err` ya había puesto en 0 → todo dial fallido cerraba el **fd/handle 0** y perdía su socket | en Linux el fd 0 puede ser el listener de otro test: el resolver fallaba 1 de cada 11 corridas con ECONNREFUSED (15/40 bajo carga) |
| unix | `tls.accept` cerraba el socket al fallar y sus tres llamadores también | doble close |
| unix | un test de TLS leía el ALPN de un Conn ya destruido | use-after-free |
| unix | `os.get_absolute_path` es `realpath(3)` en POSIX: falla si la ruta todavía no existe (Go `filepath.Abs` no toca el disco) | fstree, íconos de binmeta y el índice de updates fallaban en Linux; ahora `utils.abs_path` |
| unix | el `.gitignore` raíz (de Go) excluye todo `vendor/` y `testdata/`, también los del port | un clon nuevo de `port/odin` no compilaba ni testeaba; ahora `odin/.gitignore` los re-incluye |
| unix | el coordinador commiteó con `git add -A` mientras un agente editaba | el commit `79a21776` incluye la mitad del keyring unix: en ese commit `jess/truststores` no compila en unix (completado en `1cb7426d`) |

---

## Pendiente de decisión (tuya)

* Reportar a Safing las **dos inyecciones SQL** de netquery (y quizás
  `VerifyToken`, los CNAME y la IPv6 del kext v1). Borrador listo y sin
  enviar (2026-11-24), sólo local: `informes-locales/safing-netquery-sqli.md`
  (en `.git/info/exclude`). Gravedad estimada baja: requiere estar
  autenticado en la API y la conexión es de sólo lectura.
* Reportar a Odin el bug 1.2 (P-384 `fe_sqrt`): borrador listo y sin
  enviar, en `informes-locales/odin-p384-fe-sqrt.md`. El 1.1 ya estaba
  reportado (#7780).
* Los otros cuatro de Safing (CNAME en filter lists, `VerifyToken`, IPv6
  del kext v1 y CNAME del dnsmonitor), verificados contra `development`:
  borrador local en `informes-locales/safing-other-findings.md`, sin
  enviar. Va junto con el de la inyección SQL.
