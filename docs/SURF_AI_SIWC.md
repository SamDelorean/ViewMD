# Surf AI — Inicio de sesión con ChatGPT sin facturación API

Fecha: 2026-11-18. Estado: **módulo preparado y verificado sin cuenta real**.
Ruta de código: \`t160/siwc_auth.py\` (Python 3, PyJWT); bridge:
\`t160/bridge.py\`. Rama \`surf-ai-client\`.

## Objetivo

Autenticar el servidor T160 de Surf AI con la cuenta **ChatGPT** del
propietario usando **Sign in with ChatGPT (SIWC)** de OpenAI para
clientes de código abierto que funcionan localmente. No pedir ni
almacenar \`OPENAI_API_KEY\` ni \`ANTHROPIC_API_KEY\`, ni permitir
un fallback a claves de API de pago. Surface RT sigue siendo la GUI
GTK3 y no recibe credenciales ni ejecuta agentes.

**Límite:** la autorización usa cuota del plan ChatGPT cuando el usuario
tiene un plan elegible y autoriza el permiso
\`chatgpt.tokens.use.direct\`. Se aplican los límites de ese plan;
no se accede al historial de chatgpt.com. Las llamadas pueden consumir
cuota del plan y, según configuración de ChatGPT, créditos disponibles.
Para evitar consumo de créditos no deseado, revisar los límites de
uso de aplicaciones en ChatGPT > Configuración > Uso antes de autorizar.
No existe una garantía de acceso si el plan pierde elegibilidad.

Fuentes oficiales, consultadas el 18/11/2026:

- https://developers.openai.com/siwc/token-sharing-open-source/
- https://developers.openai.com/siwc/token-sharing-open-source/sign-in
- https://developers.openai.com/siwc/token-sharing-open-source/profiles-and-sessions
- https://developers.openai.com/siwc/token-sharing-open-source/models-and-inference
- https://developers.openai.com/siwc/token-sharing-open-source/preview-limitations

## Cómo iniciar sesión (única intervención manual)

**En una terminal de la T160, con un navegador abierto en el mismo
equipo**, ejecutar:

\`\`\`sh
cd /home/spc/Projects/surf-ai/ViewMD
python3 -m t160.siwc_auth status
python3 -m t160.siwc_auth login
\`\`\`

La orden \`login\` crea un identificador estable de la T160, inicia un
servidor **temporal sólo en 127.0.0.1** con puerto libre, prepara
OAuth Authorization Code con PKCE S256, state y nonce aleatorios, y
abre el navegador con el enlace de OpenAI. No utilizar la URL de
retorno en otro dispositivo sin reenviar correctamente el puerto
de callback hacia la T160. No compartir enlaces de autorización con
terceros.

En el navegador oficial, elegir la cuenta y autorizar explícitamente
uso del plan ChatGPT para **Surf AI**. El permiso solicitado es
\`openid profile email offline_access resource.invoke
chatgpt.tokens.use.direct\`. El navegador vuelve a
\`http://127.0.0.1:<puerto>/auth/callback\`. La terminal verifica state
y nonce, recibe un \`client_id\` emitido por OpenAI, canjea el código
con el mismo PKCE y valida firma JWT RS256 con JWKS, issuer,
audiencia y caducidad. **No se puede iniciar sesión por el usuario
ni obtener su contraseña desde ChatGPT.**

Si autorizó correctamente, aparece «ChatGPT conectado».
No se activa la inferencia real automáticamente tras el login.

### Verificar modelos permitidos

\`\`\`sh
cd /home/spc/Projects/surf-ai/ViewMD
python3 -m t160.siwc_auth status
python3 -m t160.siwc_auth models
\`\`\`

La lista se obtiene desde el catálogo autorizado por la cuenta
(\`GET https://api.openai.com/v1/models\`). Sólo se utilizará un
\`slug\` listado allí. **No adivinar** nombres de modelos ni confiar
en un catálogo de otra cuenta.

## Activación del puente REAL (fase posterior al consentimiento)

La T160 conserva el servicio existente \`surf-ai-bridge.service\`
con \`--mock\`, y las pruebas continúan funcionando sin cuenta.

Cuando estén confirmados \`status\` y \`models\`, realizar una
activación controlada del servicio con \`SURF_OPENAI_MODEL\` igual a
uno de los slugs autorizados, **sin \`--mock\`**. El nuevo código de
\`t160/bridge.py\` usa exclusivamente \`siwc_auth.access_token()\`
para GPT y manda un POST oficial a
\`https://api.openai.com/v1/responses\` con \`store=false\`,
\`stream=true\`. La respuesta se considera correcta sólo tras
\`response.completed\`; ante error o pérdida de permiso, se detiene.
No se utilizan endpoints internos de chatgpt.com.

El cambio mock → real **no se ejecuta automáticamente** y necesita
validación de consentimiento, modelo y una consulta explícita.
Claude real sigue deshabilitado hasta contar con una solución de
suscripción permitida sin facturación adicional.

## Seguridad y recuperación

- Directorio de credenciales en T160:
  \`~/.config/surf-ai/siwc/\` (0700). \`profile.json\` y \`host.json\`
  se escriben de forma atómica con 0600. La Surf no recibe estos archivos.
- La renovación del token usa \`refresh_token\` rotativo y un lock de
  archivo; no hay reconexiones periódicas, ni monitor en segundo plano.
- No imprimir tokens, contraseñas ni códigos OAuth en logs. La URL de
  reautorización con \`id_token_hint\` se abre sin imprimirla.
- Si la cuenta no autoriza \`chatgpt.tokens.use.direct\`, se rechaza
  todo acceso real. No existe fallback a API key, ni para OpenAI ni Anthropic.
- No ejecutar Codex ni Claude Code como parte del inicio de sesión.
- Si se pierde acceso, conservar el puente mock hasta reautorización
  específica; no configurar métodos de pago ni facturación.
- Para revocar permisos manualmente, utilizar las conexiones y el
  control de uso de aplicaciones en la configuración de ChatGPT.
  No borrar manualmente credenciales antes de revocar la conexión si
  todavía se necesita cerrar la sesión remota.

## Verificación realizada

T160 (18/11/2026):
- 26 pruebas unitarias y de protocolo **PASS**.
- JWT RS256 comprobado con claves de prueba; rechazo de firma, issuer,
  audiencia, nonce y permisos inválidos.
- Renovación rotativa simulada y almacenamiento 0600/0700.
- Detección de acceso inexistente y prohibición de llamadas API de pago.
- Discovery OIDC público de \`auth.openai.com\` consultado desde T160:
  issuer, authorize URL, token URL y JWKS verificados; algoritmos RS256.
- Servicio real vigente **sin cambios**, \`--mock\`.
- **No se ha realizado OAuth con una cuenta real**, ni inferencia GPT
  real ni cargos de API. La prueba final de login exige consentimiento
  de usuario desde el navegador de T160.

Logs: \`~/Projects/surf-ai/siwc-offline-tests.log\`.
