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

## Compilación de interfaz y estado del puente (18-11-2026)

- Surf: revisión `1d180dc` (rama `surf-ai-client`) instalada por fast-forward.
- Nuevas funciones: botón «⋯» por conversación; menú contextual preciso,
  título de ventana `Surf AI - <conversación> - <herramienta>`,
  vista vacía al iniciar una conversación y estado inferior oculto en reposo.
- Compilación nativa ARMv7: `nice -n 10 make -C surf -j1` **PASS**.
- `file`: ELF ARM EABI5 de 32 bits, musl; dependencias dinámicas **PASS**.
- Compilación sin advertencias ni errores registrados.
- Prueba `DISPLAY=:0 timeout 8 ./surf/surf-ai`: **GUI_SMOKE_PASS**;
  cierre controlado por timeout (143), sin errores críticos en el log.
- El lanzador XFCE4 conserva la ruta al binario actualizado.
- Logs en `/home/user/Projects/SurfAI/surf-window-title-build.log` y
  `/home/user/Projects/SurfAI/surf-window-title-smoke.log`.
- T160, solo lectura: proceso `python3 t160/bridge.py --mock`
  escuchando en `127.0.0.1:8765`, SSH limitado a LAN en puerto 2222,
  sin servicio de arranque automático localizado. Claves API no presentes
  en el entorno examinado (esto no prueba ausencia absoluta en disco).
- No se habilitaron ni reiniciaron servicios en T160; sin Codex.
- Commander utilizado con autorización del usuario para compilar/probar
  en Surf y auditoría de solo lectura en T160. Sin supervisión continua.
- Las pruebas manuales de menú «⋯», renombrado, título y tacto están
  pendientes: el smoke test verifica arranque, no interacción humana.

## Reparación de proporciones de paneles (18-11-2026)

- Problema observado en pantalla 1366x768: la banda izquierda
  reclamaba aproximadamente 634 px pese a que sus controles ocupaban 252 px.
  El área principal comenzaba demasiado a la derecha.
- Cambio: GtkBox horizontal exterior sustituido por GtkPaned horizontal.
  Panel izquierdo con posición inicial 260 px y divisor regulable; el
  panel derecho absorbe el ancho restante. Al ocultar el panel izquierdo
  se conserva un botón de menú de 52 px.
- Contenido del listado: ancho natural no propagado por GtkScrolledWindow;
  títulos elididos con ancho mínimo reducido para no forzar expansión.
- Compilación ARMv7 en Surf: nice -n 10 make -C surf -j1 -> BUILD_PASS,
  sin advertencias ni errores registrados. Prueba GTK con cierre por timeout
  sin fallos críticos.
- Auditoría optativa de una sola medición: SURF_AI_LAYOUT_AUDIT=1.
  **Resultado real en GTK3:** split=1350 px, navigation=260 px,
  sidebar=260 px, list=260 px, content=1085 px, divider=260 px.
  Porcentajes aproximados: 19.3 % navegación, 80.4 % contenido;
  el resto corresponde al divisor.
- Commit probado e instalado en Surf: 6c5d49a.
- Archivos locales:
  ~/Projects/SurfAI/surf-layout-build.log
  ~/Projects/SurfAI/surf-layout-smoke.log
  ~/Projects/SurfAI/surf-layout-audit-build.log
  ~/Projects/SurfAI/surf-layout-allocation.log
- La captura gráfica posterior resultó negra debido a la sesión protegida
  por xfce4-screensaver. No se desbloqueó ni deshabilitó la protección;
  se validó la geometría con los tamaños reales asignados por GTK3.
- Sin modificaciones a T160, SSH, sesiones, kernel ni controladores;
  sin Codex ni supervisión continua.
