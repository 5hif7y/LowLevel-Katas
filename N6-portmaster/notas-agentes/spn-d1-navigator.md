# Etapa 5 (SPN), ronda D1 — spn/navigator — informe

Sin commit. Escrito sólo en `odin/spn/navigator` (`pm_spn_navigator`, ~9.500 líneas con tests). **Ningún arreglo fuera** (crew/hooks ya compila contra esta API; no toqué `odin/spn/crew`).

## Archivos (`odin/spn/navigator/…`)
- Port: map, pin, pin_export, state, options, intel, update, database, findnearest, findroutes, route, routing_profiles, costs, optimize, optimize_region, region, sort, measurements, metrics, map_stats, api, api_route, module.
- Nuevos: errors (Error_Kind + msg inline como hub), deps, **intelfile** (intel local), gofmt (Duration.String de Go + tabwriter mínimo), testdata/main-intel.yml (copia de Go).
- Tests: navigator_test, map_test (generador propio), selfhosted_test, api_test, testenv_test.

## Portado vs PORT-TODO
Todo el paquete Go portado (Map/Pin/estados, intel + overrides + advisories + regiones, update desde DB por hook PrePut + InitializeFromDatabase, bootstrap, findNearest/findRoutes/optimize incl. bootstrap/desegregate/wait/target-structure, mediciones, métricas, stats, PinExport + DB inyectada `map`, 8 endpoints API, módulo). Imports alias `hb` (spn/hub) y `pmintel` (service/intel): los campos `hub`/`intel` sombrean el paquete.
- **Self-hosted** (sin service/updates):
  - Opción `spn/navigator/intelFile` (+ `set_intel_file` para el flag del hub, etapa 6): al start, tras cargar la DB, `load_intel_file` aplica el intel (formato Go, YAML/JSON) al mapa principal **cualquiera sea su nombre** (Go sólo vía updater y sólo map "main"). Bootstrap hubs si el mapa está vacío, Trusted/VerifiedOwner/Override/regiones. Requiere reinicio. Doc de uso en la cabecera de intelfile.odin.
  - Nombre de mapa: `conf.set_main_map_name` (existente; flag `-spn-map` = etapa 6). El navigator usa `conf.main_map_name` al start.
  - Sin base geoip: el pin recibe una Location vacía (lo que Go devuelve para una IP ausente de la base) y los Override (CountryCode → centro del país, Coordinates, ASN, ASOrg) la ubican; `find_routes` igual; `find_nearest_hubs(nil, nil)` usa ubicaciones vacías en vez de "no location provided".
  - `map_is_empty`: la regla Go "con home, 1 pin = vacío" sólo en hubs (`conf.is_public_hub()`); un cliente de una red de 1 hub ruteaba con nada (ErrEmptyMap siempre).
  - Para operar: hub(s) `Trusted: true` en el intel (home con <2 hops lo exige y firewall sube a double-hop si el home no es trusted); **sin intel no hay ruteo** (Go: el disregard por defecto incluye UsageDiscouraged → exige intel aplicado); red de 1 hub = sólo perfil "home"; 2 hubs = single/double-hop.
- PORT-TODO: `FlagID` de los quick settings de país (config.Quick_Setting no tiene el campo); grafo DOT propio (no byte-idéntico a gographviz); ruta API con `?profile=` vía hook (abajo); DNS de la ruta API con `core:net.resolve`; Time.String de las tablas como RFC 3339; Pin_Export expone en Odin un subconjunto de campos (el JSON es el completo de Go).

## Hooks / API para otros
- `deps.derive_tunnel_options_for_profile` (firewall.DeriveTunnelOptions con el perfil; lo llena la instancia, etapa 6; nil → la API responde "profile simulation not available").
- Vars de paquete: `own_location_fn` (default netenv), `measure_hub_fn` (default docks.measure_hub).
- Llenado: **`hub.set_navigator_access`** (en prep; `get_hub` devuelve el hub del pin, nueva ref).
- Crew/captain (ownership): `Main` / `main_map()` (nueva ref); `get_home` → pin + **terminal con nueva ref**; `find_routes`/`find_route_to_hub` → `^Routes` propios (`destroy_routes`), `route_copy(_up_to)`; `set_active_terminal(pin, &Pin_Connection)` retiene terminal y copia ruta; `get_active_terminal` nueva ref; pins, hubs reemplazados, entidades, intel y conexiones viejas nunca se liberan mientras vive el mapa (listas retired); leer `pin.info`/`pin.status` (snapshots) con lock de mapa o de pin; `options_copy` superficial (políticas prestadas), `options_to_opaque/from_opaque` para `Connection.tunnel_opts`. Orden de locks: mapa → hub.

## Tests (`export PATH=…vcpkg…/bin`; `timeout 600 odin test odin/spn/navigator -collection:pm=odin -vet -out:$TEMP/d1/nav.exe > log 2>&1`, ×3 en primer plano)
**23/23 ×3 OK** (6,4–7,7 s), sin leaks; `odin check -vet -no-entry-point` limpio windows_amd64 y linux_amd64; crosscheck (+darwin_arm64) ok. Sin red: geoip con updater falso y MMDB armadas con mmdb.writer (30 redes v4 + 12 v6 públicas), `measure_hub_fn` falso, sin netenv.
- Go: TestStates, TestSorting, TestNearbyPinsCleaning, TestRandomMapCreation (200 hubs), TestFindNearest (200 búsquedas), TestFindRoutes (10 destinos, Go 1), TestOptimize (100 hubs, todos ≤5 hops tras optimizar). gofakeit → generador determinista propio (splitmix64, mismas probabilidades/estructura; IPs de las redes de test).
- Propios: **red self-hosted**: mapa principal `selfhosted1` + intel local (bootstrap de A, Trusted, VerifiedOwner, Override ASOrg); identidades cabin A/B, announcements+statuses **firmados** aplicados (`hub.apply_*` + `save` → hook) → lanes, Active/Trusted, home, rutas double/single/home/triple, route-to-hub, failing, measure→optimize (wait→target-structure), persistencia de mediciones, DB `map:` get/query; mapa de 1 hub (home OK, multi-hop no, regla de hub); sin geoip (overrides, sin ubicación de dispositivo); bootstrap a otro mapa desde el archivo, archivos rotos; intel de Go (testdata); costos/TTL/min-lanes; perfiles y compliance; matchers (defaults por tipo, intel aplicado, owners, políticas, advisories, exit policy); lanes/removal/flags/stats/export JSON golden/ruta activa; bootstrap (v4+v6, IP distinta, dominios); superseding; discontinued/trust/advisory; hook DB (put/delete/initialize); regiones + optimize de miembro y satélite; API completa (pins, optimization(+table), measurements(+table), graph .dot/.html/.png, route/to, intel/update, mapa inexistente).

## Bugs de Go
Corregidos: `updateStates` lanza el worker "delete hubs" dentro del loop (una vez por pin, mismo slice creciendo: borrados repetidos + carrera) → uno al final; `updateLocationData` hace `return` tras fallar IPv4 y deja IPv6 viejo; PrePut toma el mapa con el record del hub bloqueado (orden hub→mapa, inverso al resto) y `RemoveHub` re-bloquea ese mismo hub vía Export (deadlock) → el hook desbloquea el record mientras actualiza; `SaveMeasuredHubs` guarda bajo RLock y el hook pide Lock (deadlock) → recolecta y guarda fuera; `FindRouteToHub(nil opts)` y API measurements con nil → panic; `Matcher` hace append al HubPolicies del caller; handlers API/grafo leen pins tras soltar el lock; pins agregados después del intel quedaban sin región; Go nunca llama SetNavigatorAccess.
Conservados: `markSuggestedReachable` nunca setea distancias (empiezan en 0) → restricción de hops regional siempre "cumplida" (cambiarlo cambia la optimización de toda la red); Discontinued deja state=None+Offline; VerifiedOwner no se limpia al salir del intel; FindRoutes muta `opts.routing_profile` (home→single-hop); sin intel aplicado ningún pin es elegible con defaults.

## Trampas
1. Campo `hub`/`intel` en structs del paquete = nombre de paquete importado → "Illegal declaration cycle" → alias `hb`, `pmintel` (sed `(^|[^.])hub\.`).
2. GC: hub.apply_* reemplaza y libera Info/Status bajo el lock del hub; lectores con sólo el lock del mapa (matchers, superseding, nearest) → snapshots `pin.info/status` (ref propia, cambian sólo con lock de mapa+hub).
3. Retirar conexiones con su terminal mantiene vivo el crane → la terminal se suelta al retirar (sólo se lee bajo lock de pin); la ruta queda retirada.
4. `free_all(temp)` dentro de un loop sobre una lista temp (sorted_pins) → segfault: lista en heap.
5. Announcement es dueño de entry/exit_policy → destruirlas aparte = double free.
6. `ensure_hub` exige transports (prepare) → un hub sin transports no entra por el hook (silencioso, log debug).
7. `remove_stale_test_dirs` tarda 5–7 s con ~7.000 entradas en %TEMP% → archivos de test dentro de `servicetest.env.dir`.
8. core:testing de esta versión no tiene logf/error → core:log + expect.
9. Heredocs de bash con comillas/apóstrofos en el contenido cortan el comando → Write para archivos.
