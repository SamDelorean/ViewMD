#!/bin/sh
# Surf AI: persistent low-resource SSH local forwarding from Surface RT to T160.
# No model requests, secrets, polling of conversations, or remote shell access.
# OpenSSH connection blocks until failure; then reconnects after a short delay.
set -eu
umask 077
KEY="$HOME/.ssh/surf_ai_ed25519"
HOST="${SURF_T160_HOST:-192.168.100.7}"
USER="${SURF_T160_USER:-spc}"
PORT="${SURF_T160_SSH_PORT:-2222}"
RUNTIME="${XDG_RUNTIME_DIR:-$HOME/.cache}/surf-ai"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/surf-ai"
mkdir -p "$RUNTIME" "$CACHE"
LOCK="$RUNTIME/tunnel.lock"
LOG="$CACHE/ssh-tunnel.log"
exec 9>"$LOCK"
if ! flock -n 9; then
  exit 0   # Already running; never create duplicate listeners.
fi
while :; do
  if [ -f "$LOG" ] && [ "$(wc -c < "$LOG")" -gt 65536 ]; then
    : > "$LOG"
  fi
  /usr/bin/ssh -N -p "$PORT" -i "$KEY" \
    -o BatchMode=yes \
    -o StrictHostKeyChecking=yes \
    -o IdentitiesOnly=yes \
    -o ExitOnForwardFailure=yes \
    -o ConnectTimeout=8 \
    -o ServerAliveInterval=30 \
    -o ServerAliveCountMax=3 \
    -L 127.0.0.1:8765:127.0.0.1:8765 \
    "$USER@$HOST" >> "$LOG" 2>&1
  printf "SSH tunnel disconnected; retrying after 15 seconds.\n" >> "$LOG"
  sleep 15
done
