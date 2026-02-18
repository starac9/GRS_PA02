#!/bin/bash
# MT25062
# MT25062_Part_C_Experiment.sh
# Automated Experiment Script for PA02: Network I/O Primitives Analysis
# Roll No: MT25062
#
# This script:
#   1. Sets up network namespaces (ns_server, ns_client) with a veth pair.
#   2. Compiles all three implementations (A1, A2, A3).
#   3. Runs experiments across message sizes and thread counts.
#   4. Collects perf stat profiling output automatically.
#   5. Stores results in CSV format.
#
# No manual intervention required after script starts.
# Re-running the script will clean up and restart experiments.
#
# Usage: sudo bash MT25062_Part_C_Experiment.sh
# Note:  Requires root privileges for network namespace management and perf.

# NOTE: Removed 'set -e' because background server processes launched via
# 'sudo ip netns exec' cause PID tracking issues that trigger false errors.

# ========================= Configuration ==============================
SERVER_IP="10.0.0.1"
CLIENT_IP="10.0.0.2"
PORT=8080
DURATION=2          # seconds per experiment
WAIT_SERVER=2          # seconds to wait for server startup

# Experiment parameters (at least 4 each as required)
MSG_SIZES=(256 1024 4096 16384 65536)
THREAD_COUNTS=(1 2 4 8)

# Implementation names and binaries
IMPLS=("two_copy" "one_copy" "zero_copy")
SERVER_BINS=("a1_server" "a2_server" "a3_server")
CLIENT_BINS=("a1_client" "a2_client" "a3_client")

# Output files
CSV_FILE="MT25062_Part_B_Results.csv"
PERF_DIR="perf_output"

# ========================= Utility Functions ==========================

log_info()  { echo "[INFO]  $(date '+%H:%M:%S') $*"; }
log_error() { echo "[ERROR] $(date '+%H:%M:%S') $*" >&2; }

# cleanup - Remove network namespaces and kill leftover processes
cleanup() {
    log_info "Cleaning up network namespaces and processes..."
    # Kill any leftover server processes
    sudo ip netns exec ns_server kill -9 $(sudo ip netns exec ns_server pgrep -f "a[123]_server" 2>/dev/null) 2>/dev/null || true
    # Delete namespaces (also removes veth pair)
    sudo ip netns del ns_server 2>/dev/null || true
    sudo ip netns del ns_client 2>/dev/null || true
    log_info "Cleanup complete."
}

# setup_namespaces - Create network namespaces with veth pair
setup_namespaces() {
    log_info "Setting up network namespaces..."

    # Clean any existing setup
    cleanup

    # Create two network namespaces
    sudo ip netns add ns_server
    sudo ip netns add ns_client

    # Create virtual ethernet pair
    sudo ip link add veth_srv type veth peer name veth_cli

    # Move each end into its namespace
    sudo ip link set veth_srv netns ns_server
    sudo ip link set veth_cli netns ns_client

    # Configure IP addresses
    sudo ip netns exec ns_server ip addr add ${SERVER_IP}/24 dev veth_srv
    sudo ip netns exec ns_client ip addr add ${CLIENT_IP}/24 dev veth_cli

    # Bring interfaces and loopback up
    sudo ip netns exec ns_server ip link set veth_srv up
    sudo ip netns exec ns_server ip link set lo up
    sudo ip netns exec ns_client ip link set veth_cli up
    sudo ip netns exec ns_client ip link set lo up

    # Verify connectivity
    if sudo ip netns exec ns_client ping -c 1 -W 2 ${SERVER_IP} > /dev/null 2>&1; then
        log_info "Network namespaces configured successfully."
    else
        log_error "Namespace connectivity check failed!"
        exit 1
    fi
}

# compile_all - Compile all implementations using make
compile_all() {
    log_info "Compiling all implementations..."
    make clean
    make all
    log_info "Compilation complete."
}

# ========================= Experiment Runner ==========================

# run_experiment - Run a single experiment with given parameters
# Args: $1=impl_index, $2=msg_size, $3=thread_count
run_experiment() {
    local impl_idx=$1
    local msg_size=$2
    local threads=$3
    local impl_name="${IMPLS[$impl_idx]}"
    local server_bin="${SERVER_BINS[$impl_idx]}"
    local client_bin="${CLIENT_BINS[$impl_idx]}"
    local perf_file="${PERF_DIR}/${impl_name}_msg${msg_size}_thr${threads}_perf.txt"

    log_info "Running: impl=${impl_name}, msg_size=${msg_size}, threads=${threads}"

    # Start server in ns_server namespace (background)
    sudo ip netns exec ns_server ./${server_bin} ${PORT} > /dev/null 2>&1 &
    sleep ${WAIT_SERVER}

    # Verify server is running inside the namespace
    if ! sudo ip netns exec ns_server pgrep -f "${server_bin}" > /dev/null 2>&1; then
        log_error "Server failed to start for ${impl_name}"
        return 1
    fi

    # Run client in ns_client namespace with perf stat
    # Capture perf output to file and client output to variable
    local client_output
    client_output=$(sudo ip netns exec ns_client \
        perf stat -e cycles,cache-references,cache-misses,L1-dcache-load-misses,LLC-load-misses,context-switches \
        -o "${perf_file}" \
        ./${client_bin} ${SERVER_IP} ${PORT} ${msg_size} ${threads} ${DURATION} 2>&1 | \
        grep "^RESULT" || echo "RESULT,${impl_name},${msg_size},${threads},0,0,0,0")

    # Wait briefly for output flush
    sleep 1

    # Kill server inside the namespace
    sudo ip netns exec ns_server pkill -TERM -f "${server_bin}" 2>/dev/null || true
    sleep 1

    # Parse perf output
    local cycles=$(grep "cycles" "${perf_file}" 2>/dev/null | head -1 | awk '{gsub(/,/,"",$1); print $1}')
    local cache_refs=$(grep "cache-references" "${perf_file}" 2>/dev/null | head -1 | awk '{gsub(/,/,"",$1); print $1}')
    local cache_misses=$(grep "cache-misses" "${perf_file}" 2>/dev/null | head -1 | awk '{gsub(/,/,"",$1); print $1}')
    local l1_misses=$(grep "L1-dcache-load-misses" "${perf_file}" 2>/dev/null | head -1 | awk '{gsub(/,/,"",$1); print $1}')
    local llc_misses=$(grep "LLC-load-misses" "${perf_file}" 2>/dev/null | head -1 | awk '{gsub(/,/,"",$1); print $1}')
    local ctx_switches=$(grep "context-switches" "${perf_file}" 2>/dev/null | head -1 | awk '{gsub(/,/,"",$1); print $1}')

    # Set defaults for missing values
    cycles=${cycles:-0}
    cache_refs=${cache_refs:-0}
    cache_misses=${cache_misses:-0}
    l1_misses=${l1_misses:-0}
    llc_misses=${llc_misses:-0}
    ctx_switches=${ctx_switches:-0}

    # Parse client RESULT line
    local throughput=$(echo "${client_output}" | awk -F',' '{print $5}')
    local latency=$(echo "${client_output}" | awk -F',' '{print $6}')
    local total_bytes=$(echo "${client_output}" | awk -F',' '{print $7}')
    local elapsed=$(echo "${client_output}" | awk -F',' '{print $8}')

    throughput=${throughput:-0}
    latency=${latency:-0}
    total_bytes=${total_bytes:-0}
    elapsed=${elapsed:-0}

    # Write to CSV
    echo "${impl_name},${msg_size},${threads},${throughput},${latency},${cycles},${l1_misses},${llc_misses},${cache_refs},${cache_misses},${ctx_switches},${total_bytes},${elapsed}" >> "${CSV_FILE}"

    log_info "  Throughput=${throughput} Gbps, Latency=${latency} us, Cycles=${cycles}"
}

# ========================= Main ======================================

main() {
    log_info "=========================================="
    log_info " PA02: Network I/O Experiment Runner"
    log_info " Roll No: MT25062"
    log_info "=========================================="

    # Check for root privileges
    if [ "$(id -u)" -ne 0 ]; then
        log_error "This script requires root privileges. Run with sudo."
        exit 1
    fi

    # Check for perf availability
    if ! command -v perf &> /dev/null; then
        log_error "'perf' tool not found. Install with: sudo apt install linux-tools-common linux-tools-\$(uname -r)"
        exit 1
    fi

    # Compile all implementations
    compile_all

    # Set up network namespaces
    setup_namespaces

    # Create perf output directory
    mkdir -p "${PERF_DIR}"

    # Initialize CSV file with header
    echo "implementation,msg_size,threads,throughput_gbps,latency_us,cpu_cycles,l1_cache_misses,llc_cache_misses,cache_references,cache_misses,context_switches,total_bytes,elapsed_sec" > "${CSV_FILE}"

    # Total experiments count
    local total_exp=$(( ${#IMPLS[@]} * ${#MSG_SIZES[@]} * ${#THREAD_COUNTS[@]} ))
    local exp_num=0

    log_info "Running ${total_exp} experiments..."
    log_info "Message sizes: ${MSG_SIZES[*]}"
    log_info "Thread counts: ${THREAD_COUNTS[*]}"
    log_info "Duration per experiment: ${DURATION} sec"

    # Run all experiments
    for impl_idx in $(seq 0 $(( ${#IMPLS[@]} - 1 ))); do
        for msg_size in "${MSG_SIZES[@]}"; do
            for threads in "${THREAD_COUNTS[@]}"; do
                exp_num=$((exp_num + 1))
                log_info "--- Experiment ${exp_num}/${total_exp} ---"
                run_experiment ${impl_idx} ${msg_size} ${threads}
                # Brief pause between experiments
                sleep 2
            done
        done
    done

    log_info "=========================================="
    log_info "All experiments complete!"
    log_info "Results saved to: ${CSV_FILE}"
    log_info "Perf logs saved to: ${PERF_DIR}/"
    log_info "=========================================="

    # Print summary table
    echo ""
    echo "=== Results Summary ==="
    column -t -s',' "${CSV_FILE}" | head -1
    echo "---"
    column -t -s',' "${CSV_FILE}" | tail -n +2

    # Cleanup namespaces
    cleanup
}

# Trap to ensure cleanup on exit
trap cleanup EXIT

# Run main
main "$@"
