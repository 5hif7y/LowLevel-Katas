# base/log, base/utils (+osdetail, renameio, debug), base/rng, base/info — informe

Tests: info 2, log 4, utils 18, osdetail 7, renameio 2, debug 1, rng 5 (x5) — OK.

- log: `package pm_log` (choque con core:log). Hilo escritor, cola 1024 (mutex + 2 conds), compactación " [Nx]", scheduling, BOF/EOF, ring de logs inesperados, formato Go con hora local (SystemTimeToTzSpecificLocalTime), colores por VT processing, file writer `<dir>/YYYY-MM-DD_HH-MM-SS.log` + clean_old_logs, Context_Tracer explícito, puente runtime.Logger, buffer pre-start (16k).
- utils: Error :: union #shared_nil {os.Error, Utils_Error}; ACLs reales (osdetail/acl_windows.odin); closures → proc(rawptr)+data; Stable_Pool($T); BroadcastFlag con contador de generación + futex; escritura atómica.
- osdetail: RtlGetVersion; SCM API en vez de parsear `sc`; EnumServicesStatusExW.
- rng: generador Fortuna (AES-256, contador 128-bit LE, rekey por request, reseed SHA256(SHA256(key‖seed))), semilla de crypto.rand_bytes, feeders como hilos.
- info: metadatos por #config (PM_VERSION, PM_COMMIT, ...).

Bugs de Go corregidos: stable pool salteaba el slot index-1; START_PENDING mapeado a StopPending; caché de nombres de servicio sombreada; RunTerminalCmd con bytes.IndexAny matcheaba la "t" de "Active code page"; rng: el disparador de reseed de 1MB nunca disparaba (contador nunca incrementado), sesgo de módulo y división por cero en number(0), SupplyEntropyIfNeeded con condición invertida, conteo de entropía del feeder nunca reseteado.

PORT-TODO: TriggerWriterChannel; reinicio del writer tras panic; atributos slog; tracer↔mgr ctx; zona horaria no-Windows; add_goroutine_stack; detección de VM; Serpent (no está en core:crypto); rng↔mgr.

Trampas Go → Odin:
1. Nombre de paquete `log` choca con core:log de core:testing → `package pm_log` (el import sigue llamándose log por el directorio).
2. core:sync/chan sin buffer con varios emisores: un único slot unbuffered_data; el segundo emisor pisa al primero → valores perdidos/duplicados, double free. Rendezvous mutex+cond (rng/rendezvous.odin).
3. Sin select bloqueante → mutex + conds; time.After → cond_wait_with_timeout.
4. Datos que cruzan hilos o son globales con runtime.heap_allocator() (thread.create guarda context.allocator).
5. Go bloquea un mutex en una goroutine y lo libera en otra; sync.Mutex en Windows es SRWLOCK (lo debe liberar el dueño) → sync.Atomic_Mutex.
6. sync.atomic_add devuelve el valor VIEJO (Go AddInt32 devuelve el nuevo): `Add(-1)==0` → `==1`.
7. or_return exige resultados nombrados; `_ = f() or_return`.
8. proc `new` a nivel de paquete sombrea el builtin → create().
9. Constante string no indexable en runtime.
10. uuid.generate_v4 exige random_generator criptográfico → v4 desde crypto.rand_bytes.
11. Tests de Go que suponen 100 goroutines arrancando juntas y Sleep(1ms)≈1ms → barrera Wait_Group, cotas más anchas.
12. odin check también chequea *_test.odin.
13. Formatter de time.Duration ignora ancho.
14. os.rename ya reemplaza en Windows; time.now() es UTC (Go: local); declarar SCM/ACL Win32 con foreign import advapi32.
15. (herramienta) heredocs/python manglean backslashes en fuente Odin.
