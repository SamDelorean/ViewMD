# Surf AI v0.2 — panel de conversaciones y pruebas (2026-11-18)

## Comportamiento del panel lateral

- Panel izquierdo plegable con botón superior.
- Orden descendente por actividad: conversación más reciente arriba, más antigua abajo.
- Primera consulta de **hasta 20 filas**, limitadas a ID, título y marca de
  actualización. Sin descargar cuerpos de mensajes.
- Botón "Cargar 20 más": paginación keyset por (updated,id), también descendente.
- Seleccionar fila: recupera **máximo cinco mensajes** completos para el panel.
- Botones "Mensajes anteriores" y "Volver a recientes" cambian la ventana visual.
- Menú contextual al hacer **clic derecho** o una **pulsación prolongada**
  sobre una conversación; sin botones de Renombrar/Eliminar permanentes.
- "Renombrar": cuadro editable, máximo 100 caracteres.
- "Eliminar": cuadro modal de confirmación; una vez aceptado se borran la
  conversación y sus mensajes de la base SQLite de Surf AI en T160.
- Abrir el menú no descarga mensajes ni abre el historial de la conversación.
- La T160 conserva contexto del proveedor y conversaciones en SQLite, mientras
  la Surface RT sólo mantiene la ventana gráfica necesaria.
- Las conversaciones originales de ChatGPT web y Claude web no son
  automáticamente accesibles: este listado contiene sesiones propias de Surf AI.

## Arquitectura y restricciones

- Cliente: C, GTK3 y renderizador ViewMD nativo para ARMv7/postmarketOS.
- Servidor: Python 3, protocolo local dentro de túnel SSH y SQLite.
- T160 escucha sólo en localhost para el puente; no exponer el puerto.
- Codex y Claude Code son por ahora botones de interfaz sin agentes ejecutables.
- Sin invocar Codex, sin supervisión continua ni automatización de acciones.
- Compilación exclusivamente en Surf con nice y make -j1, no en T160.
- No cambiar kernel, arranque, controladores ni Wi-Fi.

## Validación acreditada

- 7/7 tests automatizados en T160: PASS (incluye paginación, orden,
  historial, rename y delete).
- Surf: nueva versión nativa ARMv7 compilada correctamente (build inicial v0.2).
- Prueba real Surf -> túnel SSH -> T160: PASS para ASK, HISTORY, LIST,
  RENAME y DELETE; se borró la conversación temporal de prueba.
- Smoke test de ventana v0.2: inició pero detectó una advertencia GTK por
  actualización temprana de la etiqueta de estado.
- Corrección de esa advertencia incorporada en commit 268a2e21.
- Corrección GTK compilada nuevamente en la Surf: **BUILD_PASS**.
- Smoke test final de la ventana corregida: **GUI_SMOKE_PASS**, cierre
  programado por timeout (143), sin Gtk-CRITICAL ni bibliotecas faltantes.
- La verificación funcional completa con teclado y tacto permanece pendiente.

## Archivos de diagnóstico

- Surf compilación: ~/Projects/SurfAI/surf-build-v02.log
- Surf smoke inicial: ~/Projects/SurfAI/surf-gui-v02.log
- Surf última compilación: ~/Projects/SurfAI/surf-build-final.log
- Surf último smoke: ~/Projects/SurfAI/surf-gui-final.log

## Pendiente

La lista de conversaciones se crea sobre el catálogo propio y no sincroniza
chats web existentes. Falta integrar la ejecución real de Codex, Claude Code,
herramientas y adjuntos. La prueba de clic/táctil y captura visual del panel
en la Surface RT sigue pendiente; el smoke sólo confirma el arranque.

## Recompilación e instalación posterior (2026-11-18)

- Surf: actualización fast-forward de ccf697c a 4d97ed3.
- Compilación nativa ARMv7 con nice -n 10 y make -C surf -j1: **BUILD_PASS**.
- Binario: /home/user/Projects/SurfAI/ViewMD/surf/surf-ai (ELF 32-bit ARM EABI5, musl).
- Dependencias dinámicas: **LINK_PASS**; sin bibliotecas faltantes.
- Advertencias de compilación: ninguna.
- Apertura GTK3 en DISPLAY=:0, limitada a 8 segundos:
  **GUI_SMOKE_PASS**; cierre por timeout, sin errores críticos.
- Lanzador XFCE4 existente: Exec apunta al ejecutable actualizado.
- Logs locales:
  /home/user/Projects/SurfAI/surf-build-context-menu.log y
  /home/user/Projects/SurfAI/surf-gui-context-menu.log.
- Incluye fila izquierda compacta, menú de conversación mediante
  clic derecho/pulsación prolongada, y controles de historial apilados
  junto al campo de texto.
- No se usó Codex; Commander se utilizó sólo bajo autorización para
  comprobar y compilar en la Surface RT. Sin supervisión continua.
- Pendiente prueba manual de interacción con ratón y tacto; una
  apertura sin errores no valida el comportamiento de los gestos.

## Icono y acceso directo 2026-11-18

- Diseño seleccionado por el usuario: icono Surf AI minimalista con letras AI,
  tabla de surf, ola y sol. PNG de 128x128 colores indexados, copia
  `surf/icons/surf-ai.png` en GitHub.
- Instalado en Surf como
  `~/.local/share/icons/hicolor/128x128/apps/surf-ai.png`;
  SHA-256: `67c1cb5d79931d6eeecf5693f865d5c6b1fb7a1704db521ea0365d11f6dead37`.
- Acceso directo `~/.local/share/applications/surf-ai.desktop`:
  `Icon=surf-ai`, `Categories=Network;` (Internet, no Accesorios).
- Whisker Menu: entrada `surf-ai.desktop` en `/plugins/plugin-1/favorites`.
  Se conservaron los cuatro favoritos predeterminados.
- Ejecutable GTK3 actualizado con `gtk_window_set_icon_name`.
- Compilación nativa ARMv7 con `nice -n 10 make -j1`: PASS.
- Apertura con `DISPLAY=:0 timeout 7`: GUI_SMOKE_PASS,
  finalización controlada, sin errores críticos observados.
- Recarga de `xfce4-panel --restart`: PASS, con bus de sesión correcto.
- Commit de cliente instalado: `27afd59`; rama `surf-ai-client`.
- Respaldo del lanzador previo:
  `~/.config/surf-ai-backups/surf-ai.desktop.before-icon-and-favorites`.
- Respaldo de configuración del panel previo:
  `~/.config/surf-ai-backups/xfce4-panel.xml.before-surf-ai-favorite`.
- Logs: `~/Projects/SurfAI/surf-icon-build.log` y
  `~/Projects/SurfAI/surf-icon-smoke.log`.
