# Surf AI — Integración táctil GTK3 (2026-11-18)

## Objetivo

Uso híbrido en Surface RT con postmarketOS v26.06 / XFCE4:
pantalla táctil, ratón y teclado físico. Se mantiene la apariencia de
escritorio: barra superior GPT/Codex/Claude/Claude Code, barra lateral
plegable y conversación principal a la derecha.

## Funciones incorporadas en fuentes

| Acción | Implementación |
|---|---|
| Toque sencillo | GtkButton, GtkListBoxRow, GtkTextView |
| Deslizamiento | GtkScrolledWindow con kinetic-scrolling en lista e historial |
| Barra visible | overlay-scrolling desactivado en ambas zonas |
| Clic derecho y pulsación prolongada | Menú contextual compartido con Renombrar / Eliminar |
| Borrado | Diálogo modal con confirmación explícita |
| Pinza de dos dedos | GtkGestureZoom y CSS para fuente de 11 a 24 pt |
| Entrada de texto | Campo GTK3 compatible con teclado físico y teclado en pantalla de postmarketOS/XFCE4 |
| Botones | Altura táctil aproximada de 44–48 px |
| Teclado/ratón | Flechas, RePág/AvPág, Ctrl+Enter, clics, sin cambios |

El botón "⌨ Teclado" y el teclado integrado de Surf AI se eliminaron
del código del cliente. La entrada utiliza el teclado de pantalla propio
de la Surf (Onboard está instalado). No se modifica su configuración;
se conserva el campo GTK3 estándar para recibir el texto.

El clic derecho o la pulsación prolongada sólo abren el menú; no modifican ni eliminan nada
sin una acción adicional del usuario. La eliminación requiere confirmación.

## Auditoría táctil (solo lectura)

Ejecutar desde la raíz del repositorio en Surf:

    sh scripts/surf-touch-audit.sh

El script no usa sudo, no escribe archivos ni altera los controladores.
Comprueba dispositivos de entrada y xinput si ya estuviera instalado.

## Pruebas físicas pendientes

1. Confirmar que el kernel y Xorg/libinput reporten eventos táctiles.
2. Probar toque simple en los cuatro botones superiores.
3. Arrastrar verticalmente con un dedo en historial y lista de conversaciones.
4. Probar clic derecho y pulsación prolongada sobre un título; abrir
   Renombrar/Eliminar sin cargar mensajes, y cancelar el borrado.
5. Probar pinza de dos dedos y comprobar cambio de fuente sin recargar mensajes.
6. Enfocar el cuadro de entrada y utilizar el teclado en pantalla del sistema para escribir caracteres españoles y borrar texto.
7. Probar teclado físico, flechas, RePág/AvPág, Ctrl+Enter y ratón.
8. Revisar distribución a 1366×768 y que el teclado no oculte la entrada.
9. Verificar fluidez y RAM con ventana de cinco mensajes y lista de 20.

## Limitaciones y restricciones

- Auditoría en hardware completada: /dev/input/event9 se identifica como
  ID_INPUT_TOUCHSCREEN=1 (Atmel hid-over-i2c 03EB:8207), área 227 x 132 mm.
  Su presencia está confirmada; el reconocimiento de gestos requiere prueba manual.
- El comportamiento de pinza y pulsación prolongada necesita prueba física.
- La funcionalidad del teclado en pantalla depende de su configuración en postmarketOS/XFCE4; Surf AI ya no incorpora teclado propio.
- Cambios incorporados a la rama surf-ai-client en GitHub.
- **Compilación e instalación validadas en Surf**, exclusivamente con
  nice -n 10 y make -j1, sin compilar en T160.
- Commit instalado y probado: ccf697c (rama surf-ai-client).
- Primera compilación: error por gtk_gesture_get_widget, resuelto
  sustituyéndolo por gtk_event_controller_get_widget para GTK3.
- Segunda compilación: BUILD_PASS, ELF ARM 32-bit musl, 0 warnings/errors.
- Apertura gráfica en XFCE4, DISPLAY=:0 durante 8 s: GUI_SMOKE_PASS
  (cierre programado por timeout), sin Gtk-CRITICAL ni GLib-CRITICAL.
- El lanzador XFCE4 existente apunta al binario actualizado en
  /home/user/Projects/SurfAI/ViewMD/surf/surf-ai.
- Logs: ~/Projects/SurfAI/surf-touch-build2.log y surf-touch-smoke2.log.
- La inspección táctil confirmó udev ID_INPUT_TOUCHSCREEN=1; no se
  ha simulado o medido contacto físico real. Onboard está instalado
  en la Surf y será el único teclado virtual previsto.
- La revisión de fuentes no utilizó Codex ni Commander. La compilación y
  las auditorías de esta sesión usaron Commander con autorización puntual.
- No se tocó kernel, arranque, firmware, DTB ni drivers.

## Compilación futura autorizada (exclusivamente en Surf)

Desde el repositorio limpio:

    git fetch origin surf-ai-client
    git merge --ff-only origin/surf-ai-client
    nice -n 10 make -C surf -j1

Abrir desde XFCE para validar el comportamiento táctil manualmente.
No activar servicios, supervisión continua o autorun.

## Estado de validación actualizado (2026-11-18)

- Repositorio master intacto; sólo la rama surf-ai-client cambió.
- No se alteraron kernel, controladores, servicios, Device Tree ni particiones.
- Pendiente validación manual de toque, deslizamiento cinético, pinza y
  menú de pulsación prolongada; una prueba de arranque no los acredita.

## Ajuste de interfaz posterior

- Eliminados del código fuente el botón «⌨ Teclado», el panel de teclado GTK3
  y sus funciones de manejo.
- Se preservan los gestos táctiles, el zoom, la barra lateral, la entrada
  de texto y el botón «Enviar».
- Este cambio de fuente está en GitHub. **La compilación e instalación
  anteriores de ccf697c no incluyen todavía esta eliminación.**
- Para sustituir el binario instalado en la Surf será necesaria una nueva
  compilación ARMv7 con los permisos operativos correspondientes.
