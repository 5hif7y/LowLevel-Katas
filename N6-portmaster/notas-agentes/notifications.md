# base/notifications + puente a notifyd — informe (8/8 tests, x4, e2e contra notifyd real)

Ver módulo 25 de la guía para el diseño. Trampas: strings.replace_all devuelve su entrada si no reemplaza; `any` desde local apunta al stack; deadlock de delete en storage inyectado; carrera respuesta/señal (mutex durante IPC + mapa); join del lector con lock tomado; allocators de wnotify cruzando hilos; canales → Cond con contador de esperas; heap_allocator() en "contextless" no compila; expired_wait despierta antes de salir del set; notifyd --forward sólo registra closed si el display lo confirma.
PORT-TODO: EventData/Payload no se decodifican desde un put de la UI (dsd no decodifica a `any`).
Limpieza pendiente: el harness unitario deja %TEMP%\pm-notifications-test-<pid> por corrida (sin hook de teardown).
