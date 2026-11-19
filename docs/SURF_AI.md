# Surf AI — preparación técnica v0.1 (18-11-2026)

## Objetivo

Cliente de IA nativo **C + GTK3** para Microsoft Surface RT, reutilizando
el renderizador Markdown del fork ViewMD. La **T160** actúa como intermediario:
GPT, Claude, TLS, claves y contexto de conversación se gestionan allí.
El proyecto no instala navegador, Electron ni un nuevo sistema operativo.

## Inventario histórico de Surf (NO verificado en vivo)

| Elemento | Estado conocido |
|---|---|
| Equipo | Surface RT primera generación, modelo 1516 |
| SoC / RAM | NVIDIA Tegra 3, ARMv7 de 32 bits, 2 GB |
| Sistema | postmarketOS v26.06, Alpine/musl, XFCE4 |
| Kernel | 7.0.1-postmarketos-grate |
| Pantalla | 1366 × 768, táctil |
| Wi-Fi funcional | Adaptador USB |
| Wi-Fi interno | Marvell 88W8797 / mwifiex_sdio, inestable |
| Bluetooth | No acreditado ni operativo |
| Audio | No requerido por esta aplicación |
| Arranque | USB /dev/sda (7.2 GB), rendimiento limitado; dato histórico |
| eMMC | /dev/mmcblk0 (29.7 GB); NO TOCAR |
| SSH | user@surf.local, sujeto a la red actual |
| IP previa | 192.168.0.6; NO asumir como actual |
| Suspensión | s2idle, sin soporte deep confirmado |

La auditoría real sigue pendiente. El cliente GTK no depende de los trabajos
experimentales de Bluetooth, SDIO ni suspensión.

## Restricciones

- NO cambiar kernel, Device Tree, firmware, particiones ni controladores.
- Conservar el adaptador Wi-Fi USB.
- No usar Codex ni Remote Desktop Commander sin autorización.
- Compilación **nativa en Surf**, con nice y make -j1; no cruzar ni compilar
  para este proyecto en la T160.
- Sin instalación global, ejecución remota, servicios de arranque ni cambios
  de sistema sin indicación.
- Mantener la rama master y el ViewMD original intactos.
- Claves de OpenAI y Anthropic sólo en T160.
- Bridge T160 únicamente en loopback 127.0.0.1; conectividad vía túnel SSH.

## Archivos preparados

- surf/surf_ai.c: interfaz nativa con selector GPT/Claude, visor Markdown
  ViewMD, entrada multilínea, respuesta progresiva y sesión en memoria.
- surf/Makefile: binario independiente de ViewMD, compilación ARMv7 nativa.
- surf/surf-ai.desktop: lanzador opcional (no instalado).
- scripts/surf-audit.sh: inspección estrictamente de solo lectura.
- t160/bridge.py: relay Python 3 estándar con OpenAI Responses API,
  Anthropic Messages API, SSE, sesiones temporales y modo de prueba.
- docs/SURF_AI.md: inventario, protocolo, comandos y pruebas.

## 1. Auditoría de Surf (solo lectura)

Desde la raíz del fork, sobre Surf:

    sh scripts/surf-audit.sh

El script no usa sudo, no modifica la red y no escribe archivos.
Registrar la versión real de paquetes y de kernel antes de cualquier acción.

## 2. Compilación nativa (solo cuando se autorice en Surf)

Si el informe detecta herramientas ausentes, **no** instalarlas automáticamente.
Paquetes previstos en Alpine ARMv7:

    sudo apk add build-base pkgconf gtk+3.0-dev git

Después, desde la raíz del repositorio:

    make -C surf check
    nice make -C surf -j1
    ./surf/surf-ai

El paquete gtk+3.0-dev existe en repositorios Alpine para ARMv7. No activar
edge ni sustituir postmarketOS. Para detener, cerrar la ventana.
La aplicación usa el ViewMD original enlazado como código fuente,
sin modificar su configuración de usuario.

## 3. Prueba sin proveedor (T160)

En T160 (Python 3), en copia local del fork:

    python3 t160/bridge.py --mock

El programa escucha exclusivamente 127.0.0.1:8765 de T160.
En Surf, una vez confirmadas las credenciales SSH habituales y el host:

    ssh -N -L 8765:127.0.0.1:8765 USUARIO@HOST_T160

Sustituir USUARIO y HOST_T160 con datos actuales; no usar IPs históricas.
No habilitar algoritmos SSH inseguros.

En Surf, abrir surf-ai, escribir un mensaje y pulsar Ctrl+Enter. El modo
mock devuelve un título, tabla y código para probar el renderizador;
no realiza llamadas a proveedores ni genera cargos.

## 4. Prueba GPT / Claude (T160)

Configurar exclusivamente EN T160 estas variables de entorno:

- OPENAI_API_KEY y SURF_OPENAI_MODEL (identificador oficial válido).
- ANTHROPIC_API_KEY y SURF_ANTHROPIC_MODEL.
- SURF_MAX_TOKENS (opcional, por defecto 1536).

Iniciar:

    python3 t160/bridge.py

Usar el mismo túnel SSH en Surf, cambiar entre GPT/Claude con el selector
de la aplicación. El cliente no contiene ni almacena claves.

APIs oficiales: https://developers.openai.com/api/docs/guides/streaming-responses
y https://platform.claude.com/docs/en/build-with-claude/streaming.

## 5. Protocolo v0.1

TCP dentro de SSH; **una solicitud por conexión**; UTF-8 codificado como base64.

Solicitud de cliente:

    ASK<TAB>openai|anthropic<TAB>session-id<TAB>base64(UTF-8)<LF>

Respuestas del bridge:

    DELTA<TAB>base64(UTF-8)<LF>  (cero o más)
    DONE<LF>                  (fin correcto)
    ERROR<TAB>base64(UTF-8)<LF> (error terminal)

El servidor limita la línea entrante a 1 MiB y conserva hasta 12 pares
de mensajes por sesión y proveedor **sólo en memoria**. No proporciona
autenticación propia ni protege conexiones TCP sin SSH: NO exponer su
puerto 8765 fuera de localhost.

## 6. Alcance real y pendientes

Fuente implementada: UI GTK3, renderizado ViewMD (texto rico, código, tablas,
enlaces, imágenes locales), entrada de texto, streaming de GPT/Claude
vía T160 y prueba mock. Sin WebView. El repintado se limita a ~120 ms.

**No verificado todavía**: compilación ARMv7 en Surf, ejecución física,
rendimiento de renderizado, alcance de imágenes bajo contenido remoto
y conectividad SSH real. Sin instalación en Surf ni T160.

No implementado en esta fase: historial persistente, recuperación de sesión
tras reinicio, adjuntos, descarga segura de imágenes remotas, renderizado
completo de LaTeX, panel lateral y reproducción de todas las funciones de
las interfaces oficiales de ChatGPT y Claude. Antes de declarar paridad
visual, validar cada componente por separado.

## 7. Pruebas de aceptación

1. Revisar auditoría; confirmar ARMv7, GTK3, Wi-Fi USB y kernel sin cambios.
2. Compilar nativamente con nice make -j1; inspeccionar warnings y errores.
3. Ejecutar GTK3 con XFCE4 a 1366 × 768, teclado y táctil.
4. Con mock, validar Markdown, tablas, código y scroll durante streaming.
5. Romper el túnel; comprobar error visible y UI recuperable.
6. Con credenciales sólo en T160, probar un intercambio GPT y uno Claude.
7. Confirmar que master y ViewMD original no sufrieron modificaciones.

**Estado 2026-11-18:** fuentes y procedimientos preparados en GitHub;
pruebas en hardware PENDIENTES.
