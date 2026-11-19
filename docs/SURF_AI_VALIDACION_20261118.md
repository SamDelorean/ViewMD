# Surf AI — validación de instalación y pruebas (2026-11-18)

Estado: **Cliente ARMv7 compilado; enlace SSH y protocolo mock PASS. APIs reales pendientes.**

## Instalación comprobada

| Equipo | Archivo principal |
|---|---|
| Surf, usuario user | /home/user/Projects/SurfAI/ViewMD/surf/surf-ai |
| Surf, log de compilación | /home/user/Projects/SurfAI/surf-build.log |
| T160, usuario spc | /home/spc/Projects/surf-ai/ViewMD/t160/bridge.py |
| T160, registro de pruebas | /home/spc/Projects/surf-ai/t160-test.log |
| Configuración SSH restringida en T160 | /home/spc/Projects/surf-ai/ssh/sshd_config |

Plataforma Surf observada: ARMv7l, postmarketOS v26.06, kernel
7.0.1-postmarketos-grate, GTK 3.24.52, XFCE4.
No hubo cambios del kernel, firmware, almacenamiento, DTB, ni reinicios.
Compilación sólo en Surf, nice -n 10 y make -j1. Sin Codex.

## Evidencia

- PASS: compilación ARMv7 del cliente GTK3, sin advertencias de compilador.
- PASS: ejecutable ELF ARM de 32 bits EABI5 enlazado con musl.
- PASS: ldd sin bibliotecas faltantes.
- PASS: ventana GTK abierta en pantalla :0 durante 7 segundos; se cerró por el límite de prueba. La prueba de pantalla no valida todavía todos los eventos del usuario.
- PASS: cuatro pruebas T160 de modo mock y protocolo (GPT, Claude, rechazo de proveedor inválido, UTF-8).
- PASS: análisis de sintaxis Python del bridge.
- PASS: autenticación SSH exclusiva para la Surf hacia T160 y reenvío de 127.0.0.1:8765.
- PASS: una petición UTF-8 desde Surf obtuvo 5 DELTA y DONE desde T160.
- PENDIENTE: conexiones reales a API GPT y Claude; no hay variables de clave en el entorno T160 verificado.
- PENDIENTE: revisión visual humana de tablas, código, tactilidad y manejo real de sesiones.

## Seguridad y conexión

T160: puerto 2222 SSH ligado a 192.168.100.7, con clave pública dedicada
de Surf y permisos de reenvío restringidos a 127.0.0.1:8765.
El servidor puente sólo escucha en loopback. ssh.service y ssh.socket
globales permanecen inactivos. La Surf dispone de una clave privada
separada en ~/.ssh/surf_ai_ed25519, no guardada en Git.
La Surf utilizó 192.168.100.11 en la prueba.
Las IP son datos temporales de la red: verificarlas antes de reutilizarlas.

Advertencia: apk reportó "1 error" al instalar openssh-client-default,
aunque la instalación del cliente SSH y la prueba de túnel funcionaron.
No se investigó el error de apk ni se modificó su base de datos.

## Ejecución actual

En esta prueba quedaron ejecutándose el bridge de T160 en modo mock,
la instancia SSH restringida y el reenvío SSH de la Surf. Para abrir
la ventana desde un terminal del escritorio XFCE4:

    /home/user/Projects/SurfAI/ViewMD/surf/surf-ai

El modo mock no realiza llamadas a GPT ni Claude ni consume créditos.

## Después de reiniciar

NO se configuró autoinicio. Desde T160, verificar primero su IP y ejecutar:

    sudo /usr/sbin/sshd -f /home/spc/Projects/surf-ai/ssh/sshd_config
    cd /home/spc/Projects/surf-ai/ViewMD
    python3 t160/bridge.py --mock

En Surf, sustituir la IP si ha cambiado:

    ssh -f -N -p 2222 -i ~/.ssh/surf_ai_ed25519 \
      -o IdentitiesOnly=yes -o BatchMode=yes \
      -o ExitOnForwardFailure=yes \
      -L 127.0.0.1:8765:127.0.0.1:8765 spc@192.168.100.7
    /home/user/Projects/SurfAI/ViewMD/surf/surf-ai

Para conexión a APIs reales, configurar modelos y claves en T160 y
ejecutar bridge.py sin --mock. No copiar claves a Surf.
No se habilitaron servicios persistentes.

## Alcance y limitaciones

El cliente tiene una interfaz GTK3 nativa con salida Markdown y
streaming de texto. El historial es temporal y el puente almacena
como máximo 12 pares por proveedor/sesión en memoria; sin adjuntos ni
renderizado de fórmulas avanzado. Tampoco se han probado todas las
funciones visuales en pantalla táctil ni llamadas reales a proveedores.
