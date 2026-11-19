# Surf AI — Codex nativo por chat

**Fecha:** 18 de noviembre de 2026. **Estado:** implementado, instalado y
probado de extremo a extremo en Surface RT → SSH → T160 → Codex App Server.

## Decisión de arquitectura

**Codex es un proveedor de chat dentro de Surf AI**, al igual que GPT.
El botón **Codex** NO debe lanzar el escritorio remoto, FreeRDP ni
ventanas externas. Sólo selecciona las conversaciones propias de Codex.
El backend de la T160 ejecuta Codex App Server durante una consulta
`ASK` explícita y entrega la respuesta mediante el streaming habitual.

Los otros modos siguen independientes:
- GPT: conexión SIWC Responses mediante `openai`.
- Codex: Codex App Server mediante proveedor `codex`.
- Claude: pendiente de integración permitida sin facturación adicional.
- Claude Code: pendiente de implementación autorizada.

El protocolo TCP/SSH de Surf AI continúa idéntico:
`ASK`, `LIST`, `HISTORY`, `RELOAD`, `RENAME`, `DELETE`,
con estados `idle`, `working`, `ready`, `error`.
Los catálogos de GPT y Codex se separan por proveedor en SQLite.

## Autenticación y límites de uso

Codex usa el token OAuth **Sign in with ChatGPT** ya autorizado en la
T160 mediante `siwc_auth.access_token()`, transferido únicamente
al subproceso `codex app-server` como `ACCESS_TOKEN`.
Se configura el proveedor oficial `openai_chatgpt_plan`, con
`https://api.openai.com/v1`, `wire_api="responses"` y HTTP/SSE.

- **Ninguna** `OPENAI_API_KEY` ni `ANTHROPIC_API_KEY`.
- **Sin fallback** a facturación API.
- Modelo autorizado seleccionado: `gpt-6.2-sol`.
- Las peticiones consumen cuota del plan ChatGPT; también pueden usar
  créditos disponibles **si el usuario habilitó ese consumo**.
  Revisar límites específicos en **ChatGPT → Configuración → Uso**.
- El login de la CLI de Codex original está registrado con ChatGPT,
  pero el adaptador utiliza el perfil SIWC de Surf AI en T160.

## Restricciones deliberadas de Codex en Surf AI

**Primera etapa: conversación sobre código, sin herramientas.**

En `t160/codex_adapter.py` se mantiene:

- `features.shell_tool=false`.
- `features.apps=false` y `features.plugins=false`.
- `agents.enabled=false`.
- `features.request_permissions_tool=false`.
- Sandboxing `read-only` y política de aprobaciones `never`.
- Rechazo explícito de solicitudes de aprobación de comandos,
  cambios de archivos, herramientas MCP y similares.
- Directorio de trabajo independiente de los proyectos del usuario:
  `~/.local/share/surf-ai/codex-notes`.
- `CODEX_HOME` propio:
  `~/.config/surf-ai/codex`, con permisos privados.
- El proceso App Server se inicia ante cada turno y se cierra al
  finalizar; no hay consultas periódicas ni agentes persistentes.

Estas precauciones **no convierten** a Codex en un sandbox externo
hermético frente a todo acceso de lectura al sistema. Antes de dar
permisos para operar sobre repositorios o archivos, será necesario
definir un mecanismo de autorizaciones adicional y auditarlo.
**No hay autorización para ejecutar comandos ni editar proyectos.**

## Continuidad

Los identificadores de conversación de la interfaz se almacenan en
`conversations` y `messages` de la base SQLite de T160.
Un segundo registro `codex_threads` mantiene un identificador
`thread.id` inmutable por conversación Codex.

La primera consulta emite `thread/start`, y las sucesivas usan
`thread/resume`, aun si el proceso App Server se reinicia entre turnos.
En cada turno se selecciona el modelo autorizado y se utiliza
`turn/start`; el texto se transmite a Surf mediante
`item/agentMessage/delta`. Sólo se considera exitosa la respuesta
tras `turn/completed` con `status=completed`.

**Actualizar** se limita a consultar SQLite; no reenvía la petición
al modelo. Borrar una conversación elimina las entradas SQLite y la
referencia a `thread.id`. La gestión de los archivos locales internos
de Codex es una operación distinta, pendiente de una política propia.

## Instalación real

### T160

El servicio existente se mantiene:

- Unidad: `~/.config/systemd/user/surf-ai-bridge.service`
- Estado: activo, habilitado, `mock=False`
- Dirección: `127.0.0.1:8765`
- Entorno privado: `~/.config/surf-ai/bridge.env`
- Variables activadas:
  `SURF_OPENAI_MODEL=gpt-6.2-sol`,
  `SURF_CODEX_TEXT_ONLY=1`,
  `SURF_CODEX_MODEL=gpt-6.2-sol`.

Se efectuó copia de la base SQLite antes del cambio:
`~/Projects/surf-ai/backups/sessions-before-native-codex-20261118-233425.sqlite3`.
La copia pasó `PRAGMA integrity_check`.

El servicio RDP de GNOME, configurado previamente por una confusión
con el alcance del proyecto, quedó **desactivado y sin escuchar en el
puerto 3389**. No se borraron contraseñas ni certificados; se dejó
intacto el túnel SSH dedicado y el servicio GPT.

### Surface RT

El ejecutable GTK3 ARMv7 instalado en:

`~/Projects/SurfAI/ViewMD/surf/surf-ai`

SHA-256:

`1d821c66bee2ca078214f8970a0b658c30abbdf44472271dca4c7af8be291a35`

Respaldo del anterior:
`~/Projects/SurfAI/backups/surf-ai-before-native-codex-20261118`.

El botón **Codex** selecciona `provider=codex` en la propia interfaz,
mostrando las conversaciones y respuestas de Codex en el panel derecho.
No intenta abrir `surf-codex-rdp`.

**Una instancia antigua de Surf AI seguía abierta en la pantalla al
instalar el binario.** Por seguridad se preservó cualquier posible
borrador; el usuario debe cerrar y reabrir la aplicación para cargar
el nuevo código. La observación manual del botón después de reabrir
la ventana queda pendiente de confirmación.

## Pruebas realizadas

- 34 pruebas automatizadas del bridge y adaptador, OAuth, catálogo,
  aislamiento y rechazo de herramientas: **PASS**.
- Handshake real con Codex App Server y configuración efectiva
  `shell_tool=false`: **PASS**, sin iniciar consulta.
- Turno real de texto con token SIWC: **PASS**,
  «Codex en Surf AI.».
- Continuidad de dos turnos, con recuperación por `thread/resume`:
  **PASS**.
- Servicio TCP aislado en T160, dos turnos, historial, recarga,
  borrado e integridad SQLite: **PASS**.
- Servicio de producción, consulta real originada desde Surf por SSH:
  **PASS**, «Integración Codex Surf AI operativa.».
- Eventos recibidos: nueve fragmentos `DELTA`; estado `ready` al
  completar, `idle` después de `RELOAD`, dos mensajes guardados.
- La sesión temporal de aceptación se borró al terminar.
- Compilación ARMv7 nativa sin advertencias, enlazado GTK3 completo:
  **PASS**. Smoke test GUI con 260 px de navegación y 1085 px de
  contenido: **PASS**.
- Prueba manual del botón en la nueva instancia: **pendiente**.

## Fuentes oficiales

- https://developers.openai.com/siwc/token-sharing-open-source/codex-app-server
- https://developers.openai.com/siwc/token-sharing-open-source/preview-limitations
- https://developers.openai.com/codex/app-server
