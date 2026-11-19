# service/mgr — informe del port (26/26 tests)

Archivos: errors, context, manager, worker, worker_info, workermgr, events, hooks,
states, group, group_module, group_ext, sleepyticker, pool (+6 *_test.odin).

PORT-TODO:
1. Sin `recover`: un assert en un worker mata el proceso (Go lo logueaba y reiniciaba). Banner "===== PANIC =====" vía assertion_failure_proc por hilo.
2. slog estructurado → "msg manager=X worker=Y".
3. worker_info sin nombre de función/línea (Go lee stacks de goroutines); estado "waiting" llevado a mano.

Bug de Go no copiado: SleepyTicker.SetSleep(false) cierra un canal nil si sleepDuration>0.

Trampas Go → Odin:
1. Goroutine → hilo: crear un hilo cuesta 50–150 ms aquí; pool elástico (pool.odin) que crea hilos cuando hay más trabajos en cola que hilos libres. core:thread.Pool es de tamaño fijo y los workers largos lo bloquean.
2. `select` + ctx.Done(): core:sync/chan sólo tiene select no bloqueante y saltea canales cerrados → Mutex/Cond + flags + deadlines, despertados al cancelar con ctx_after_func + Cond_Waker.
3. Sin recover (arriba).
4. Closures → proc + data: rawptr; callbacks genéricos con proc anidado dentro del proc paramétrico (ve T en compilación).
5. Lifetimes del GC → refcount en contextos, suscripciones, Worker_Mgr; el timer sólo "try-retain" (>0); el worker decrementa el contador del manager como último acceso.
6. Allocator: memoria que cruza hilos con el tracking allocator del test → falsos leaks/bad free. Todo lo compartido usa runtime.heap_allocator() explícito; hilos con default_context(); temp allocator limpiado por trabajo.
7. Orden de locks al cancelar: padre→hijo; el hijo suelta su lock antes de desengancharse del padre.
8. `Error(Ctx_Error.None)` NO es nil en una union → helper ctx_error_to_error.
9. defer en bucles con `continue` etiquetado bajo lock → unlock/lock explícitos.
10. cond_wait_with_timeout en Windows trunca a ms; `do` es keyword, `new` builtin; procs genéricos no instanciados no se chequean; `s := f(s)` sombra falla -vet.
11. Paquete llamado `log` choca con core:log (importado por core:testing) → `package pm_log`.
12. Tests dependientes del timing de goroutines → polling con timeout.
