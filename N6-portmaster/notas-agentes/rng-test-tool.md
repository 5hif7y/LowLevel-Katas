# base/rng/test → odin/base/rng/test

Programa `test {fortuna|tickfeeder} <file> [size in MB]` portado como `package main`
en `odin/base/rng/test/` (fork `portmaster`, rama `port/odin`, sin commitear).

## Qué hay

- `main.odin`: mismo módulo `Test` con su `mgr.Manager` (se llama `"geoip"`, igual que el
  copy-paste del Go), workers `fortuna`, `tickfeeder` y `noise` (AES-CTR con
  `core:crypto/aes`, misma clave/plaintext). Mismos textos de uso/errores, mismos tamaños
  (MB = 10^6 bytes, chunks de 64 / 8 bytes), mismo progreso por stderr ("." cada 1024,
  `\n<n> bytes written\n` cada 65536). Archivo abierto sin truncar, 0644, como Go.
- `README.md`: copia textual; sólo cambia `go build` por el `odin build` desde la raíz del fork.
- `main_test.odin`: fortuna escribe exacto (4096 y 70000→70016, por los chunks de 64),
  tickfeeder produce 64 bytes, errores de args con el texto de Go (los dos usage, ParseUint
  "invalid syntax"/"value out of range", open, "only one instance allowed"), tamaño en MB del 4º arg.
  Hook de tamaño: los tests setean `output_size` directo (no hizo falta flag nuevo).
- `rng_test.odin`: actualicé el comentario que decía "not ported".

## Desvíos (documentados en el header)

- El Go tal cual no anda: `main` vuelve apenas lanza los workers (sale sin escribir nada) y
  nunca arranca el módulo rng (`rng.Bytes` daría "RNG is not ready yet"). `run()` arranca rng
  (la dependencia vieja de `modules.Register`), espera a que el worker cancele el manager,
  espera workers, para rng y cierra el archivo.
- `GOMAXPROCS(1)`: sin equivalente, omitido. `time.Sleep(10ns)` → `time.sleep`, que en
  Windows es `Sleep(0)`.

## Verificación

- 3× `odin test odin/base/rng/test` y `odin/base/rng` con `-vet`: todo verde.
- `VET=1 crosscheck.sh windows_amd64 linux_amd64 darwin_arm64 -- odin/base/rng/test`: ok ×3.
- Binario: `fortuna <tmp> 1` → 1.000.000 bytes en ~0,25 s (ojo: son 10^6, no MiB, como Go).
  Archivo y exe borrados; no queda nada corriendo.
