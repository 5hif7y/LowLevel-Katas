# Brief común para agentes de porteo (leer completo antes de empezar)

Proyecto: port de Portmaster (Go) a Odin, por etapas. Fork: `C:\Users\Shifty\DEV\gitclones\portmaster`, rama `port/odin`. Odin: `odin` en PATH (dev-2026-10-nightly, fuentes en `C:\Software\Odin-oct-2026` — leer `core/` ante cualquier duda; `core:os` es la API NUEVA). Windows x64, Git Bash. No hay toolchain de Go.

## Antes de escribir código
1. `odin/PORTING.md` (convenciones) y `odin/STAGES.md` (qué está hecho, PORT-TODOs, hooks).
2. Todas las notas de esta carpeta (`notas-agentes/*.md`): APIs ya portadas y trampas ya resueltas.
3. Módulos de la guía 22–26: `C:\Users\Shifty\DEV\LowLevel-Katas-n6\N1-coreutils\Docs\porter-guide\`.

## Reglas
- Escribir SOLO en los directorios de tu alcance. Otros agentes trabajan en paralelo en directorios hermanos. No commitear, no cambiar de rama, no editar `STAGES.md` (lo actualiza el coordinador con tu informe).
- Imports: colección `pm` (`import "pm:base/log"`, compilar con `-collection:pm=odin` desde la raíz del fork).
- Nombres de paquete únicos en todo el programa y nunca iguales a un paquete del core: usar prefijo `pm_` (`package pm_network`), el import sigue usando el directorio.
- Dependencias hacia etapas futuras: struct de hooks en `deps.odin` del paquete (nil = no disponible, comentario con paquete y etapa que lo llena). Si dependés de un paquete que otro agente porta EN ESTA MISMA ronda: portá primero lo que no lo necesita y chequeá periódicamente `odin check odin/<pkg> -collection:pm=odin -no-entry-point`; cuando compile, conectalo. Si al final no está, hook + PORT-TODO preciso.
- Linux/macOS (`_linux.go`, eBPF, nfqueue, iptables, /proc, dbus): fuera de alcance → archivo `*_other.odin` mínimo o PORT-TODO. El objetivo es Windows.
- Datos compartidos entre hilos: `runtime.heap_allocator()`. Nunca devolver una vista (`arr[:]`, un `map`) de estado global mutable: copia bajo lock (ver módulo 26).
- Herramientas: los escapes `\uXXXX` en Write/Edit se convierten en el carácter literal; heredocs/python manglean backslashes. Construir escapes en código si hace falta.
- NO cargar drivers, no modificar la configuración de red, firewall, DNS o servicios del sistema, no requerir admin. Lo que necesite el driver real se prueba a nivel de protocolo (bytes) y se deja un test manual documentado, saltado por defecto.

## Tests
- Portar los `*_test.go` del paquete (son la especificación) y agregar los propios.
- Correr: `export PATH="$PATH:/c/Users/Shifty/DEV/vcpkg/installed/x64-windows/bin"` (sqlite3.dll), luego `timeout 600 odin test odin/<pkg> -collection:pm=odin -vet -out:$TEMP/<algo>.exe > archivo.log 2>&1` (salida a archivo: los pipes con head esconden cuelgues). Correr cada suite **3 veces** (las carreras no aparecen en una sola corrida).
- `odin check odin/<pkg> -collection:pm=odin -vet -no-entry-point` limpio.
- Sin red real salvo un smoke test opcional y saltado por defecto.

## Informe final (en este formato)
Archivos · portado vs PORT-TODO (con motivo) · hooks (nombre, quién lo llena, etapa) · comandos de test + resultados (3 corridas) · bugs de Go encontrados (corregidos / conservados y por qué) · trampas concretas (construcción Go → qué se rompió → arreglo).
