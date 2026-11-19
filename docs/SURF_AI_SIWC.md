# Surf AI — ChatGPT real sin clave API de pago

Estado: **operativo en T160 y probado desde Surf** (18 de noviembre de 2026).
Rama: `surf-ai-client`. Cliente Surf: GTK3 nativo ARMv7.

## Autenticación y facturación

Se utilizó **Sign in with ChatGPT** (OAuth Authorization Code + PKCE)
y consentimiento específico para `chatgpt.tokens.use.direct`.
T160 guarda las credenciales OAuth únicamente en
`~/.config/surf-ai/siwc/` (directorio 0700, perfil 0600). Surf no ve
los tokens ni las claves privadas.

El puente **no usa `OPENAI_API_KEY` ni `ANTHROPIC_API_KEY`** ni tiene
fallback a facturación API. La inferencia GPT pasa por
`https://api.openai.com/v1/responses` con token OAuth, `store=false`
y `stream=true`, aceptando éxito sólo después de
`response.completed`. El modelo seleccionado de la lista autorizada
por la cuenta es `gpt-6.2-sol` (**GPT-6.2-Sol**).

**Importante:** las solicitudes utilizan los límites del plan ChatGPT.
OpenAI ofrece configuración por aplicación en **ChatGPT → Settings →
Usage** para administrar el uso del plan y los créditos disponibles.
El puente no puede comprobar ni modificar directamente esos límites.
Para respetar la condición de evitar gastos adicionales, no se han
configurado claves de API ni compras de créditos. Revisar y limitar
en ChatGPT cualquier uso de créditos que no se desee autorizar.
El flujo no accede al historial alojado en chatgpt.com.

Claude real permanece deshabilitado hasta disponer de una vía oficial
compatible con suscripción que no produzca facturación adicional.
**Actualización 18/11/2026:** Codex App Server ya está integrado en
el chat de Surf AI con SIWC, conversación de código sin herramientas y
sin claves API. Se validó una consulta real y la continuidad del
historial; ver `docs/SURF_CODEX_NATIVE.md`. Claude Code sigue pendiente.

## Servicio principal T160

Archivo de unidad en repositorio:
`t160/surf-ai-bridge.service`.

- Instalado como servicio de usuario:
  `~/.config/systemd/user/surf-ai-bridge.service`.
- Configuración privada local:
  `~/.config/surf-ai/bridge.env` (0600), contiene
  `SURF_OPENAI_MODEL=gpt-6.2-sol`, sin claves API.
- Servicio **activo y habilitado** mediante `systemctl --user enable --now`.
- `Restart=on-failure`, sin `--mock`; tras reiniciar la T160 su
  usuario dispone de `Linger=yes` y arranca la unidad.
- Escucha exclusivamente en `127.0.0.1:8765`, no en la red externa.
- No realiza consultas ni consume cuota al iniciar: sólo ante una
  operación `ASK` originada por un cliente.

Comprobación sin secretos en T160:

```sh
cd ~/Projects/surf-ai/ViewMD
python3 -m t160.siwc_auth status
python3 -m t160.siwc_auth models
systemctl --user status surf-ai-bridge.service
```

## Transporte SSH dedicado

Unidad de sistema T160:
`t160/surf-ai-sshd.service`, instalada y habilitada como
`/etc/systemd/system/surf-ai-sshd.service`.

Usa la configuración dedicada
`~/Projects/surf-ai/ssh/sshd_config` para escuchar
`192.168.100.7:2222`, con autenticación exclusivamente por clave,
sin contraseña, shell interactiva, TTY ni agente, y sólo permite
reenvío local a `127.0.0.1:8765`. Se preservó la clave de host
existente, por lo que Surf mantiene la comprobación estricta de
huella SSH.

En Surf, `scripts/surf-ai-ssh-tunnel.sh` mantiene el reenvío hacia la
T160. El script usa `flock` para impedir túneles duplicados,
`ServerAliveInterval=30` y un retardo de 15 segundos para reconectar
sólo cuando SSH se desconecta. La bitácora está acotada a 64 KiB.
No consulta el estado de las conversaciones ni realiza peticiones a
los modelos.

El arranque gráfico de XFCE4 se instaló como:
`~/.config/autostart/surf-ai-tunnel.desktop`, con
`Exec=/bin/sh /home/user/Projects/SurfAI/ViewMD/scripts/surf-ai-ssh-tunnel.sh`.

**Nota:** se validaron el túnel y el archivo de autoinicio en la
sesión actual. No se reiniciaron físicamente ambas máquinas para
comprobar un ciclo completo de arranque.

## Pruebas de aceptación y persistencia

- 26 pruebas Python de SIWC, JWT, seguridad, almacenamiento, bridge y
  SQLite: **PASS** (sin consumo real durante la batería).
- Login real en T160: perfil autorizado, `chatgpt.tokens.use.direct`
  concedido; catálogo de 10 modelos disponible.
- Solicitud de inferencia directa T160 → GPT-6.2-Sol:
  **PASS**, respuesta «Surf AI conectado.».
- Prueba completa desde Surf por su túnel SSH:
  `ASK` real → respuesta «Surf AI en línea.» →
  `LIST` (`ready`) → `RELOAD` (`idle`) →
  dos mensajes recuperados → segundo `RELOAD` sin duplicación →
  `DELETE` de la conversación temporal. **PASS**.
- La prueba de reconexión del nuevo supervisor se limitó a
  establecer el túnel y consultar `LIST` con éxito.
- Base SQLite de conversaciones existente respaldada y verificada
  antes de activar el servicio:
  `~/Projects/surf-ai/backups/sessions-before-siwc-live-20261118-224156.sqlite3`.
  En ese momento: cuatro conversaciones y 30 mensajes.
- No se reescribió el ejecutable GTK3 de Surf; ya era compatible con
  el protocolo `ASK/LIST/HISTORY/RELOAD`.

## Recuperación y diagnóstico

Si GPT deja de responder, revisar en orden:

1. En T160: `systemctl --user is-active surf-ai-bridge.service`.
2. En T160: `systemctl is-active surf-ai-sshd.service`.
3. En Surf: comprobar que existe el proceso
   `scripts/surf-ai-ssh-tunnel.sh` y que su puerto local 8765
   responde a `LIST`.
4. En T160: `python3 -m t160.siwc_auth status`; si el permiso expiró
   o fue revocado, reautorizar manualmente desde el navegador.
5. Consultar **Settings → Usage** si se supera un límite de plan
   o de la aplicación. No introducir una API key como alternativa.

Si se desea suspender GPT real sin perder la cuenta ni el historial:

```sh
systemctl --user stop surf-ai-bridge.service
```

Esto no revoca los tokens: la desconexión de la cuenta se administra
desde ChatGPT y debe hacerse por decisión del usuario.

## Referencias oficiales

- https://developers.openai.com/siwc/token-sharing-open-source/
- https://developers.openai.com/siwc/token-sharing-open-source/sign-in
- https://developers.openai.com/siwc/token-sharing-open-source/profiles-and-sessions
- https://developers.openai.com/siwc/token-sharing-open-source/models-and-inference
- https://developers.openai.com/siwc/token-sharing-open-source/errors-and-recovery
