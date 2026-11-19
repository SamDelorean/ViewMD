# Surf AI — Actualización de conversación y semáforo integrado (18-11-2026)

## Interfaz

Cada conversación conserva un solo control de **38 × 38 px**: un
botón **circular** con «⋯» en el centro. El **fondo del mismo botón**
indica el estado; no hay una columna de indicadores adicional.

| Fondo | Estado SQLite | Significado |
|---|---|---|
| Gris | idle | Sin actividad o respuesta ya consultada |
| Amarillo, alternando tonos | working | La T160 está procesando una respuesta |
| Verde | ready | Terminó una respuesta y aún no se ha vuelto a consultar |
| Rojo | error | La última operación de la conversación falló |

La alternancia amarilla se realiza con un temporizador **local GTK**
de 650 ms, exclusivamente mientras la lista muestra alguna respuesta
en curso. **No hay supervisión de procesos, monitoreo remoto ni consultas
periódicas a la T160.** El estado se consulta únicamente al listar,
seleccionar o actualizar una conversación y al terminar una respuesta
originada desde Surf AI.

El menú «⋯» tiene tres acciones: **Actualizar, Renombrar y Eliminar**.
El menú por clic derecho sigue disponible. El borrado conserva
confirmación obligatoria.

## ¿Qué hace Actualizar?

**Actualizar** genera exactamente una orden
\`RELOAD<TAB>provider<TAB>session<TAB>-<LF>\` por el túnel SSH.

1. La T160 abre una transacción SQLite y lee de nuevo el título,
   estado, fecha de actualización y cinco mensajes más recientes.
2. Si había una respuesta completa sin consultar (verde), la marca
   como vista (gris) en esa misma transacción.
3. Envía \`META\`, hasta cinco \`ITEM\`, \`MORE\` y \`DONE\` usando el mismo
   encapsulado base64 del protocolo original.
4. Surf AI sustituye los mensajes visibles, actualiza el título de
   ventana, controles de paginación y lista de conversaciones.
5. La acción **no** vuelve a enviar \`ASK\`, no consulta OpenAI/Anthropic,
   no realiza un reintento y no causa cargos de API.

Actualizar **no es sincronización con los sitios web de ChatGPT/Claude**.
El catálogo sólo contiene las sesiones propias de Surf AI guardadas
en la T160. Mientras una respuesta está en curso, una recarga desde otra
sesión puede mostrar los mensajes ya guardados junto al estado amarillo,
pero no forzará una segunda ejecución del modelo.

## Continuidad entre múltiples clientes

Cuando dos clientes de Surf AI comparten el **mismo identificador de
conversación, proveedor y base de datos en la T160**, SQLite es la fuente
autoritativa. Si el cliente B continúa esa conversación y el cliente A
pulsa **Actualizar**, A recibe los mensajes más recientes ya almacenados,
el título y el estado actual, sin duplicar mensajes ni volver a ejecutar
la consulta.

La siguiente consulta del cliente A utiliza el contexto recuperado
directamente de SQLite por el puente; **no depende de una copia
desactualizada del visor**. El cliente sólo muestra cinco mensajes
por página, pero los anteriores permanecen almacenados y el puente
incorpora hasta 12 pares recientes al contexto enviado al proveedor.

No se sincronizan por este mecanismo las conversaciones que existan
únicamente en la web oficial de ChatGPT, Codex o Claude. Esa función
requiere autenticación e integración adicionales.

El texto visible del menú es **Actualizar**; la orden interna sigue
llamándose `RELOAD` por compatibilidad, sin modificar el protocolo.

## Lógica del puente T160

- Columna \`conversations.status\` añadida con migración compatible
  en SQLite existente; las conversaciones previas pasan a gris.
- Al comenzar una consulta válida: \`working\`; se impiden peticiones
  simultáneas duplicadas a la misma conversación.
- Tras guardar una respuesta completa: \`ready\`.
- Ante fallo de una petición **que realmente comenzó**: \`error\`.
  Un intento duplicado rechazado no altera la petición que sí trabaja.
- Al abrir el historial explícitamente, o ejecutar \`RELOAD\` sobre una
  respuesta verde: \`idle\`.
- La actualización automática de los últimos cinco mensajes después
  del streaming conserva \`ready\`; el indicador puede seguir verde
  hasta la siguiente consulta explícita.
- Después de reiniciar el bridge, las sesiones que quedaron
  \`working\` pasan a \`error\` (no se presume que la API continúa).

## Seguridad, restricciones y despliegue

No se modifican credenciales, servicios, kernel ni Wi-Fi. En la
T160, al utilizar esta función se actualizarán únicamente los metadatos
de estado en la base SQLite; **no se borran ni reenvían los mensajes**.
No se utiliza Codex ni supervisión continua.

## Instalación y aceptación real — 18 de noviembre de 2026

**Implementado y verificado en T160 y Surface RT**, conservando el modo
`--mock` (sin claves API ni consultas a servicios externos).

### T160
- Base original respaldada mediante la API de copia SQLite en
  `~/Projects/surf-ai/backups/sessions-before-reload-20261118-220134.sqlite3`.
  Comprobación de integridad: `ok`; antes de la migración había tres
  conversaciones y seis mensajes.
- Suite Python de protocolo y persistencia: **13 pruebas PASS**, con
  bases temporales aisladas. Se corrigió un acoplamiento de fixtures que
  inicialmente hacía fallar una prueba sin afectar datos reales.
- Puente reiniciado como **`surf-ai-bridge.service`**, servicio de
  usuario independiente, puerto `127.0.0.1:8765`, opción `--mock`.
  Se configuró sin reinicio automático y **sin habilitar arranque al
  encender el equipo**. No se reinició el servicio remoto de Commander.
- Prueba de integración real: `ASK → LIST ready → RELOAD idle →
  2 ITEM → RELOAD sin duplicados → DELETE`.
- Al terminar, permanecían exactamente **3 conversaciones, 6 mensajes**
  y la integridad de SQLite era correcta.

### Surface RT
- Código `surf-ai-client` sincronizado y compilado **nativamente ARMv7**
  mediante `nice -n 10 make -C surf -j1 TARGET=surf-ai-next`.
- **BUILD PASS**, sin advertencias; dependencias dinámicas completas.
- `SURF_AI_LAYOUT_AUDIT=1`: navegación **260 px** y contenido
  **1085 px**, en ventana de 1366 × 768.
- Prueba real **a través del túnel SSH desde Surf**: nuevo catálogo con
  cuatro campos, solicitud mock, estado `ready`, recarga
  `idle` con dos mensajes, ausencia de duplicación y borrado de
  sesión temporal. **PASS**.
- Despliegue atómico del ejecutable, con respaldo anterior en
  `~/Projects/SurfAI/backups/surf-ai-before-actualizar-20261118`.
  SHA-256 del instalado:
  `2cc9dcd4c4d9e42ab8da74b49aa438987e4f6506bdbe827d508460d1240ad736`.
- Prueba de arranque GTK3 del nuevo ejecutable: **PASS**, sin errores
  críticos registrados. El cierre con señal 143 corresponde al
  `timeout` de la prueba, no a un fallo de aplicación.
- Se preservó la ventana de usuario que ya estaba abierta, para no
  perder texto sin enviar. **Cerrar y reabrir Surf AI es necesario para
  activar en esa ventana el nuevo ejecutable**.
- Prueba manual del botón táctil y colores en pantalla desbloqueada:
  **pendiente**. Las pruebas de protocolo y el arranque gráfico sí pasaron.
- Logs locales:
  `~/Projects/surf-ai/reload-protocol-tests.log` (T160);
  `~/Projects/SurfAI/reload-status-build.log`,
  `reload-status-smoke.log`,
  `reload-status-installed-smoke.log` (Surf).

No se habilitaron APIs reales, no se ejecutó Codex, no se modificaron
sistema, kernel, Wi-Fi ni conversaciones ajenas. No se configuró
supervisión continua ni sondeo remoto periódico.

Pruebas añadidas en \`tests/test_surf_catalog.py\`:
listado con estado, transiciones, lectura de historial, recarga sin
\`ASK\`, recarga mientras otra solicitud está en curso, reconocimiento
de lectura, argumentación inválida y prevención de la doble petición.
