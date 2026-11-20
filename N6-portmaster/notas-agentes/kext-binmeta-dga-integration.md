# windows_kext/kextinterface, profile/binmeta, detection/dga, integration — informe

Tests (x3): dga 2, kextinterface 11, binmeta 13, integration 3 — OK. Ningún test carga el driver, toca el SCM ni pide admin.

kextinterface: byte-exacto contra los dos testdata reales del árbol Go: `rust_info_test.bin` (escrito por el encoder Rust) y `go_command_test.bin` (escrito por Go): 1000 infos re-codificadas idénticas, 5000 comandos re-codificados al mismo stream. Fake kext sobre named pipe para el camino overlapped real (ERROR_IO_PENDING, infos partidas en trozos de 777 bytes). `interface_version()` hace #load del version.txt del árbol Go. Test manual `-define:PM_KEXT_MANUAL=true` (abre `\\.\PortmasterKext`, pide versión; necesita driver firmado + admin).
binmeta: parser PE propio (reemplaza winres): recursos de 3 niveles, VS_VERSIONINFO, RT_GROUP_ICON+RT_ICON → .ico; decoder ICO/DIB; encoder PNG con deflate propio; ícono verificado sobre bytes reales de notepad/cmd/explorer (decodificable por core:image/png, píxeles idénticos en round trip). Mejora: en Win10 notepad.exe no tiene RT_ICON → buscar en `%SystemRoot%\SystemResources\<exe>.mun`.
integration: DLL falsa compilada en el test con las mismas exports (ABI C, callbacks, códigos de error).

Hooks: pm_integration.Instance (get_binary_file, add_resources_updated_callback; `instance_from_updater` ya existe, etapa 6); binmeta.set_profile_icon_storage_path.

Bugs de Go corregidos: KextFile.Read en loop infinito con lectura de 0 bytes; ReadFile/DeviceIoControl overlapped sin evento (IO_PENDING tratado como error; IOCTL pendiente devolvía versión en ceros); RecvInfo leía de más dentro de la info siguiente y alocaba lo que dijera el u32 (tope 64 MiB); waitForServiceStatus busy-loop con WaitHint 0; LoadAndSaveIcon pasaba ".png" con punto y fallaba siempre; getIconAndNamefromRSS envolvía err nil en vez de iconErr; ícono vacío guardado para binarios inexistentes; initializeETW dejaba una tabla de funciones a medias (crash al usarla); el callback de updates recargaba la DLL cada vez (leak + OnInitialized repetido); carrera en i.os; mensaje de StopOldSession nombraba DestroySession.
Conservados: contrato de log con sólo severidad; quirk de dga (la última corrida de letras no se mide — los tests de Go dependen de eso).

Trampas: `distinct u32` en union #shared_nil → "must have a nil value" (usar enum u32 {Success=0}); copiar desde parámetro no direccionable → `u := u`; ACL admin en base de test; os.write_entire_file require_results; los SHA1 de PNG difieren de Go (otro encoder) → la caché sólo suma entradas.
