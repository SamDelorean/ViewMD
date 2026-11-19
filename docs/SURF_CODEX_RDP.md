# Codex en T160 mediante escritorio remoto desde Surf

Fecha: 2026-11-18. Estado: **cliente y servidor instalados**;
**aún pendiente la autenticación gráfica con contraseña real por el usuario**.

## Alcance aprobado

Se eligió el camino de escritorio remoto por simplicidad, en lugar
de habilitar el adaptador experimental de Codex App Server. La T160
ejecuta Codex CLI y conserva todos los recursos de procesamiento.
La Surface RT únicamente muestra el escritorio de GNOME a través de
FreeRDP, con teclado y ratón/táctil.

Al pulsar el botón **Codex** de la barra de Surf AI, se abre una
ventana aparte de **XFCE Terminal → FreeRDP** para autenticar una
sesión remota. El botón **no** ejecuta Codex, no cambia la conversación
GPT activa ni transmite una petición `ASK`. La ventana de Surf AI
continúa intacta. Sigue disponible la entrada independiente
**Codex — escritorio T160** en Internet/RemoteAccess.

**Nota de activación:** el binario GTK3 actualizado ya está instalado
en `~/Projects/SurfAI/ViewMD/surf/surf-ai`, pero una instancia
anterior seguía abierta durante la instalación. Cerrar y abrir Surf AI
es necesario para cargar el nuevo comportamiento del botón Codex.

## T160 (GNOME Wayland)

- Servidor nativo GNOME Remote Desktop, versión del sistema 50.2,
  habilitado como servicio de usuario `gnome-remote-desktop.service`.
- RDP en el puerto TCP `3389`, con TLS de certificado autofirmado
  y **credenciales específicas** independientes del usuario Linux.
- Usuario de RDP: `surf-codex`.
- Contraseña generada aleatoriamente y guardada sólo en T160 en
  `~/.config/surf-ai/rdp-credentials.txt` (modo 0600).
  **Nunca** se guardó en Git, en Surf ni en un comando de inicio.
- Certificado y clave TLS en
  `~/.local/share/gnome-remote-desktop/surf-codex-rdp.{crt,key}`.
  La clave tiene permisos 0600.
- Huella SHA-256 verificada en T160:
  `B5:B5:2D:27:3A:85:D4:DF:C9:31:67:D6:67:70:0C:28:3A:4D:F2:76:6B:EB:C8:F8:EB:3D:D1:E9:CF:72:39:B8`.
- Modo `mirror-primary`: comparte la sesión gráfica ya abierta.
  GNOME puede cerrar la conexión cuando se bloquee esa sesión.

El servidor escucha en todas las interfaces de T160; sólo está
protegido por TLS y autenticación RDP, **no** por un firewall restrictivo
dado que UFW estaba deshabilitado. Es un despliegue de prueba para la
red doméstica. No abrir el puerto 3389 en el router ni a Internet.
Si se decide conservar el acceso remoto, agregar filtrado de origen
o encapsular RDP en el túnel SSH ya existente.

## Surface RT (postmarketOS ARMv7, XFCE4)

- Cliente `freerdp2` versión 2.11.7, instalado desde los repositorios
  Alpine compatibles con ARMv7. El instalador `apk` devolvió un
  error de mantenimiento en la configuración general del sistema,
  pero los dos paquetes `freerdp2`/`freerdp2-libs` aparecen
  instalados y `xfreerdp` arranca correctamente.
- Alias de resolución `t160 → 192.168.100.7` añadido a
  `/etc/hosts`; respaldo previo
  `/etc/hosts.surf-ai-before-t160`.
- Certificado TLS guardado localmente mediante **TOFU**
  en `~/.config/freerdp/known_hosts2`; evita confiar en
  certificados distintos en conexiones posteriores.
- Comando de entrada:
  `~/.local/bin/surf-codex-rdp`, ejecutable.
- Lanzador gráfico:
  `~/.local/share/applications/surf-codex-rdp.desktop`.
- Resolución inicial 1280×700, profundidad 16 bpp, compresión,
  sin efectos de papel tapiz ni animaciones, con portapapeles habilitado.
  La contraseña se solicita en la terminal con
  `/from-stdin:force`: **no** aparece en el listado de procesos.

## Cómo efectuar la primera conexión

1. En la **T160**, abrir una terminal local y consultar el archivo
   protegido: `cat ~/.config/surf-ai/rdp-credentials.txt`.
   No copiar esa contraseña a ChatGPT.
2. En la **Surf**, guardar cualquier borrador de Surf AI, cerrar
   esa aplicación y abrirla otra vez desde el menú.
3. Pulsar **Codex** en Surf AI, o iniciar directamente
   **Codex — escritorio T160** desde el menú Internet.
4. En la ventana de terminal que aparece, introducir la contraseña
   RDP de la T160. La aplicación RDP abrirá el escritorio GNOME
   después de autenticar.
5. Una vez en el escritorio remoto, abrir una terminal **de T160**
   y consultar `codex login status`. Se comprobó previamente que
   devuelve «Logged in using ChatGPT». Desde ahí se puede iniciar
   Codex CLI manualmente con `codex`, cuando el usuario decida
   realmente consumir cuota del plan.

## Pruebas ejecutadas

- T160: servicio RDP **active / enabled**, TLS activo, puerto 3389,
  autenticación con credenciales, control remoto permitido.
- Surf: cliente FreeRDP instalado, ejecutable y bibliotecas presentes;
  conexión TCP con T160 **PASS**.
- Surf: negociación TLS con certificado autofirmado aceptado mediante
  TOFU; autenticación NLA con contraseña falsa rechazada
  con `STATUS_LOGON_FAILURE`, comportamiento esperado.
- Surf: acceso directo validado con `desktop-file-validate`,
  y script con `sh -n`.
- Surf: recompilación nativa ARMv7 de Surf AI **PASS**,
  sin advertencias ni dependencias ausentes. Arranque GTK3
  **PASS**, con bandas 260 px / 1085 px.
- Instalado el binario con SHA256
  `7222e0f890d7853ef84b89446db79a73219526f831841101e2020d5a096fca6a`.
- Respaldo del binario anterior:
  `~/Projects/SurfAI/backups/surf-ai-before-codex-remote-20261118`.
- **No se ha verificado aún una sesión RDP autenticada con la contraseña
  real, ni se ha lanzado una consulta real de Codex**. Esa comprobación
  requiere intervención del usuario en la pantalla de Surf.
- El servicio GPT de T160 se mantuvo activo sin cambios.
- El adaptador experimental `t160/codex_adapter.py` quedó en el
  código fuente, **no habilitado en el proceso de puente actual**.
  La opción `Codex` de Surf abre ahora el escritorio remoto,
  no ese adaptador.

## Créditos y costos

**RDP, SSH y FreeRDP no utilizan tokens ni créditos de ChatGPT**;
son comunicaciones entre dispositivos propios.

**Codex CLI**, una vez iniciado y ante una petición, consume la
asignación incluida de Codex/ChatGPT o créditos disponibles,
según los límites y permisos de la cuenta. No se configuró ninguna
clave API de pago. La cuenta de Codex CLI mostraba
`Logged in using ChatGPT`.

Para evitar créditos adicionales revisar
**ChatGPT → Settings → Usage**:
mantener deshabilitada la autorización de que aplicaciones usen
créditos tras agotar la cuota y no habilitar compras automáticas.
La conexión RDP no puede verificar ni modificar estos permisos.

Documentación de OpenAI:
- https://help.openai.com/en/articles/11369540-using-codex-with-your-chatgpt-plan
- https://help.openai.com/en/articles/20001542-using-your-chatgpt-plan-in-other-apps-and-sites

## Desactivación y recuperación

Para desactivar temporalmente el servidor RDP desde T160:

```sh
systemctl --user stop gnome-remote-desktop.service
```

El servicio original de Surf AI (GPT) y su túnel SSH no se afectan.
Para retirar permanentemente la capacidad RDP se pueden desactivar
el servicio GNOME y el backend con `grdctl rdp disable`, además
de borrar el acceso de la Surf, sólo previa autorización del usuario.
