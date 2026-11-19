#!/bin/sh
# Read-only audit of the existing Surf. No sudo, no kernel changes, no writes.
set -u
echo '=== Surf AI | auditoría SOLO LECTURA ==='
date -u
uname -a
if [ -r /etc/os-release ]; then
    sed -n '1,12p' /etc/os-release
fi
echo '=== Arquitectura y memoria ==='
uname -m
free -m 2>/dev/null || true
echo '=== GTK3 y compilación ==='
command -v gcc || true
command -v make || true
command -v pkg-config || true
pkg-config --modversion gtk+-3.0 2>/dev/null || true
echo '=== Paquetes (sin modificar) ==='
apk info -e gtk+3.0 gtk+3.0-dev build-base pkgconf git 2>/dev/null || true
echo '=== Red (sin acciones) ==='
ip -br link 2>/dev/null || true
echo '=== Almacenamiento (sin acciones) ==='
lsblk -o NAME,SIZE,TYPE,MOUNTPOINTS 2>/dev/null || true
df -h / /home 2>/dev/null || true
echo '=== Sesión ==='
printenv DISPLAY 2>/dev/null || true
printenv XDG_SESSION_TYPE 2>/dev/null || true
echo '=== Fin. No se realizaron cambios. ==='
