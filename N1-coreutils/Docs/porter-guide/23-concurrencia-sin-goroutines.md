# Módulo 23: Concurrencia sin goroutines — mgr, canales y `select`

Portmaster está construido sobre goroutines baratas, canales, `select` y
`context.Context`. `service/mgr` (módulos, workers, eventos, estados) es la
columna vertebral. Odin da hilos del sistema operativo, `core:sync` y
`core:sync/chan`. Este módulo es lo que costó cerrar esa distancia.

Código: `odin/service/mgr/` (26 tests), `odin/base/log/`,
`odin/base/rng/rendezvous.odin`, `odin/base/database/subscription.odin`.

---

## 1. Una goroutine no es un hilo

Medido en esta máquina: **crear un hilo cuesta 50–150 ms** cuando se crean
varios seguidos (con `core:thread` y con `CreateThread` crudo por igual).
`TestWorkerInfo` de Go lanza tres workers y a los 100 ms espera verlos
corriendo; en Odin el segundo y el tercero todavía no existían.

`core:thread.Pool` tiene tamaño fijo: un worker de larga vida (los de
Portmaster duran lo que dura el proceso) ocupa un hilo para siempre y la cola
se traba.

Solución, `mgr/pool.odin`: **pool elástico**. Si hay más trabajos en cola que
hilos ociosos, se crea otro hilo; los ociosos se reutilizan. Un worker
bloqueado nunca deja sin hilo a los demás, y el costo de creación se paga una
sola vez.

```odin
manager_go(m, "bandwidth update handler", bandwidth_update_handler)
w := manager_repeat(m, "cleaner", 10 * time.Second, clean_notifications)
```

`manager_go` cuenta el worker **antes** de encolarlo: un `wait_for_workers`
justo después no puede perderlo (en Go, el `go` y el contador eran casi
atómicos por el scheduler).

## 2. `select` no existe (y los canales del core no son los de Go)

```go
select {
case <-ctx.Done():  return ctx.Err()
case <-ticker.C:    flush()
case <-trigger:     flush()
case l := <-logBuffer: write(l)
}
```

`core:sync/chan` tiene `try_send`/`try_recv` y un `select_raw` **no
bloqueante** (el bloqueante es un alias deprecado), que además **saltea los
canales cerrados**: el patrón "canal `done` que se cierra para avisar" no
funciona.

Reemplazo usado en todo el port: **un `Mutex` + `Cond` + flags**, y la
cancelación del contexto despierta la condición.

```odin
Writer :: struct { mu: sync.Mutex, cond: sync.Cond, queue: [dynamic]^Line, trigger: bool }

for {
	sync.mutex_lock(&w.mu)
	for len(w.queue) == 0 && !w.trigger && !mgr.ctx_done(ctx) {
		sync.cond_wait_with_timeout(&w.cond, &w.mu, next_tick_in())   // el ticker
	}
	...
}
// en el arranque: que cancelar el ctx despierte la cond
mgr.ctx_after_func(ctx, mgr.cond_waker_proc, &mgr.Cond_Waker{&w.mu, &w.cond})
```

* `time.After` dentro de un `select` → `cond_wait_with_timeout` o polling de
  `try_send` con backoff (`storage/iterator_send.odin`).
* Un canal de "trigger" sin buffer con envío no bloqueante **pierde** el
  trigger si el receptor está ocupado → flag + `cond_signal`.
* `cond_wait_with_timeout` en Windows trunca a milisegundos enteros.

### El bug de los canales sin buffer

`chan.Chan(T)` sin buffer guarda **un solo** `unbuffered_data`. Con varios
emisores (todos los feeders de entropía de `base/rng` mandan al mismo canal)
el segundo pisa el dato del primero mientras éste espera: valores perdidos o
entregados dos veces. Con `[]u8` eso fue un double free y un slice de 26 bytes
donde se habían mandado 32. Reemplazo: `rng/rendezvous.odin` (mutex + cond,
un dato por emisor).

Los canales **con** buffer funcionan, y recibir de un canal cerrado entrega
primero lo que quedaba (verificado): es lo que usa `iterator.next`.

## 3. `context.Context` → `^mgr.Context`

```odin
ctx := mgr.with_timeout(parent, 5 * time.Second)
defer mgr.ctx_destroy(ctx)          // = defer cancel()
if mgr.ctx_wait(ctx, 100 * time.Millisecond) { return mgr.ctx_err_error(ctx) } // <-ctx.Done()
mgr.ctx_sleep(ctx, d)               // select { time.After(d) / ctx.Done() }
```

* Heap + refcount: el padre referencia a sus hijos para cancelarlos, el hijo
  puede morir antes.
* Orden de locks: el padre cancela a los hijos **con su lock tomado**
  (padre → hijo). Un hijo que se desengancha del padre suelta primero el suyo.
  `cancel_internal` nunca libera referencias, así que nada se libera con un
  lock tomado.
* Deadlines: un único hilo timer, creado bajo demanda. Sólo hace
  "try-retain" de contextos con refcount > 0 (no resucita uno que se está
  liberando).

## 4. Sin `recover`

Go: un panic en un worker se loguea con stack y el worker se reinicia. Odin no
tiene `recover`: un `assert` fallido mata el proceso. El port instala un
`assertion_failure_proc` por hilo que imprime `===== PANIC =====` con el
nombre del manager y del worker antes de abortar (los fallos de bounds check
no pasan por ahí). Es una diferencia de comportamiento real y está en
`STAGES.md`: la robustez pasa a depender de no tener bugs, no de
sobrevivirlos.

## 5. Mutex que suelta otro hilo

El call limiter de `base/utils` toma un mutex en una goroutine y lo suelta en
otra. En Windows `sync.Mutex` es un **SRWLOCK**, que debe soltarlo el hilo
dueño. Reemplazo: `sync.Atomic_Mutex`, sin dueño.

## 6. Allocators y hilos

`context.allocator` en un test es el **tracking allocator del test runner,
por test**. Cualquier cosa que cruce de hilo o sea global (líneas de log,
records de una suscripción, el propio `^thread.Thread`, que guarda el
allocator de creación) alocada con él aparece como leak o "bad free" cuando la
libera otro hilo.

Regla: estado compartido → `runtime.heap_allocator()` explícito; los hilos
arrancan con `runtime.default_context()`; el temp allocator se vacía después
de cada trabajo. `storage.worker_context(allocator)` arma un contexto con el
allocator del que abrió el storage.

`heap_allocator()` necesita contexto; para usarlo en código `"contextless"`
se arma el `Allocator` a mano con `heap_allocator_proc`.

## 7. Lo que el GC hacía: refcount

En Go un record leído de la base puede estar a la vez en la caché LRU de la
`Interface`, en N feeds de suscripción y en el hashmap; el GC lo libera cuando
nadie lo mira. En Odin:

```odin
Base :: struct { ..., _refs: i32 }
retain  :: proc(r: ^Record) -> ^Record { sync.atomic_add(&r._refs, 1); return r }
destroy :: proc(r: ^Record) { if sync.atomic_sub(&r._refs, 1) == 1 { r._vtable.destroy(r) } } // atomic_sub devuelve el viejo
```

Contrato resultante: lo que devuelve `get` o llega por un feed es **una
referencia tuya** (`record.destroy` al terminar). Un hook o un storage
inyectado devuelve el mismo record que recibió o una referencia nueva que
cede. Devolver "una copia retenida del mismo record" filtró una referencia en
el glue de `config` hasta que se escribió esto.

`mgr` aplica lo mismo a contextos, suscripciones, callbacks y `Worker_Mgr`.
Un worker que termina decrementa el contador del manager como **último**
acceso al manager, para que `destroy_manager` no lo libere debajo.

## 8. Tests de Go que asumían el scheduler

* "Lanzo 100 goroutines y arrancan juntas" → barrera `sync.Wait_Group`.
* `time.Sleep(time.Millisecond)` ≈ 1 ms → en Windows ≈ 15,6 ms: contar
  iteraciones de 10 ms deja de tener sentido; se mide tiempo transcurrido.
* "Duermo 100 ms y miro una vez" → polling con timeout (3 s).
* `TestMain` + estado global: el runner de Odin corre tests en **4 hilos**;
  lo que toca la base global va en un único `@(test)` secuencial o detrás de
  un mutex global + `sync.once_do`.

## 9. Bugs de Go que aparecieron al portear

Portear obliga a leer cada línea; salieron bugs reales del upstream. Se
corrigieron en el port (comentario `NOTE` en el código) salvo que cambiar el
comportamiento rompiera compatibilidad:

| Dónde | Bug |
|---|---|
| `base/rng` | el reseed cada 1 MB nunca ocurre: el contador de bytes no se incrementa |
| `base/rng` | `SupplyEntropyIfNeeded` con la condición invertida; sesgo de módulo y división por cero en `Number(0)` |
| `base/utils` | `StablePool` saltea el slot `index-1` y puede perder un elemento |
| `base/utils/osdetail` | `START_PENDING` mapeado a `StopPending`; caché de servicios sombreada (nunca se usa); `bytes.IndexAny` matchea la "t" de "Active code page" |
| `service/mgr` | `SleepyTicker.SetSleep(false)` cierra un canal nil (panic) |
| `base/database` | `FlushCache` con la condición invertida (nunca hace nada); carrera en `getController` que arranca un storage dos veces; `Interface.Delete` sigue sirviendo el borrado desde la caché |
| `storage/sqlite` | **conservado**: `auto_vacuum=INCREMENTAL` llega después de crear `gorp_migrations` y SQLite lo ignora; el port hace lo mismo para que los archivos sean idénticos |

---

Anterior: [22](22-go-a-odin-mapa-del-lenguaje.md) · Siguiente: [24 — Compatibilidad binaria con Go](24-compatibilidad-binaria-con-go.md).
