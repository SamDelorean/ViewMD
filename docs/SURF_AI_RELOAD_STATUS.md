# Surf AI — Recarga de conversación y semáforo integrado (18-11-2026)

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
seleccionar o recargar una conversación y al terminar una respuesta
originada desde Surf AI.

El menú «⋯» tiene tres acciones: **Recargar, Renombrar y Eliminar**.
El menú por clic derecho sigue disponible. El borrado conserva
confirmación obligatoria.

## ¿Qué hace Recargar?

**Recargar** genera exactamente una orden
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

Recargar **no es sincronización con los sitios web de ChatGPT/Claude**.
El catálogo sólo contiene las sesiones propias de Surf AI guardadas
en la T160. Mientras una respuesta está en curso, una recarga desde otra
sesión puede mostrar los mensajes ya guardados junto al estado amarillo,
pero no forzará una segunda ejecución del modelo.

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

**Estado: fuente y pruebas automatizadas añadidas a GitHub, NO
ejecutadas todavía en hardware**. Antes de usar el nuevo protocolo se
deben validar los tests Python de T160 y una compilación nativa en Surf;
la versión instalada actualmente no incorpora esta revisión. El bridge
que ya está corriendo aún entiende el protocolo anterior.

La actualización de los dos procesos debe ser coordinada: primero la
T160 (respaldando la base y conservando modo mock si aplica), y luego
el binario GTK3 en Surf con \`nice -n 10 make -C surf -j1\`. Commander
y cualquier reinicio de servicio requerirán autorización específica.

Pruebas añadidas en \`tests/test_surf_catalog.py\`:
listado con estado, transiciones, lectura de historial, recarga sin
\`ASK\`, recarga mientras otra solicitud está en curso, reconocimiento
de lectura, argumentación inválida y prevención de la doble petición.
