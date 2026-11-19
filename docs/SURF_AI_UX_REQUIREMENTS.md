# Surf AI — Especificación de experiencia de usuario y continuidad

Fecha: 2026-11-18. Estado: **requisitos definidos; sin implementación completa**.

## Codex: proveedor de chat nativo (18-11-2026)

**Contrato de interfaz vigente:** el botón **Codex** selecciona
conversaciones del proveedor `codex` dentro de Surf AI, de manera
independiente de `openai` (GPT) y `anthropic` (Claude). El envío
`ASK` llega mediante SSH al puente T160 y éste utiliza Codex App
Server y el token autorizado de ChatGPT. Las respuestas se muestran
en el mismo panel GTK3 mediante streaming `DELTA`, con
`HISTORY`, `LIST` y `RELOAD` disponibles.

**Prohibido:** sustituir este botón por escritorio remoto, ejecutar
agentes, herramientas, comandos o cambios de archivos sin una nueva
autorización expresa. La etapa actual es conversación de código en
modo read-only y sin herramientas, con cuota de ChatGPT y sin API key.

- Cliente GTK3 ARMv7: compilado e instalado en Surface RT.
- T160: `SURF_CODEX_TEXT_ONLY=1` y `SURF_CODEX_MODEL=gpt-6.2-sol`.
- Prueba real Surf → T160 → Codex: aprobada; también la
  continuidad y actualización de conversación.
- Al instalar se conservó abierta la instancia anterior de Surf AI
  para no perder un borrador: cerrar y reabrir aplica el nuevo binario.
- Referencia autorizada: `docs/SURF_CODEX_NATIVE.md`.
- El experimento RDP anterior se cerró y su servidor quedó
  deshabilitado (puerto 3389 cerrado).

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

### Menú contextual de conversaciones (ajuste posterior)

Se eliminan los botones fijos «Renombrar» y «Eliminar» del pie de la
banda izquierda. Sobre cualquier título de conversación:

- **Ratón:** clic derecho abre un menú contextual con Renombrar y Eliminar
- **Pantalla táctil:** pulsación prolongada abre el mismo menú
- **Clic izquierdo o toque breve:** selecciona y abre la conversación

Abrir el menú contextual no descarga el historial de la conversación.
Renombrar conserva el diálogo de edición y Eliminar mantiene la
confirmación explícita antes de borrar. La lista mantiene 20 títulos
por página, ordenados por actividad más reciente.

Cambio incorporado a GitHub; todavía no recompilado ni instalado en la
Surface RT. No autoriza uso adicional de Commander o Codex.

### Título dinámico de ventana y recuperación de altura (ajuste posterior)

El panel derecho de conversación ya no presenta el texto de bienvenida
«Nueva conversación» ni la leyenda permanente de estado al pie.
Cuando la conversación aún está vacía, el visor de mensajes queda
realmente vacío. El estado del cliente permanece oculto cuando está
inactivo; sólo aparece para consultas en curso, fallos o avisos
relevantes.

El título nativo de la ventana GTK3 sigue el patrón exacto:

`Surf AI - <nombre de conversación> - <herramienta>`

Ejemplo: `Surf AI - nueva conversación - GPT`.

- Al pulsar «Nueva conversación», se muestra el título anterior y
  se limpia el visor sin consumir una fila adicional.
- Al seleccionar un título de la lista, el título de ventana refleja
  esa conversación sin alterar el historial.
- Al renombrarla mediante «⋯», se actualiza después de confirmar
  que el servidor aceptó el nuevo nombre.
- Tras el primer mensaje, se muestra un título provisional breve y
  se sincroniza después con el título canónico de la T160.
- Al cambiar GPT, Codex, Claude o Claude Code, cambia la herramienta
  del título; como cada proveedor tiene un catálogo separado, el
  visor pasa a «nueva conversación» hasta seleccionar una sesión
  existente en el modo elegido. Esto no elimina el historial previo.

Los errores y mensajes de trabajo continúan disponibles de forma
temporal, sin reservar un renglón cuando la aplicación está inactiva.
No se cambia el botón «Enviar», la barra lateral ni la paginación.

**Estado:** cambios guardados en GitHub; sin recompilar ni instalar
esta revisión en la Surf. El acceso remoto/compilación sigue requiriendo
autorización específica. No se utilizó Codex ni Commander.

### Menú «⋯» circular como semáforo y recarga (ajuste actual)

El botón discreto «⋯» de cada conversación pasa a ser un **círculo
de 38 px**. Su propio fondo indica el estado: **gris** en reposo o
consultada; **amarillo oscilante** mientras la T160 procesa; **verde**
para una respuesta nueva completa; **rojo** ante un fallo. No se añade
ningún indicador separado ni se pierde ancho del título.

El botón conserva el menú contextual, ahora con tres acciones:
**Actualizar, Renombrar, Eliminar**. Actualizar fuerza la lectura de la
conversación persistida en la T160 y reemplaza la ventana de cinco
mensajes en la Surf. **No repite el mensaje a GPT ni Claude** y no
sincroniza conversaciones alojadas en las páginas web de los proveedores.

Las actualizaciones de estado se obtienen con las operaciones del
usuario y al terminar una respuesta. La animación amarilla usa únicamente
un temporizador GTK local, sin supervisión continua ni consultas
periódicas a la T160.

Cuando otro cliente de Surf AI trabaja sobre la **misma conversación
de T160**, pulsar Actualizar trae sus mensajes nuevos y el título
actual. La siguiente consulta utiliza el contexto reciente desde SQLite,
sin reenvío del mensaje anterior. Esto no equivale a sincronizar
automáticamente los chats alojados en la web de los proveedores.

Contrato técnico, estados, compatibilidad y límites en
`docs/SURF_AI_RELOAD_STATUS.md`.

**Estado:** rama `surf-ai-client` actualizada; pruebas añadidas
pero todavía sin ejecutar en las máquinas. Pendiente de instalar el
nuevo bridge en T160 y recompilar el cliente nativo en Surf con
autorización expresa de Commander. No se ha usado Codex.

### Botón «⋯» por conversación (ajuste táctil)

El toque prolongado no respondió de manera fiable en la Surf.
**Control principal:** cada fila del listado muestra un botón discreto
«⋯» inmediatamente después del título, alineado al extremo derecho.
Está integrado en la misma fila y no añade renglones ni descarga mensajes.

Al pulsar «⋯» se abre el menú contextual **Renombrar / Eliminar** de
esa conversación exacta. El destino de las acciones se guarda en la fila
que originó el menú: **no depende de la fila seleccionada** y abrir el
menú no recupera ni abre el historial. La eliminación sigue requiriendo
confirmación explícita y los errores del servidor no se confunden con
una eliminación exitosa.

El clic derecho permanece como alternativa. El gesto de pulsación
prolongada se conserva como opción experimental, pero no es requisito
para acceder a las acciones.

El catálogo sigue limitado a 20 títulos por página y ordenado por
actividad reciente descendente. No se cambió la T160, ni la entrada de
texto ni los botones de navegación de mensajes.

**Estado:** código y documento actualizados en GitHub; **pendiente
compilación ARMv7 e instalación en Surf**, que necesitan autorización
explícita para utilizar Commander. No se usó Codex ni Commander en esta
revisión.

### Controles inferiores compactos (ajuste posterior)

La fila independiente de «Mensajes anteriores» y «Volver a recientes»
queda eliminada. Esas dos acciones se colocan **una encima de la otra**
en una columna vertical junto al campo de texto, exactamente en el
espacio que antes ocupaba el botón de teclado interno; cada control
conserva una superficie horizontal y su función de paginación.
**Enviar permanece a la derecha, sin cambios.** Esto recupera una
fila de altura para el visor de conversación.

Cambio guardado en la rama `surf-ai-client`; pendiente de compilación
y prueba en ARMv7. El ajuste no autoriza un nuevo acceso remoto al equipo.

**Estado:** esta distribución está incorporada a la rama de GitHub
pero NO se ha recompilado ni instalado todavía en la Surf. No se
autorizó Commander para compilar esta revisión.

## Selector de modelos y herramientas

Los cuatro selectores **GPT**, **Codex**, **Claude** y **Claude Code**
están ahora en las dos primeras filas de la banda izquierda, no en una
barra superior independiente. Claude Code es el agente de programación
de Anthropic; Claude identifica el chat general.

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
