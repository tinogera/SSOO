#!/usr/bin/env bash
#
# deploy.sh — Levanta todos los módulos del TP en orden.
#
# Uso:
#   ./deploy.sh <script_inicial> [directorio_configs]
#
# Variables de entorno opcionales (antes del comando):
#   MS_SIZE=1024      Tamaño del Memory Stick en bytes       (default: 1024)
#   N_MS=1            Cantidad de Memory Sticks a levantar   (default: 1)
#   N_CPUS=1          Cantidad de CPUs a levantar            (default: 1)
#   IO_TIPOS="STDOUT STDIN SLEEP"  IOs a levantar            (default: los tres)
#   NO_BUILD=1        Saltar la compilación                  (default: compila)
#
# Ejemplos:
#   ./deploy.sh /home/utnso/pruebas/PLANI_PRE_0.prc
#   ./deploy.sh PLANI_PRE_0.prc ./configs/plani_pre
#   N_CPUS=2 MS_SIZE=2048 ./deploy.sh MEMORIA_PRE_0.prc ./configs/memoria_pre

set -uo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"
PIDS=()
MODULOS=()

# ─── colores ───────────────────────────────────────────────────────────────
C_OK="\033[0;32m"
C_ERR="\033[0;31m"
C_INFO="\033[0;36m"
C_WARN="\033[0;33m"
C_RST="\033[0m"

log_ok()   { echo -e "${C_OK}[deploy]${C_RST} $*"; }
log_err()  { echo -e "${C_ERR}[deploy] ERROR:${C_RST} $*" >&2; }
log_info() { echo -e "${C_INFO}[deploy]${C_RST} $*"; }
log_warn() { echo -e "${C_WARN}[deploy] WARN:${C_RST} $*"; }

# ─── parámetros ────────────────────────────────────────────────────────────
SCRIPT_INICIAL="${1:-}"
# Resolver CONFIG_DIR a ruta absoluta si se proporcionó
if [ -n "${2:-}" ]; then
    CONFIG_DIR="$(cd "${2}" && pwd)"
else
    CONFIG_DIR=""
fi

if [ -z "$SCRIPT_INICIAL" ]; then
    echo "Uso: $0 <script_inicial> [directorio_configs]"
    echo ""
    echo "  script_inicial    Path al .prc del proceso 0 (absoluto o relativo)"
    echo "  directorio_configs  Carpeta con km.config, ks.config, ms.config,"
    echo "                      swap.config, cpu.config, io.config"
    echo "                      (si se omite se usan los configs de cada módulo)"
    exit 1
fi

MS_SIZE="${MS_SIZE:-1024}"
N_MS="${N_MS:-1}"
N_CPUS="${N_CPUS:-1}"
IO_TIPOS="${IO_TIPOS:-STDOUT STDIN SLEEP}"
NO_BUILD="${NO_BUILD:-0}"

# Resolver ruta del script si es relativa
if [[ "$SCRIPT_INICIAL" != /* ]]; then
    SCRIPT_INICIAL="$(pwd)/$SCRIPT_INICIAL"
fi

if [ ! -f "$SCRIPT_INICIAL" ]; then
    log_err "No se encontró el script inicial: $SCRIPT_INICIAL"
    exit 1
fi

# ─── rutas de configs ───────────────────────────────────────────────────────
resolve_config() {
    local modulo="$1"
    local nombre="$2"
    if [ -n "$CONFIG_DIR" ]; then
        echo "$CONFIG_DIR/$nombre.config"
    else
        echo "$REPO/$modulo/$modulo.config"
    fi
}

KM_CONFIG="$(resolve_config kernel_memory km)"
KS_CONFIG="$(resolve_config kernel_scheduler ks)"
MS_CONFIG="$(resolve_config memory_stick ms)"
SWAP_CONFIG="$(resolve_config swap swap)"
CPU_CONFIG="$(resolve_config cpu cpu)"
IO_CONFIG="$(resolve_config io io)"

# Verificar que todos los configs existen
for cfg in "$KM_CONFIG" "$KS_CONFIG" "$MS_CONFIG" "$SWAP_CONFIG" "$CPU_CONFIG" "$IO_CONFIG"; do
    if [ ! -f "$cfg" ]; then
        log_err "Config no encontrado: $cfg"
        exit 1
    fi
done

# ─── cleanup ────────────────────────────────────────────────────────────────
cleanup() {
    echo ""
    log_info "Deteniendo todos los módulos (${#PIDS[@]} procesos)..."
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    # Esperar hasta 3s a que terminen
    local deadline=$((SECONDS + 3))
    while [ ${#PIDS[@]} -gt 0 ] && [ $SECONDS -lt $deadline ]; do
        local vivos=()
        for pid in "${PIDS[@]}"; do
            kill -0 "$pid" 2>/dev/null && vivos+=("$pid") || true
        done
        PIDS=("${vivos[@]}")
        sleep 0.2
    done
    # Forzar si quedó alguno
    for pid in "${PIDS[@]}"; do
        kill -9 "$pid" 2>/dev/null || true
    done
    log_ok "Sistema detenido."
}
trap cleanup EXIT INT TERM

# ─── compilar ───────────────────────────────────────────────────────────────
if [ "$NO_BUILD" = "0" ]; then
    log_info "Compilando todos los módulos..."
    for mod in utils kernel_memory memory_stick swap kernel_scheduler cpu io; do
        if ! make -C "$REPO/$mod" --no-print-directory 2>&1 | grep -E "^(Error|.*error:)" ; then
            log_ok "  $mod OK"
        fi
    done
fi

# ─── función auxiliar para levantar un módulo ───────────────────────────────
iniciar() {
    local nombre="$1"
    local dir="$2"
    shift 2
    log_info "Iniciando $nombre..."
    (cd "$dir" && exec "$@") &
    local pid=$!
    PIDS+=("$pid")
    MODULOS+=("$nombre:$pid")
}

# ─── arrancar en orden ──────────────────────────────────────────────────────
echo ""
log_info "Script inicial:  $SCRIPT_INICIAL"
log_info "Config dir:      ${CONFIG_DIR:-<defaults de cada módulo>}"
log_info "Memory Sticks:   $N_MS × $MS_SIZE bytes"
log_info "CPUs:            $N_CPUS"
log_info "IOs:             $IO_TIPOS"
echo ""

iniciar "KernelMemory" "$REPO/kernel_memory" ./bin/kernel_memory "$KM_CONFIG"
sleep 0.5

for i in $(seq 1 "$N_MS"); do
    iniciar "MemoryStick-$i" "$REPO/memory_stick" ./bin/memory_stick "$MS_CONFIG" "$MS_SIZE"
    sleep 0.3
done

iniciar "Swap" "$REPO/swap" ./bin/swap "$SWAP_CONFIG"
sleep 0.4

iniciar "KernelScheduler" "$REPO/kernel_scheduler" ./bin/kernel_scheduler "$KS_CONFIG" "$SCRIPT_INICIAL"
sleep 0.5

for i in $(seq 0 $((N_CPUS - 1))); do
    iniciar "CPU-$i" "$REPO/cpu" ./bin/cpu "$CPU_CONFIG" "$i"
    sleep 0.2
done

for tipo in $IO_TIPOS; do
    iniciar "IO-$tipo" "$REPO/io" ./bin/io "$IO_CONFIG" "$tipo"
    sleep 0.1
done

# ─── resumen ────────────────────────────────────────────────────────────────
echo ""
log_ok "Sistema levantado:"
for m in "${MODULOS[@]}"; do
    nombre="${m%%:*}"
    pid="${m##*:}"
    printf "  %-20s PID %s\n" "$nombre" "$pid"
done
echo ""
log_info "Logs en cada módulo: kernel_memory/kernel_memory.log, etc."
log_info "Presioná Ctrl+C para detener todo."
echo ""

wait
