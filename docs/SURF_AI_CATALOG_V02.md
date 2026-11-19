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
