#!/bin/sh
# Surf AI: inspeccion solo lectura. Sin sudo ni cambios en drivers/X11.
set -u
echo '=== SURF TOUCH AUDIT READ ONLY ==='
printf 'Arch: '; uname -m
printf 'Kernel: '; uname -r
printf 'DISPLAY: '; printenv DISPLAY || echo 'unset'

echo '=== Dispositivos Linux input ==='
if [ -r /proc/bus/input/devices ]; then
    awk '/^N: Name=|^H: Handlers=|^B: ABS=|^B: EV=/{print}' /proc/bus/input/devices
else
    echo 'No se puede leer /proc/bus/input/devices'
fi

echo '=== XInput (si existe) ==='
if command -v xinput >/dev/null 2>&1; then
    xinput --list 2>&1 || true
else
    echo 'xinput no instalado; no es requisito de Surf AI'
fi

echo '=== Onboard (opcional) ==='
if command -v onboard >/dev/null 2>&1; then
    echo 'Onboard disponible'
else
    echo 'Onboard no encontrado; Surf AI tiene teclado GTK3 propio'
fi

echo '=== FIN: No se realizaron cambios ==='
