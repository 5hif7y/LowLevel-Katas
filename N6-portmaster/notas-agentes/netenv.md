# service/netenv — informe (42 tests x3)

- Una sola llamada a GetAdaptersAddresses (interfaces, unicast/anycast, gateways, DNS, sufijos, métricas) + GetIpForwardTable2 para la ruta por defecto. Reemplaza los dos comandos PowerShell de Go (~1 s cada uno, scrapeo de texto).
- Traceroute sobre IcmpSendEcho (sin admin) con la semántica de Go (hasta 4 saltos, 2 intentos, timeout hop×20+100 ms).
- Online status: máquina de estados completa; tests offline con servidor HTTP local, responder DNS UDP local y direcciones simuladas (302 → Portal con notificación, 204 + DNS correcto → Online, puerto DNS cerrado → Offline, HTTP colgado → timeout).
- `netenv.install_hooks()` llena: state.deps.ipv6_enabled, status.deps (online status, captive portal, callback), broadcasts.deps.get_internet_location, filterlists add_online_status_callback (asignar ANTES de new_filter_lists: copia el Instance por valor). Hook nuevo: Instance.submit_intel_resources_updated (etapa 6).
PORT-TODO: check HTTP de portal no se ata a la dirección local permitida (WinHTTP no permite elegir la local); resolver (etapa 4) debe setear dns_test_query_func; Linux; EventData del portal nunca se libera (otros pueden retener la notificación).

Bug de Go corregido: DeviceLocations.Copy() ignora el receptor y copia el global.
Conservados: dominio de conectividad "ping.archlinux.org" sin punto final (nunca matchea un FQDN — decide qué trata Portmaster como chequeo de conectividad); caché de GetDefaultInterface mantiene el valor viejo ante error; Less(i,j) no es un orden estricto → se reproduce el sort de Go (insertion sort ≤ 12) para obtener el mismo orden; ubicaciones v4/v6 por timezone (IP nil) colapsan en una.

Trampas: *InterfaceInfo apuntando a una caché que el refresh reemplaza → copias; singleflight con 100 goroutines → 16 hilos + barrera (crear hilos cuesta 50–150 ms); Wait_Group de Odin hace panic con add mientras alguien espera ("misuse") → flag + cond; lock/unlock en hilos distintos → Atomic_Mutex; paquetes que el firewall libera tras el veredicto → el listener ICMP copia; campo llamado `in` es error de sintaxis; IPNet IPv4 de la tabla de adaptadores viene sin máscara.
