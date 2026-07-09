#!/usr/bin/env bash
#
# set_ip.sh — Actualiza la IP de un rol (Kernel Memory, Kernel Scheduler o Memory Stick)
# en TODOS los .config que la referencian, sin editar cada archivo a mano.
#
# Uso:
#   ./set_ip.sh km <ip>              # IP de la VM donde corre Kernel Memory
#   ./set_ip.sh ks <ip>              # IP de la VM donde corre Kernel Scheduler
#   ./set_ip.sh ms <ip> [indice]     # IP de la VM donde corre un Memory Stick (indice por defecto: 0)
#
# Correr una vez por cada rol cuya VM sea distinta a la propia (ver Sección 7 de Doc/instalacion.md).
#
# Ejemplos:
#   ./set_ip.sh km 10.100.3.10
#   ./set_ip.sh ks 10.100.3.11
#   ./set_ip.sh ms 10.100.3.14 0

set -uo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"
SET_CONFIG="$REPO/set_config.sh"

C_OK="\033[0;32m"; C_ERR="\033[0;31m"; C_RST="\033[0m"
log_ok()  { echo -e "${C_OK}[set_ip]${C_RST} $*"; }
log_err() { echo -e "${C_ERR}[set_ip] ERROR:${C_RST} $*" >&2; }

ROL="${1:-}"
IP="${2:-}"
if [ -z "$ROL" ] || [ -z "$IP" ]; then
    sed -n '2,/^[^#]/p' "$0" | grep '^#' | sed 's/^# \?//'
    exit 1
fi

case "$ROL" in
    km|kernel_memory)
        "$SET_CONFIG" ks   "KERNEL_MEMORY_IP=$IP"
        "$SET_CONFIG" cpu  "IP_MEMORY=$IP"
        "$SET_CONFIG" ms   "KERNEL_MEMORY_IP=$IP"
        "$SET_CONFIG" swap "KERNEL_MEMORY_IP=$IP"
        log_ok "IP de Kernel Memory ($IP) actualizada en kernel_scheduler, cpu, memory_stick y swap."
        ;;

    ks|kernel_scheduler)
        "$SET_CONFIG" cpu "IP_KERNEL=$IP"
        "$SET_CONFIG" io  "KERNEL_SCHEDULER_IP=$IP"
        log_ok "IP de Kernel Scheduler ($IP) actualizada en cpu e io."
        ;;

    ms|memory_stick)
        IDX="${3:-0}"
        "$SET_CONFIG" cpu "IP_MEMORY_STICK_${IDX}=$IP"
        log_ok "IP de Memory Stick #$IDX ($IP) actualizada en cpu."
        ;;

    *)
        log_err "Rol desconocido: '$ROL'"
        echo "Roles válidos: km, ks, ms"
        exit 1
        ;;
esac
