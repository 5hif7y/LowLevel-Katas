# service/{core(+base),ui,status,control,broadcasts} + lib/yaml + service/servicetest — informe

Tests: yaml 6, status 2, core 7, core/base 2, ui 3, control 2, broadcasts 4 — OK x3 (Go no tenía tests para estos paquetes: todos nuevos, por HTTP real contra el api server en puerto efímero). Harness compartido `service/servicetest` (log, config, sqlite, runtime, notifications en pipe privado, api; permisos por header `X-Test-Perm`).

Hooks (nil = todavía no):
- core: Instance.binary_updates / intel_updates → ^core.Updater (lo llena service/updates; lo conecta la instancia, etapa 6); get_process_by_request_origin (process, 3); captain/resolver/compat add_to_debug_info (5/4/3); get_binary_update_urls, default_intel_index_urls (configure); shutdown/restart/dirs (etapa 6). Sin updater los endpoints de updates responden 503.
- status: get_online_status, get_captive_portal, add_online_status_callback (netenv, 3); get_states / add_states_callback (instancia, 6).
- control: interception_group (firewall/interception, 3), spn_group (spn, 5), is_shutting_down, config (6); opción spn/enable la registra spn/access (5).
- ui: get_binary_update_file (updates), data_dir.
- broadcasts: intel_updates (updates), get_internet_location (netenv+geoip, 3–4), get_spn_status_json (captain, 5), get_user (spn/access, 5).

PORT-TODO: pprof y flags (6); BuildInfo de Go; State.Data null; EventData de broadcasts sin decodificar (dsd sin `any`); YAML sin anchors/aliases/tags.

Bugs de Go corregidos: getUpdateResource devuelve JSON con Content-Type application/yaml; DataError{Error: err} serializa {} y pierde el mensaje; broadcastNotify borra estados en memoria pero nunca los guarda; entrada de broadcast null → panic; authorizeApp espera la desconexión del cliente aun con el prompt expirado; ui.setArchive pisa y filtra un archivo abierto por otra request. Conservados: errInvalidReadPermission también para write; status loguea "new worst state" como error; redirAddSlash agrega "/" después del query string.

Trampas: `string([]u8{X})` no compila; `do` multilínea; unions con slices en expect_value; defer + resultados nombrados en pause/resume → procs *_locked; select timer+ticker+evento+ctx → subscription_recv con timeout min(resto,15s); canal sin buffer entre acción de notificación y handler HTTP (la acción puede correr después de que el handler volvió) → Auth_Wait en heap liberado por el último; EventData apuntando a un time.Time compartido → buffer fijo en el struct; zipfs cerrado con requests leyendo → refcount de archivos; tabwriter de Go: la línea vacía tras el header lo separa en su propio bloque; YAML 1.1: `y`/`n` son booleanos; parse_query aloca el mensaje de error; dsd serializa map vacío como null (Go: {}); marshal_data_only exige meta; opciones de config sólo se registran antes de arrancar config.

## Bug del port corregido después (fork, commit "status kept a map key pointing into a freed state update")
`set_state_locked` destruía el State_Update viejo antes de reemplazarlo, pero la clave del map `s.states` era el string `module` de ese mismo update: el map quedaba comparando contra memoria liberada → entradas duplicadas con slices liberados, worst states basura (`state=0001-01-01`), WorstState publicado equivocado y un segfault en 20 corridas (falla 2/15). Arreglo: `delete_key` primero, después liberar, y la nueva entrada usa el string de su nuevo dueño. 30 + 50 corridas limpias.
Regla: en Go la clave de un map es un valor que mantiene vivo el GC; en Odin, si la clave es un string que pertenece al valor guardado, destruir el valor antes de sacar la entrada deja la clave colgando.
