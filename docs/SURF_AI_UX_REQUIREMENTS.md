# Surf AI — Especificación de experiencia de usuario y continuidad

Fecha: 2026-11-18. Estado: **requisitos definidos; sin implementación completa**.

## Objetivo principal

Cliente nativo GTK3 de Surface RT con experiencia de trabajo lo más próxima
posible a ChatGPT, Codex y Claude, sin navegador pesado en Surface.
T160 mantiene sesiones, herramientas, modelos y archivos. La continuidad real
de proyectos y conversaciones tiene prioridad sobre el simple chat.

## Distribución compacta aprobada (2026-11-18)

La barra superior independiente queda **eliminada** para ampliar la
conversación hasta el borde superior del área principal.

La banda izquierda contiene, en orden vertical:

- **Fila 1:** ☰ Menú | GPT | Codex
- **Fila 2:** Claude | Claude Code
- **Fila 3:** + Nueva conversación
- **Debajo:** lista de conversaciones, sin leyenda de encabezado.

Se retiran de la superficie gráfica las leyendas «Surf AI» y
«Conversaciones · recientes primero». El orden interno del listado
continúa siendo por actividad reciente descendente y sólo carga
20 títulos por página desde T160.

Al plegar la banda izquierda permanece **un botón ☰ compacto**, fuera
del panel oculto, para volver a desplegarla. El área de conversación
ocupa la altura completa disponible, con entrada de texto abajo y
desplazamiento por barra, teclado y tacto.

La Surf utiliza su teclado de pantalla ya instalado (Onboard);
el botón y teclado propios de Surf AI se retiraron del código.

**Estado:** esta distribución está incorporada a la rama de GitHub
pero NO se ha recompilado ni instalado todavía en la Surf. No se
autorizó Commander para compilar esta revisión.

## Navegación superior (decisión del usuario)

La barra superior contiene cuatro botones independientes, visibles en todo
momento: **GPT**, **Codex**, **Claude** y **Claude Code**. Claude Code es el
agente de programación oficial de Anthropic; Claude identifica el chat general.

Cambiar pestañas cambia el contexto de interfaz pero **no ejecuta** herramientas.
Codex y Claude Code tienen sus propios paneles de tareas, archivos, diffs,
aprobaciones, terminal/logs y reanudación de trabajos, por implementar.

El panel izquierdo se despliega o se oculta con el botón de menú. El área
principal conserva la caja de entrada abajo y navegación vertical por barra
de desplazamiento o teclas Arriba/Abajo y RePág/AvPág.

Estado: estructura GTK3 incorporada en fuentes. La lista remota de
conversaciones, herramientas y su ejecución NO están conectadas todavía.
La integración de agentes sigue requiriendo aprobación explícita.

## Pantalla principal

1. Panel lateral de conversaciones y proyectos: buscar, seleccionar, retomar,
   renombrar y archivar.
2. Selector visible de producto: GPT, Codex y Claude; distinguir chat de
   ejecución de agente.
3. Zona central de conversación con Markdown, tablas, código, imágenes,
   enlaces y documentos. Mostrar 3–5 mensajes como ventana dinámica.
4. Historial anterior recuperable por paginación desde la T160; no mezclar
   ventana visual con contexto de modelo o almacenamiento del servidor.
5. Panel de tareas, herramientas y resultados: ejecución, diff, archivos,
   permisos, estado, progreso, errores.
6. Compositor de consultas con adjuntos y referencias a archivos/proyectos.

## Sesiones y recuperación

- Identidad estable por proyecto, conversación, proveedor y backend.
- Guardar metadatos, estado y cursores en T160, no en Surf.
- El backend debe permitir páginas de mensajes, búsqueda y restauración.
- El contexto del modelo, el contexto de trabajo y la ventana visual son
  tres conceptos distintos.
- La navegación por conversaciones NO debe cambiar silenciosamente el
  directorio de trabajo ni autorizar comandos.
- Reusar Git y documentos de los proyectos AlphaSmart, DANA y Surf AI
  sólo mediante fuentes y permisos expresamente disponibles.
- Implementar un adaptador ChatGPT web únicamente si existe una vía
  autorizada; nunca presentar una copia como sesión web original.

## Integraciones

- OpenAI API: conversaciones y respuestas propias de nuestro backend.
- Codex App Server: hilos, turnos, eventos de herramientas, aprobaciones y
  reanudación; sujeto a disponibilidad y autenticación admitida.
- Claude Agent SDK: sesiones y controles de herramientas/permisos.
- Claude API: chat del backend, distinto del historial de claude.ai.
- Modo opcional de visualización remota de un navegador en T160 para
  fidelidad a la interfaz web original, pendiente de prueba de rendimiento.

## Seguridad y restricciones operativas

- No ejecutar Codex, Claude Code, compilaciones, instalaciones ni comandos
  por seleccionar una pestaña o abrir un proyecto.
- Respetar autorización expresa para Codex y Remote Desktop Commander.
- No activar aprobación automática ni mecanismos de supervisión continua.
- Las acciones sensibles se presentan con descripción y solicitud de permiso.
- Evitar almacenar claves API, secretos o credenciales web en Surface.
- No modificar kernel, DTB, firmware ni la conexión Wi-Fi USB de Surf.
- Compilar cliente sólo en Surf, con make -j1, nice y scripts sh.

## Secuencia de implementación

Gate UX-0: inventario técnico y navegación GTK3 (sin herramientas).
Gate UX-1: 5 mensajes máximo + paginación verificable desde T160.
Gate UX-2: sesiones y proyectos persistentes en T160; cambio de sesión.
Gate UX-3: panel de archivos/diffs y eventos simulados de herramientas.
Gate UX-4: adaptador Codex App Server y aprobaciones explícitas.
Gate UX-5: adaptador Claude Agent SDK y reanudación.
Gate UX-6: modo de compatibilidad web original si es viable.
Gate UX-7: prueba real de ergonomía y rendimiento ARMv7.

## Estado de implementación al definir requisitos

La compilación y la prueba de GTK3, SSH y respuestas mock ya pasaron.
El cliente inicial solo tiene un selector simple GPT/Claude.
El puente T160 dispone en el repositorio de una extensión experimental
para consultar páginas de historial, pero su integración cliente-servidor,
los tests actualizados y la persistencia aún están pendientes.
NO afirmar paridad de funciones con ChatGPT/Codex/Claude web.
