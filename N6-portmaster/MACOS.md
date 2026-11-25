# macOS sin Mac: opciones (investigación, 2026-11-24)

Contexto: el objetivo es un SPN/VPN privado y personal compartido por Linux,
Windows, macOS y Android. El Portmaster original nunca tuvo cliente de macOS:
el port compila para `darwin_arm64` (14 `PORT-TODO(darwin)`), pero interceptar
tráfico en macOS es **desarrollo nuevo**, no parte del port. Investigación web
hecha por un agente; lo marcado "sin verificar" no se pudo confirmar en fuentes.

## Resumen

* **Compilar y correr tests**: runners macOS de GitHub Actions, nativos, en
  las dos arquitecturas. Barato o gratis.
* **Intercepción en vivo**: hace falta un Mac real con admin y GUI (VNC), más
  el Apple Developer Program (99 USD/año) para firmar la Network Extension.
* Mac en la nube: **AWS EC2 Mac** es el único que deja apagar SIP por API;
  **Scaleway** es el más barato, pero no deja tocar SIP.
* Cross-compilar desde Windows/Linux alcanza, como mucho, para un CLI sin
  frameworks. Las VMs macOS en PC son sólo x86_64, quedan fuera de la licencia
  de Apple y se terminan: macOS 26 Tahoe es el último para Intel.

## Comparación

| Método | Oficial | Costo aprox. | Compilar | Tests | Intercepción en vivo | Limitaciones |
|---|---|---|---|---|---|---|
| GitHub Actions `macos-26` (arm64) / `macos-26-intel` | Sí | Gratis en repo público; privado ~0,062 USD/min (~193 min/mes con el crédito del plan Free) | Sí, nativo | Sí | No (sin GUI, SIP ni MDM) | Efímero; Intel hasta otoño 2027 |
| AWS EC2 `mac-m4` / `mac2` | Sí | 1,23 USD/h (mín. 24 h ≈ 29,5 USD) / ~0,65 USD/h (sin verificar) | Sí | Sí | Sí: SIP por API, aprobar la extensión por VNC (inferido) | Mínimo 24 h; puede pedir aumento de cuota |
| Scaleway Mac mini M4 | Sí | 0,22 €/h (mín. 24 h ≈ 5,3 €), 149 €/mes | Sí | Sí | `pf` sí; Network Extension firmada probablemente sí (sin verificar) | No se puede apagar SIP |
| MacStadium (bare metal) | Sí | desde 109 USD/mes | Sí | Sí | Sí (SIP por soporte, sin verificar) | Sólo mensual |
| MacinCloud por hora | Sí | ~1 USD/h (dato viejo) | Sí | Sí | No (usuario sin admin) | El plan dedicado sí da root |
| Apple Developer Program | Sí | 99 USD/año | — | — | Requisito para firmar la Network Extension (Developer ID) | — |
| VM macOS x86 en PC (OSX-KVM, Hyper-V + OpenCore, VMware) | No | 0 | Sí (x86_64) | Sólo amd64 | Parcial (SIP apagable; firma dudosa) | Fuera de la licencia de macOS; frágil; termina con Tahoe 26; en Hyper-V sólo CPUs Intel |
| Cross-compile (objeto Odin + zig/lld/osxcross) | No (el SDK sólo se licencia para hardware Apple) | 0 | Parcial (los frameworks necesitan el SDK) | No | No | Sin forma de testear |

## Puntos clave

* **Entitlements de Network Extension**: desde 2016 cualquier miembro pago los
  habilita sin pedido especial (salvo push provider y Hotspot Helper). En
  macOS, el filtro de contenido, el de paquetes, el proxy transparente y el
  packet tunnel van como *system extension* firmada con Developer ID.
* **Desarrollo local**: la app contenedora va en `/Applications` y cada
  activación se aprueba a mano en System Settings. `systemextensionsctl
  developer on` exige SIP apagado.
* **`pf` sin Network Extension**: como root no necesita entitlements ni el
  programa pago. Sirve para filtrar por IP/puerto, pero no atribuye por app.
* **Odin**: el linkeo cruzado a darwin está deshabilitado; se puede emitir un
  objeto (`-target:darwin_arm64 -build-mode:obj`), pero sin forma de probarlo.
* **Licencia de macOS**: permite hasta 2 VMs por cada computadora Apple
  propia, para desarrollo y testing; en hardware no-Apple queda fuera.

## Camino propuesto, por etapas

1. **Ya**: seguir con `odin check -target:darwin_arm64` desde Windows
   (`odin/tests/crosscheck.sh`), como en el port.
2. **CI**: un job de GitHub Actions con `macos-26` y `macos-26-intel` que
   instale Odin y corra `odin build` y `odin test`. Entra en el crédito
   gratis si los jobs son cortos.
3. **MVP sin entitlements**: daemon root con launchd + `pf` (anchors propios)
   + DNS. Se prueba en una sesión de 24 h de Scaleway M4 (~5 €) por SSH/VNC.
4. **Intercepción por app**: Apple Developer Program (99 USD), app contenedora
   mínima, system extension (NEFilterDataProvider para el firewall por app,
   NEPacketTunnelProvider o NETransparentProxyProvider para el SPN), daemon
   Odin por XPC o socket. Primero en Scaleway; si hace falta SIP apagado,
   AWS EC2.
5. **Notificaciones y keychain moderno**: sobre el bundle firmado.
6. Si se itera a diario, un Mac mini M4 se amortiza frente a las sesiones de
   AWS, y además permite VMs macOS legales (tart, UTM).

## Sin verificar

Si la notarización es obligatoria para activar una system extension en
desarrollo; si Scaleway deja aprobar extensiones por VNC sin tocar SIP; si
MacStadium apaga SIP; tarifas actuales de MacinCloud y `mac2.metal`; si los
runners arm64 traen Rosetta para correr tests amd64; un pipeline Odin + zig/lld
para darwin probado de punta a punta; que Network Extension ande en EC2.

## Fuentes

* Network Extension: https://developer.apple.com/forums/thread/67613 ,
  https://developer.apple.com/documentation/technotes/tn3134-network-extension-provider-deployment ,
  https://developer.apple.com/forums/thread/725805 ,
  https://developer.apple.com/forums/thread/663135 ,
  https://developer.apple.com/documentation/security/disabling-and-enabling-system-integrity-protection
* GitHub Actions: https://docs.github.com/en/billing/concepts/product-billing/github-actions ,
  https://github.blog/changelog/2026-02-26-macos-26-is-now-generally-available-for-github-hosted-runners/ ,
  https://developer.apple.com/forums/thread/771712
* AWS: https://aws.amazon.com/ec2/instance-types/mac/ ,
  https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/mac-sip-settings.html
* Scaleway: https://www.scaleway.com/en/pricing/apple-silicon/ ,
  https://www.scaleway.com/en/docs/apple-silicon/faq.md
* MacStadium: https://macstadium.com/pricing
* VMs: https://github.com/Qonfused/OSX-Hyper-V , https://github.com/acidanthera/MacHyperVSupport ,
  https://github.com/kholia/OSX-KVM , licencia: https://www.apple.com/legal/sla/docs/macOSSequoia.pdf
* Cross-compile: https://forum.odin-lang.org/t/how-to-crosscompile-from-mac-m1/891 ,
  https://github.com/tpoechtrager/osxcross , https://github.com/rust-cross/cargo-zigbuild
