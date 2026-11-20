# service/network + nameserver/nsutil — informe (26 + 4 tests x3)

- Connection con el JSON exacto de Go (33 campos en orden, net.IP como texto, nil → null; Reason.Context según tipo; test con el string exacto). Verdict :: enum i8 con los valores de Go. Clave `network:tree/<pid>/{ip,dns}/<id>`.
- Cola de paquetes por conexión: FIFO de 100 con mutex+cond despertada por el ctx; llena → drop (como el `select default` de Go).
- Base "network" inyectada; llama process.set_db_controller. Puente nsutil → intel.deps.make_message_record en install_bridges().
- **Gate de features pagas:** `deps.may_use(feature)` (lo llenará spn/access, etapa 5); mientras sea nil decide `local_may_use`: "history" permitido y lo decide la opción por app existente `history/enable` (default false); "bw-vis" → opción nueva global `bandwidth/enable` (default false, anotación requires-feature "bw-vis"); otras features: no.
Hooks: resolver_get_ip_info, resolver_add_to_debug_info (4); Instance.resolver_is_disabled; may_use (5); Opaque_Value para TunnelOpts/TunnelContext (5); Custom_Reason_Context; Instance.profile/dns_monitor (6). Resolver_Info/DNS_Request_Context/Resolved_Domain replicados en network (resolver no puede importar network) → convertir o mover en la etapa 4.
Contratos: todo ^Connection devuelto es una referencia nueva (release); handle_packet se queda con el paquete; set_verdict se queda con el contexto (heap); delete_connection exige el lock de la conexión y una referencia propia.

Bugs de Go corregidos: el gauge `network/connections/active/total` contaba las conexiones TERMINADAS (Ended != 0); log "after %d tries" pasaba +1 en vez de i+1; sin default firewall handler → llamada a func nil. Desvío: Query bloqueaba para siempre en `it.Next <- r` sin mirar Done → iterator_send con 10 s. Conservado: RemovePrompt borra la notificación pero deja el puntero.

Trampas: delete_key devuelve (key, value) sin ok; `for x in chan.recv(...) or_break` no compila; el proceso de test es "Portmaster" para process (perfil especial _portmaster, Internal=true → historial nunca activo); opciones en capas cacheadas por revisión del perfil; `_` ordena después de los dígitos; rng y metrics deben arrancar antes; tracer sin submit hay que destruirlo; set_sleep competía con el worker que destruye su ticker.
