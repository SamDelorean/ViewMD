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
| Pulsación prolongada | GtkGestureLongPress, menú Renombrar / Eliminar |
| Borrado | Diálogo modal con confirmación explícita |
| Pinza de dos dedos | GtkGestureZoom y CSS para fuente de 11 a 24 pt |
| Teclado virtual | Teclado GTK3 integrado, oculto al inicio |
| Idioma y símbolos | Números, letras, ñ, áéíóúü, puntuación y símbolos de código |
| Teclas especiales | Mayús, Espacio, Borrar, Intro |
| Botones | Altura táctil aproximada de 44–48 px |
| Teclado/ratón | Flechas, RePág/AvPág, Ctrl+Enter, clics, sin cambios |

El teclado integrado aparece sólo al pulsar el botón "⌨ Teclado".
No depende de Onboard, no ejecuta procesos externos y no necesita
privilegios. No reemplaza un teclado completo del sistema operativo.

La pulsación prolongada sólo abre el menú; no modifica ni elimina nada
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
4. Mantener pulsado un título y abrir Renombrar/Eliminar; cancelar borrado.
5. Probar pinza de dos dedos y comprobar cambio de fuente sin recargar mensajes.
6. Mostrar el teclado, introducir caracteres españoles y borrar texto.
7. Probar teclado físico, flechas, RePág/AvPág, Ctrl+Enter y ratón.
8. Revisar distribución a 1366×768 y que el teclado no oculte la entrada.
9. Verificar fluidez y RAM con ventana de cinco mensajes y lista de 20.

## Limitaciones y restricciones

- La pantalla de Surface RT soporta toque, pero todavía no está
  acreditada la recepción de sus eventos bajo esta instalación concreta.
- El comportamiento de pinza y pulsación prolongada necesita prueba física.
- El teclado integrado es básico, sin predicción ni autocorrección.
- Cambios incorporados a la rama surf-ai-client en GitHub.
- NO compilados ni instalados en Surf en esta revisión: compilar sólo en Surf
  tras autorización, con nice y make -j1. No compilar en T160.
- No se usó Codex ni Remote Desktop Commander para la revisión.
- No se tocó kernel, arranque, firmware, DTB ni drivers.

## Compilación futura autorizada (exclusivamente en Surf)

Desde el repositorio limpio:

    git fetch origin surf-ai-client
    git merge --ff-only origin/surf-ai-client
    nice -n 10 make -C surf -j1

Abrir desde XFCE para validar el comportamiento táctil manualmente.
No activar servicios, supervisión continua o autorun.
