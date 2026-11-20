# service/intel (+geoip, geoip/mmdb, filterlists, customlists, publicsuffix) — informe (adelantado de la etapa 4)

Tests (x3): intel 4, geoip 6, mmdb 6, publicsuffix 1, filterlists 5, customlists 3 — OK.

- mmdb: lector propio según la spec MaxMind DB (árbol 24/28/32 bits, nodo de inicio IPv4 en árboles IPv6, punteros de 4 tamaños, todos los tipos; decode a árbol genérico o por reflexión con tags `maxminddb:"..."`) + escritor para tests. testdata: 6 bases de prueba de MaxMind (Apache 2.0, con LICENSE).
- publicsuffix: reemplazo de golang.org/x/net/publicsuffix con la PSL embebida (#load, MPL 2.0). Snapshot 2026-11 (puede diferir del de x/net); punycode sí, IDNA/NFC no.
- filterlists: bloom de tannerryan/ring v1.1.2 portado bit a bit (incluido el quirk de cola de murmur3 y el formato binario v1) → los blooms cacheados por Go cargan; índice, decoder DSDL (gzip, uvarint, dsd), updater, purge.
- geoip: identidad de archivo por handle (GetFileInformationByHandle) para detectar reemplazo; Geo_IP_DB con refcount (get_reader retiene, release_db).

Hooks: intel.set_reverse_resolver (resolver, etapa 4); intel.deps.make_message_record (nsutil, 4; nil = port local); filterlists.Instance.add_online_status_callback (netenv, 3); geoip/filterlists Instance.intel_updates (^core.Updater, coreupdater); customlists.Instance.config.

Bugs de Go corregidos: entradas viejas tras un update (la interfaz cachea 256 records y los batch writes la saltean → Go sigue sirviendo entradas borradas/whitelisteadas/purgadas); posible deadlock en updateListIndex (lock no reentrante re-tomado vía ResolveListIDs); Entity.GetCountryInfo deref de location nil con IP no global; recursión sin límite en getCategorySources (ciclo → stack overflow; tope 32); ring.UnmarshalBinary acepta blobs truncados.
Conservados: quirk murmur3 (compatibilidad de caché); ratio NaN en lista custom vacía; algoritmo PSL exacto de x/net.

Trampas: readers mantenidos vivos por el GC → refcount; updates.Version presta su texto (clonar); records de database.get entran a la caché compartida → heap_allocator; free_all(temp) dentro del loop liberaba strings del caller → TEMP_GUARD por entrada + arena virtual; Write tool borra espacios finales (el markdown de Go usa "  \n"); $TEMP es ruta Windows → cygpath -u. Deja %TEMP%\pm-geoip-test-<pid> por corrida.
