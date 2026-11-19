# base/notifications + puente a notifyd — informe (8/8 tests, x4, e2e contra notifyd real)

Ver módulo 25 de la guía para el diseño. Trampas: strings.replace_all devuelve su entrada si no reemplaza; `any` desde local apunta al stack; deadlock de delete en storage inyectado; carrera respuesta/señal (mutex durante IPC + mapa); join del lector con lock tomado; allocators de wnotify cruzando hilos; canales → Cond con contador de esperas; heap_allocator() en "contextless" no compila; expired_wait despierta antes de salir del set; notifyd --forward sólo registra closed si el display lo confirma.
PORT-TODO: EventData/Payload no se decodifican desde un put de la UI (dsd no decodifica a `any`).
Limpieza pendiente: el harness unitario deja %TEMP%\pm-notifications-test-<pid> por corrida (sin hook de teardown).

## Revisión híbrida (fork 0b8f1b28)
Historial local completo `notifications-history` + política global (default important). 12/12 tests x9 con notifyd real. Trampas: orden save (política → historial → cola) para que forwarded_global no preceda a created; worker de retención compite con el purge manual del test; Thread alocado con el tracking allocator; storage sqlite no registrado sin `@(require) import`; LIKE de sqlite case-insensitive y `_` comodín (matches_key filtra exacto); `@(fini)` no corre porque el runner termina con os.exit → queda el dir del test más reciente.
Abierto: eventos con el historial detenido se descartan; retención con resolución de 1 s.
