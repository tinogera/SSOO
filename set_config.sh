#!/usr/bin/env bash
#
# set_config.sh — Escribe o actualiza claves en el .config de un módulo, sin editor de texto.
#
# Uso:
#   ./set_config.sh <modulo> CLAVE=VALOR [CLAVE=VALOR ...]
#
# Módulos (mismos nombres que deploy.sh):
#   km    Kernel Memory       ks    Kernel Scheduler
#   cpu   CPU                 io    IO
#   ms    Memory Stick        swap  Swap
#
# Si el .config del módulo no existe todavía, se crea a partir de su .config.example.
# Si una CLAVE ya existe en el archivo, se reemplaza su valor. Si no existe, se agrega al final.
#
# Ejemplos:
#   ./set_config.sh km KERNEL_MEMORY_PORT=23841 SCRIPTS_BASEPATH=/home/utnso/pruebas
#   ./set_config.sh cpu IP_KERNEL=10.100.3.11 PUERTO_KERNEL=19337

set -uo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"

# ─── colores ────────────────────────────────────────────────────────────────
C_OK="\033[0;32m"; C_ERR="\033[0;31m"; C_INFO="\033[0;36m"; C_RST="\033[0m"
log_ok()   { echo -e "${C_OK}[set_config]${C_RST} $*"; }
log_err()  { echo -e "${C_ERR}[set_config] ERROR:${C_RST} $*" >&2; }
log_info() { echo -e "${C_INFO}[set_config]${C_RST} $*"; }

# ─── ayuda ──────────────────────────────────────────────────────────────────
MODULO="${1:-}"
if [ -z "$MODULO" ] || [ "$#" -lt 2 ]; then
    sed -n '2,/^[^#]/p' "$0" | grep '^#' | sed 's/^# \?//'
    exit 1
fi
shift

case "$MODULO" in
    km|kernel_memory)    CFG="$REPO/kernel_memory/kernel_memory.config" ;;
    ks|kernel_scheduler) CFG="$REPO/kernel_scheduler/kernel_scheduler.config" ;;
    cpu)                 CFG="$REPO/cpu/cpu.config" ;;
    io)                  CFG="$REPO/io/io.config" ;;
    ms|memory_stick)     CFG="$REPO/memory_stick/memory_stick.config" ;;
    swap)                CFG="$REPO/swap/swap.config" ;;
    *)
        log_err "Módulo desconocido: '$MODULO'"
        echo "Módulos válidos: km, ks, cpu, io, ms, swap"
        exit 1
        ;;
esac

# ─── crear el archivo si no existe ──────────────────────────────────────────
if [ ! -f "$CFG" ]; then
    EJEMPLO="${CFG}.example"
    if [ -f "$EJEMPLO" ]; then
        log_info "No existe $CFG — creándolo a partir de $EJEMPLO"
        cp "$EJEMPLO" "$CFG"
    else
        log_info "No existe $CFG ni $EJEMPLO — creando archivo vacío"
        : > "$CFG"
    fi
fi

# ─── aplicar cada CLAVE=VALOR ───────────────────────────────────────────────
for par in "$@"; do
    if [[ "$par" != *=* ]]; then
        log_err "Argumento inválido (esperado CLAVE=VALOR): '$par'"
        exit 1
    fi
    clave="${par%%=*}"
    valor="${par#*=}"
    if grep -q "^${clave}=" "$CFG"; then
        sed -i "s|^${clave}=.*|${clave}=${valor}|" "$CFG"
        log_ok "Actualizado: ${clave}=${valor}"
    else
        echo "${clave}=${valor}" >> "$CFG"
        log_ok "Agregado: ${clave}=${valor}"
    fi
done

log_info "Config final: $CFG"
