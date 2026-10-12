# Módulo 06: Seguridad en OpenBSD: `pledge(2)` y `unveil(2)` en Windows

## 1. La Filosofía de Mitigación de OpenBSD

OpenBSD es reconocido mundialmente por su enfoque proactivo hacia la seguridad de software. En lugar de limitarse a auditorías manuales, su equipo de ingeniería diseñó mecanismos en el kernel para limitar el daño potencial de una vulnerabilidad de día cero (*Zero-Day*) o corrupción de memoria:

1. **`pledge(promises, execpromises)`**:
   Permite a un proceso renunciar voluntariamente al acceso a clases enteras de llamadas al sistema. Una vez que el proceso realiza una promesa, **jamás puede revocarla ni adquirir más permisos**. Si el proceso intenta invocar una syscall no permitida por su promesa, el kernel termina el proceso de inmediato con la señal `SIGABRT` y genera un volcado de memoria.
2. **`unveil(path, permissions)`**:
   Restringe la visión del sistema de archivos del proceso a un conjunto explícito de rutas y permisos (`r` para lectura, `w` para escritura, `x` para ejecución, `c` para creación). Si el proceso intenta abrir cualquier ruta fuera de las reveladas, el kernel retorna `ENOENT`.

---

## 2. El Uso de `pledge` en las Coreutils

En prácticamente todas las utilidades de OpenBSD, la primera línea ejecutable de `main()` es una llamada a `pledge`:

```c
/* openbsd-src/bin/cat/cat.c */
int main(int argc, char *argv[]) {
    ...
    if (pledge("stdio", NULL) == -1)
        err(1, "pledge");
    ...
}
```

Promesas comunes en coreutils:
- `"stdio"`: Permite únicamente E/S estándar sobre descriptores ya abiertos, memoria (`mmap`, `brk`) y operaciones básicas de consola.
- `"rpath"`: Permite abrir archivos existentes en modo sólo lectura.
- `"wpath"`: Permite abrir o modificar archivos existentes en modo escritura.
- `"cpath"`: Permite crear o eliminar archivos y directorios (`creat`, `unlink`, `mkdir`, `rmdir`).

---

## 3. Emulación en Windows: De Stubs No Operativos a Sandboxing

En un entorno de porteo directo donde el objetivo es la compatibilidad binaria sin depender de un kernel modificado:

### 3.1. Stubs Seguros en `bsd_compat`
En `compat/include/pledge.h`, `compat/include/unveil.h` y `compat/src/pledge_stub.c`:
```c
#include <pledge.h>
#include <unveil.h>

int pledge(const char *promises, const char *execpromises) {
    (void)promises;
    (void)execpromises;
    return 0; /* Éxito transparente */
}

int unveil(const char *path, const char *permissions) {
    (void)path;
    (void)permissions;
    return 0; /* Éxito transparente */
}
```
Esto permite que el código de OpenBSD mantenga intactas sus comprobaciones defensivas sin fallar en sistemas que no implementan estas syscalls.

### 3.2. Equivalentes Nativos en la Arquitectura de Windows
Si en el futuro se desea implementar un sandboxing real en Windows con semántica similar a `pledge` y `unveil`:
- **AppContainer Isolation**: Crea un perfil de seguridad aislado donde el proceso no tiene acceso al sistema de archivos salvo a través de capacidades concedidas explícitamente (`SECURITY_CAPABILITIES`).
- **Restricted Tokens (`CreateRestrictedToken`)**: Permite deshabilitar privilegios específicos de Windows (como `SeDebugPrivilege` o `SeBackupPrivilege`) y añadir SIDs denegados a la lista de acceso.
- **Process Mitigation Policies (`SetProcessMitigationPolicy`)**: Permite forzar ASLR de alta entropía, deshabilitar llamadas de sistema Win32k/GDI (`ProcessSystemCallDisablePolicy`), y bloquear la carga de binarios sin firma de Microsoft.
