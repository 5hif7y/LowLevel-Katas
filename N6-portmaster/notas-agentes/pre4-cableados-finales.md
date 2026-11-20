# Pre-etapa 4 — últimos cableados y pendientes aceptados

- `pm:base/log/logctx`: tracer atado a ^mgr.Context (Go log.AddTracer(ctx)/Tracer(ctx)); paquete aparte porque mgr importa log (ciclo). add_tracer siempre devuelve un ctx hijo nuevo (el caller siempre hace ctx_destroy).
- rng: feeders como workers de mgr (como Go module.mgr.Go); el `<-ctx.Done()` de Go se hizo con un after-func que cierra la entrada del feeder y despierta un hand-over pendiente (rv_send con flag de cancelación); stop/start reiniciable.
- dsd/http: helpers sobre pm:lib/http y pm:lib/httpclient (dirección dsd → net/http como en Go, verificado sin ciclos); round trip real servidor↔cliente en JSON/CBOR/MsgPack/YAML.
- runtime registry: un worker de mgr por provider (Go: una goroutine por provider); orden por provider conservado, el primer error cierra el iterador como errgroup.
- ACCEPTED (verificado contra el árbol Go): sin recover (mgr worker, writer de log, 500 de handlers), sin pprof/stacks/symbolizer, go_memstats, gjson wildcards (Go sólo usa paths planos), extras del router, wss/permessage-deflate (API ws:// en loopback sin compresión; SPN no tiene ship websocket), anchors/tags YAML (no aparecen en notifications.yaml, intel yml de SPN ni exports de sync), sniffing de media en dataurl, caché ARC, %e corto, big.Int, timezone no-Windows, atributos slog (el texto aplanado ya coincide con lo que imprime el handler tint de Go), schema sqlite por diseño. Ninguna función "no usada" resultó usada.
- Auditoría del patrón del bug de status (clave de map que pertenece al valor liberado): los otros 7 lugares clonan la clave aparte → sólo status lo tenía.
Quedan 40 PORT-TODO, todos de etapas 4–6 o Linux, más log/output TriggerWriterChannel (diferencia de API).
