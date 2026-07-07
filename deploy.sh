#!/usr/bin/env bash
#
# deploy.sh — Compila y levanta UN módulo del TP.
# Cada máquina corre este script una vez para su módulo.
#
# Uso:
#   ./deploy.sh <modulo> [opciones...]
#
# Módulos disponibles:
#   km              Kernel Memory
#   ms   <tamaño>   Memory Stick  (tamaño en bytes, ej: 1024)
#   swap            Swap
#   ks   <script>   Kernel Scheduler (path al .prc del proceso inicial)
#   cpu  <id>       CPU  (identificador numérico, ej: 0)
#   io   <tipo>     IO   (STDOUT | STDIN | SLEEP)
#
# El config se lee desde la carpeta del módulo (ej: kernel_memory/kernel_memory.config).
# Editá ese archivo con la IP y puerto correctos antes de correr el script.
#
# Variables de entorno opcionales:
#   NO_BUILD=1    Saltar la compilación (útil si ya compilaste antes)
#
# Ejemplos:
#   ./deploy.sh km
#   ./deploy.sh ms 2048
#   ./deploy.sh swap
#   ./deploy.sh ks /home/utnso/pruebas/PLANI_PRE_0.prc
#   ./deploy.sh cpu 0
#   ./deploy.sh io STDOUT

set -uo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"
NO_BUILD="${NO_BUILD:-0}"

# ─── colores ────────────────────────────────────────────────────────────────
C_OK="\033[0;32m"; C_ERR="\033[0;31m"; C_INFO="\033[0;36m"; C_RST="\033[0m"
log_ok()   { echo -e "${C_OK}[deploy]${C_RST} $*"; }
log_err()  { echo -e "${C_ERR}[deploy] ERROR:${C_RST} $*" >&2; }
log_info() { echo -e "${C_INFO}[deploy]${C_RST} $*"; }

# ─── ayuda ──────────────────────────────────────────────────────────────────
MODULO="${1:-}"
if [ -z "$MODULO" ]; then
    sed -n '2,/^[^#]/p' "$0" | grep '^#' | sed 's/^# \?//'
    exit 1
fi
shift

# ─── compilar módulo + utils ────────────────────────────────────────────────
compilar() {
    local mod="$1"
    if [ "$NO_BUILD" = "1" ]; then
        log_info "NO_BUILD=1 — omitiendo compilación."
        return 0
    fi
    log_info "Compilando utils..."
    make -C "$REPO/utils" --no-print-directory
    log_info "Compilando $mod..."
    make -C "$REPO/$mod" --no-print-directory
    log_ok "Compilación OK."
}

# ─── verificar config ────────────────────────────────────────────────────────
check_config() {
    local cfg="$1"
    if [ ! -f "$cfg" ]; then
        log_err "Config no encontrado: $cfg"
        log_err "Copiá el ejemplo y editá las IPs antes de correr el script."
        exit 1
    fi
    log_info "Usando config: $cfg"
}

# ─── lanzar módulo ──────────────────────────────────────────────────────────
case "$MODULO" in

    km|kernel_memory)
        DIR="$REPO/kernel_memory"
        CFG="$DIR/kernel_memory.config"
        check_config "$CFG"
        compilar kernel_memory
        log_ok "Iniciando Kernel Memory..."
        cd "$DIR"
        exec ./bin/kernel_memory "$CFG"
        ;;

    ms|memory_stick)
        TAMANIO="${1:?Uso: $0 ms <tamaño_en_bytes>}"
        DIR="$REPO/memory_stick"
        CFG="$DIR/memory_stick.config"
        check_config "$CFG"
        compilar memory_stick
        log_ok "Iniciando Memory Stick ($TAMANIO bytes)..."
        cd "$DIR"
        exec ./bin/memory_stick "$CFG" "$TAMANIO"
        ;;

    swap)
        DIR="$REPO/swap"
        CFG="$DIR/swap.config"
        check_config "$CFG"
        compilar swap
        log_ok "Iniciando Swap..."
        cd "$DIR"
        exec ./bin/swap "$CFG"
        ;;

    ks|kernel_scheduler)
        SCRIPT="${1:?Uso: $0 ks <path_script_inicial>}"
        if [[ "$SCRIPT" != /* ]]; then SCRIPT="$(pwd)/$SCRIPT"; fi
        if [ ! -f "$SCRIPT" ]; then
            log_err "Script inicial no encontrado: $SCRIPT"
            exit 1
        fi
        DIR="$REPO/kernel_scheduler"
        CFG="$DIR/kernel_scheduler.config"
        check_config "$CFG"
        compilar kernel_scheduler
        log_ok "Iniciando Kernel Scheduler (script: $SCRIPT)..."
        cd "$DIR"
        exec ./bin/kernel_scheduler "$CFG" "$SCRIPT"
        ;;

    cpu)
        ID="${1:?Uso: $0 cpu <id>}"
        DIR="$REPO/cpu"
        CFG="$DIR/cpu.config"
        check_config "$CFG"
        compilar cpu
        log_ok "Iniciando CPU $ID..."
        cd "$DIR"
        exec ./bin/cpu "$CFG" "$ID"
        ;;

    io)
        TIPO="${1:?Uso: $0 io <STDOUT|STDIN|SLEEP>}"
        if [[ ! "$TIPO" =~ ^(STDOUT|STDIN|SLEEP)$ ]]; then
            log_err "Tipo de IO inválido: '$TIPO'. Debe ser STDOUT, STDIN o SLEEP."
            exit 1
        fi
        DIR="$REPO/io"
        CFG="$DIR/io.config"
        check_config "$CFG"
        compilar io
        log_ok "Iniciando IO $TIPO..."
        cd "$DIR"
        exec ./bin/io "$CFG" "$TIPO"
        ;;

    *)
        log_err "Módulo desconocido: '$MODULO'"
        echo "Módulos válidos: km, ms, swap, ks, cpu, io"
        exit 1
        ;;
esac
