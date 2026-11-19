# Tests de N4-notifications

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run-tests.ps1
powershell ... -File tests\run-tests.ps1 -Only icons,dunst -KeepTemp
```

Secciones: `tiramisu` (C y Odin), `herbe`, `notifyd`, `notifyd-exec`,
`notifyd-windows`, `dunst`, `chain` (notifyd → dunst). Cada daemon corre en su
propio pipe (`WNOTIFY_PIPE`) y base temporal: el suite no toca el daemon ni el
historial reales. Busca los binarios de los forks en `..\..\..\gitclones`
(`-GitClones` para cambiarlo); lo que no esté compilado se marca SKIP.

## Íconos (obligatorio en todo cambio)

| Caso | Dónde | Cómo se verifica |
|---|---|---|
| `app_icon` = ruta Windows (`C:\...\x.png`) | tiramisu, notifyd, dunst, chain | campo `#icon`/`app_icon`; en dunst, píxeles del color del PNG en una captura |
| `app_icon` = `file:///C:/...` | dunst | captura |
| hint `image-path` | tiramisu, notifyd, dunst | hint guardado; captura |
| hint `image-data` (píxeles crudos) | tiramisu, notifyd, dunst | válido se conserva, inválido se descarta; notifyd lo guarda una sola vez en `images`; captura |
| `<image src>` de un toast de Windows | notifyd `--windows` | `app_icon` = src |
| herbe | — | upstream no tiene íconos |

Las pruebas visuales cuentan píxeles de un color sólido conocido (magenta,
cian, naranja) en la pantalla antes y después de mostrar la notificación; con
`-KeepTemp` quedan las capturas.

Trampas de PowerShell 5.1 que el suite esquiva (y que rompieron su primera
versión): `@(@(a,b,c))` se aplana a `@(a,b,c)`; `ConvertFrom-Json` devuelve un
array JSON como *un* objeto; `Start-Process -PassThru` sin leer `.Handle`
deja `ExitCode` vacío; las comillas dobles embebidas en argumentos a
programas nativos se pierden.
