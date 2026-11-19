# Notificaciones nativas de Windows o wnotify: cómo usar cada una (y las dos)

En Windows conviven dos sistemas de notificaciones que no se conocen entre sí:

| | **Nativo de Windows** (toasts) | **wnotify** (este port) |
|---|---|---|
| Qué es | `ToastNotificationManager` (WinRT); se ven como popup y quedan en el Centro de actividades | La spec freedesktop (`Notify`, `CloseNotification`, señales) sobre un named pipe por sesión |
| Quién lo dibuja | El shell de Windows | El daemon que elijas: dunst, herbe (vía notifyd), tiramisu (texto), o toasts nativos (vía `wnotify-toast`) |
| Cómo se envía | Una app registrada con AUMID; desde scripts, PowerShell + WinRT | `notify-send`, cualquier cliente del pipe (C, Odin, PowerShell) |
| Historial | El Centro de actividades, hasta que lo limpiás | `notifyd`: SQLite, permanente, buscable |
| Ventajas | Integrado: "No molestar", Focus Assist, Phone Link, accesibilidad, mismo look que el resto | Scriptable, `replaces_id`, acciones y su resultado, urgencias, hints, reglas de dunst, salida a stdout |
| Límites | Para apps sin paquete hace falta un AUMID registrado; PowerShell 5.1 no puede escuchar sus eventos (clic/descartar) | No lo ven las apps de Windows: sólo lo usan programas que hablen wnotify |

`notifyd` es el puente entre los dos: **importa** los toasts nativos a su
historial (`--windows`) y puede **mostrar** las notificaciones wnotify como
toasts nativos (`--exec wnotify-toast.exe`).

---

## 1. Usar sólo el sistema nativo

Enviar un toast desde PowerShell (Windows PowerShell 5.1, sin módulos):

```powershell
[void][Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType = WindowsRuntime]
[void][Windows.Data.Xml.Dom.XmlDocument, Windows.Data.Xml.Dom.XmlDocument, ContentType = WindowsRuntime]
$xml = New-Object Windows.Data.Xml.Dom.XmlDocument
$xml.LoadXml(@'
<toast>
  <visual><binding template="ToastGeneric">
    <text>Build FAILED</text>
    <text>3 tests fallaron</text>
    <image placement="appLogoOverride" src="file:///C:/icons/build.png"/>
  </binding></visual>
</toast>
'@)
$aumid = '{1AC14E77-02E7-4E5D-B744-2EB1AE5198B7}\WindowsPowerShell\v1.0\powershell.exe'  # AUMID de PowerShell
[Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier($aumid).Show(
    (New-Object Windows.UI.Notifications.ToastNotification $xml))
```

* El AUMID decide qué nombre e ícono muestra Windows como remitente. El de
  PowerShell existe en toda instalación; una app propia necesita el suyo
  (MSIX, o un acceso directo en el menú Inicio con `System.AppUserModel.ID`).
* El módulo de la galería [BurntToast](https://github.com/Windos/BurntToast)
  envuelve todo esto (`New-BurntToastNotification -Text 'Hola'`).
* Lo que se pierde: no hay `replaces_id`, no sabés si el usuario hizo clic
  (PowerShell 5.1 no puede suscribirse a eventos WinRT) y el historial
  desaparece cuando limpiás el Centro de actividades.

**Guardar igual el historial:** `notifyd --windows` (headless) lee el
almacén de toasts de Windows y registra cada uno en SQLite, incluido el de
cualquier otra app (Discord, Outlook, el navegador). No hay que cambiar nada
en las apps.

```powershell
notifyd.exe --windows          # sólo registra; Windows sigue mostrando todo
notifyd.exe history --app Discord
```

## 2. Usar sólo wnotify

```powershell
dunst.exe                                   # o: notifyd --exec herbe.exe / tiramisu.exe
notify-send -a build -u critical -i C:\icons\build.png "Build FAILED" "3 tests"
notify-send -A "open=Abrir log" "Deploy listo" "prod"    # imprime la acción elegida
```

Se ve con el estilo del daemon (dunst: reglas, colores, markup; herbe:
minimalista). Las apps nativas de Windows siguen usando sus toasts por su
cuenta.

## 3. Apps wnotify, pero con la interfaz de Windows

Si preferís el look de Windows para todo: `notifyd` recibe por wnotify y
muestra cada notificación como toast nativo con `wnotify-toast.exe`.

```powershell
notifyd.exe --exec N4-notifications\toast\wnotify-toast.exe
notify-send -a Correo -i C:\icons\mail.png "Mensaje de Ana" "Reunión a las 10"
```

* `wnotify-toast` cumple el contrato de herbe, así que notifyd lo trata igual:
  clic → `ActionInvoked(id, "default")`; descartado o vencido → cerrado (2);
  `CloseNotification` → el toast se retira del Centro de actividades (3).
* Toma `app_icon` (o el hint `image-path`) como ícono, `app_name` como
  atribución, y urgencia crítica como toast tipo recordatorio (no se va solo).
* `--keep` (como argumento de `wnotify-toast`) lo deja esperando en el Centro
  de actividades después del popup, en vez de cerrarlo al vencer.
* Remitente: por defecto el AUMID de PowerShell; `--aumid` para otro.

## 4. Las dos cosas a la vez, con un solo historial

```powershell
notifyd.exe --windows --exec N4-notifications\toast\wnotify-toast.exe
```

Todo se ve como toast nativo y todo queda en `history.sqlite3`: las
notificaciones wnotify (con su ciclo completo: replace, acciones, cierre) y
los toasts de las demás apps. Los toasts que muestra el propio
`wnotify-toast` (grupo `wnotify`) **no** se re-importan, así que no hay
duplicados.

O con dunst dibujando lo de wnotify y Windows lo suyo:

```powershell
$env:WNOTIFY_PIPE='\\.\pipe\dunst-down'; Start-Process dunst.exe; Remove-Item Env:WNOTIFY_PIPE
notifyd.exe --windows --forward \\.\pipe\dunst-down
```

## ¿Cuál elijo?

* Querés que se vea como el resto de Windows y respetar "No molestar" → **3** o **4**.
* Querés reglas, scripts, acciones con resultado, estilo propio → **2** (dunst).
* Sólo querés no perder nunca una notificación, sin cambiar cómo se ven → **1** con `notifyd --windows`.

## Qué está verificado

`tests/run-tests.ps1`, secciones `notifyd-windows` y `toast`: importación de
toasts (UTF-8, entidades, AUMID → nombre, `<image src>` → ícono, retiro → cierre
4), toast nativo mostrado por notifyd con ícono (`app_icon` y `image-path`),
atribución y UTF-8, cierre por `WM_CLOSE`/`CloseNotification` sin dejar restos
en el Centro de actividades, vencimiento → exit 2, y sin duplicados al combinar
`--windows` con `--exec wnotify-toast`. **No automatizado:** el clic del usuario
sobre el toast (exit 0 / `ActionInvoked`) — requiere una persona.
