"""
MT25062_Part_D_Plots.py
Plotting Script for PA02: Network I/O Primitives Analysis
Roll No: MT25062

Generates 4 required plots using matplotlib with hardcoded values:
  1. Throughput vs Message Size
  2. Latency vs Thread Count
  3. Cache Misses vs Message Size
  4. CPU Cycles per Byte Transferred

System Configuration:
  - Update SYSTEM_CONFIG below with your actual system specs.
  - Replace placeholder data arrays with values from your CSV results.

Usage: python3 MT25062_Part_D_Plots.py
"""

import matplotlib.pyplot as plt
import numpy as np

# ========================= System Configuration ======================
SYSTEM_CONFIG = (
    "System: Linux | CPU: (update with your CPU) | "
    "RAM: (update with your RAM) | Kernel: (update with kernel version)"
)

# ========================= Experiment Parameters =====================
MSG_SIZES    = [256, 1024, 4096, 16384, 65536]       # bytes
THREAD_COUNTS = [1, 2, 4, 8]

# ========================= Hardcoded Results ==========================
# IMPORTANT: Replace these placeholder values with your actual
# experimental results from the CSV file.
# Format: Each array corresponds to MSG_SIZES order.

# --- Throughput (Gbps) for each message size, with thread_count=4 ---
# (One value per message size)
throughput_two_copy  = [0.15, 0.58, 1.85, 4.20, 6.50]
throughput_one_copy  = [0.18, 0.72, 2.30, 5.10, 7.80]
throughput_zero_copy = [0.10, 0.50, 2.10, 5.80, 9.20]

# --- Latency (us) for each thread count, with msg_size=4096 ---
# (One value per thread count)
latency_two_copy  = [12.5, 14.2, 18.7, 28.3]
latency_one_copy  = [10.1, 11.8, 15.4, 23.6]
latency_zero_copy = [15.2, 16.8, 19.5, 26.1]

# --- L1 Cache Misses for each message size, with thread_count=4 ---
l1_misses_two_copy  = [45000, 120000, 380000, 1200000, 4500000]
l1_misses_one_copy  = [32000,  85000, 260000,  850000, 3200000]
l1_misses_zero_copy = [28000,  70000, 210000,  680000, 2500000]

# --- LLC (Last Level Cache) Misses for each message size, with thread_count=4 ---
llc_misses_two_copy  = [5000, 18000,  65000, 250000,  980000]
llc_misses_one_copy  = [3500, 12000,  42000, 160000,  620000]
llc_misses_zero_copy = [2000,  8000,  28000, 105000,  400000]

# --- CPU Cycles for each message size, with thread_count=4 ---
cycles_two_copy  = [5000000, 15000000, 45000000, 150000000, 500000000]
cycles_one_copy  = [4000000, 11000000, 32000000, 105000000, 350000000]
cycles_zero_copy = [4500000, 12000000, 30000000,  85000000, 280000000]

# --- Total Bytes Transferred for cycles-per-byte calculation ---
# (for thread_count=4, one value per message size)
bytes_two_copy  = [1500000, 5800000, 18500000, 42000000, 65000000]
bytes_one_copy  = [1800000, 7200000, 23000000, 51000000, 78000000]
bytes_zero_copy = [1000000, 5000000, 21000000, 58000000, 92000000]


# ========================= Plot Styling ===============================
plt.rcParams.update({
    'figure.figsize': (10, 7),
    'font.size': 12,
    'axes.titlesize': 14,
    'axes.labelsize': 13,
    'legend.fontsize': 11,
    'xtick.labelsize': 11,
    'ytick.labelsize': 11,
    'lines.linewidth': 2,
    'lines.markersize': 8,
})

COLORS = {
    'two_copy':  '#1f77b4',   # Blue
    'one_copy':  '#ff7f0e',   # Orange
    'zero_copy': '#2ca02c',   # Green
}

MARKERS = {
    'two_copy':  'o',
    'one_copy':  's',
    'zero_copy': '^',
}


# ========================= Plot 1: Throughput vs Message Size =========
def plot_throughput_vs_msgsize():
    """Plot throughput (Gbps) vs message size for all 3 implementations."""
    fig, ax = plt.subplots()

    ax.plot(MSG_SIZES, throughput_two_copy,
            color=COLORS['two_copy'], marker=MARKERS['two_copy'],
            label='Two-Copy (send/recv)')
    ax.plot(MSG_SIZES, throughput_one_copy,
            color=COLORS['one_copy'], marker=MARKERS['one_copy'],
            label='One-Copy (sendmsg/iovec)')
    ax.plot(MSG_SIZES, throughput_zero_copy,
            color=COLORS['zero_copy'], marker=MARKERS['zero_copy'],
            label='Zero-Copy (MSG_ZEROCOPY)')

    ax.set_xscale('log', base=2)
    ax.set_xlabel('Message Size (bytes)')
    ax.set_ylabel('Throughput (Gbps)')
    ax.set_title('Throughput vs Message Size (Threads=4)')
    ax.set_xticks(MSG_SIZES)
    ax.set_xticklabels([str(s) for s in MSG_SIZES])
    ax.legend(loc='upper left')
    ax.grid(True, alpha=0.3)

    # Add system config annotation
    fig.text(0.5, 0.01, SYSTEM_CONFIG, ha='center', fontsize=9,
             style='italic', color='gray')

    plt.tight_layout(rect=[0, 0.03, 1, 1])
    plt.savefig('plot_throughput_vs_msgsize.png', dpi=150, bbox_inches='tight')
    plt.show()
    print("[Plot 1] Throughput vs Message Size saved.")


# ========================= Plot 2: Latency vs Thread Count ===========
def plot_latency_vs_threads():
    """Plot latency (us) vs thread count for all 3 implementations."""
    fig, ax = plt.subplots()

    ax.plot(THREAD_COUNTS, latency_two_copy,
            color=COLORS['two_copy'], marker=MARKERS['two_copy'],
            label='Two-Copy (send/recv)')
    ax.plot(THREAD_COUNTS, latency_one_copy,
            color=COLORS['one_copy'], marker=MARKERS['one_copy'],
            label='One-Copy (sendmsg/iovec)')
    ax.plot(THREAD_COUNTS, latency_zero_copy,
            color=COLORS['zero_copy'], marker=MARKERS['zero_copy'],
            label='Zero-Copy (MSG_ZEROCOPY)')

    ax.set_xlabel('Thread Count')
    ax.set_ylabel('Average Latency (\u00b5s)')
    ax.set_title('Latency vs Thread Count (Message Size=4096 B)')
    ax.set_xticks(THREAD_COUNTS)
    ax.legend(loc='upper left')
    ax.grid(True, alpha=0.3)

    fig.text(0.5, 0.01, SYSTEM_CONFIG, ha='center', fontsize=9,
             style='italic', color='gray')

    plt.tight_layout(rect=[0, 0.03, 1, 1])
    plt.savefig('plot_latency_vs_threads.png', dpi=150, bbox_inches='tight')
    plt.show()
    print("[Plot 2] Latency vs Thread Count saved.")


# ========================= Plot 3: Cache Misses vs Message Size =======
def plot_cache_misses_vs_msgsize():
    """Plot L1 and LLC cache misses vs message size."""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))

    # --- L1 Cache Misses ---
    ax1.plot(MSG_SIZES, l1_misses_two_copy,
             color=COLORS['two_copy'], marker=MARKERS['two_copy'],
             label='Two-Copy')
    ax1.plot(MSG_SIZES, l1_misses_one_copy,
             color=COLORS['one_copy'], marker=MARKERS['one_copy'],
             label='One-Copy')
    ax1.plot(MSG_SIZES, l1_misses_zero_copy,
             color=COLORS['zero_copy'], marker=MARKERS['zero_copy'],
             label='Zero-Copy')

    ax1.set_xscale('log', base=2)
    ax1.set_yscale('log')
    ax1.set_xlabel('Message Size (bytes)')
    ax1.set_ylabel('L1 Data Cache Misses')
    ax1.set_title('L1 Cache Misses vs Message Size')
    ax1.set_xticks(MSG_SIZES)
    ax1.set_xticklabels([str(s) for s in MSG_SIZES])
    ax1.legend()
    ax1.grid(True, alpha=0.3)

    # --- LLC Cache Misses ---
    ax2.plot(MSG_SIZES, llc_misses_two_copy,
             color=COLORS['two_copy'], marker=MARKERS['two_copy'],
             label='Two-Copy')
    ax2.plot(MSG_SIZES, llc_misses_one_copy,
             color=COLORS['one_copy'], marker=MARKERS['one_copy'],
             label='One-Copy')
    ax2.plot(MSG_SIZES, llc_misses_zero_copy,
             color=COLORS['zero_copy'], marker=MARKERS['zero_copy'],
             label='Zero-Copy')

    ax2.set_xscale('log', base=2)
    ax2.set_yscale('log')
    ax2.set_xlabel('Message Size (bytes)')
    ax2.set_ylabel('LLC (Last Level Cache) Misses')
    ax2.set_title('LLC Cache Misses vs Message Size')
    ax2.set_xticks(MSG_SIZES)
    ax2.set_xticklabels([str(s) for s in MSG_SIZES])
    ax2.legend()
    ax2.grid(True, alpha=0.3)

    fig.text(0.5, 0.01, SYSTEM_CONFIG, ha='center', fontsize=9,
             style='italic', color='gray')

    plt.tight_layout(rect=[0, 0.03, 1, 1])
    plt.savefig('plot_cache_misses_vs_msgsize.png', dpi=150, bbox_inches='tight')
    plt.show()
    print("[Plot 3] Cache Misses vs Message Size saved.")


# ========================= Plot 4: CPU Cycles per Byte ================
def plot_cycles_per_byte():
    """Plot CPU cycles per byte transferred vs message size."""
    fig, ax = plt.subplots()

    # Calculate cycles per byte
    cpb_two_copy  = [c / b if b > 0 else 0
                     for c, b in zip(cycles_two_copy, bytes_two_copy)]
    cpb_one_copy  = [c / b if b > 0 else 0
                     for c, b in zip(cycles_one_copy, bytes_one_copy)]
    cpb_zero_copy = [c / b if b > 0 else 0
                     for c, b in zip(cycles_zero_copy, bytes_zero_copy)]

    ax.plot(MSG_SIZES, cpb_two_copy,
            color=COLORS['two_copy'], marker=MARKERS['two_copy'],
            label='Two-Copy (send/recv)')
    ax.plot(MSG_SIZES, cpb_one_copy,
            color=COLORS['one_copy'], marker=MARKERS['one_copy'],
            label='One-Copy (sendmsg/iovec)')
    ax.plot(MSG_SIZES, cpb_zero_copy,
            color=COLORS['zero_copy'], marker=MARKERS['zero_copy'],
            label='Zero-Copy (MSG_ZEROCOPY)')

    ax.set_xscale('log', base=2)
    ax.set_xlabel('Message Size (bytes)')
    ax.set_ylabel('CPU Cycles per Byte')
    ax.set_title('CPU Cycles per Byte Transferred (Threads=4)')
    ax.set_xticks(MSG_SIZES)
    ax.set_xticklabels([str(s) for s in MSG_SIZES])
    ax.legend(loc='upper right')
    ax.grid(True, alpha=0.3)

    fig.text(0.5, 0.01, SYSTEM_CONFIG, ha='center', fontsize=9,
             style='italic', color='gray')

    plt.tight_layout(rect=[0, 0.03, 1, 1])
    plt.savefig('plot_cycles_per_byte.png', dpi=150, bbox_inches='tight')
    plt.show()
    print("[Plot 4] CPU Cycles per Byte saved.")


# ========================= Main ======================================
if __name__ == "__main__":
    print("=" * 60)
    print("PA02: Network I/O Primitives - Plot Generation")
    print("Roll No: MT25062")
    print("=" * 60)
    print()
    print("NOTE: Replace placeholder values with your actual")
    print("      experimental results before generating final plots!")
    print()

    plot_throughput_vs_msgsize()
    plot_latency_vs_threads()
    plot_cache_misses_vs_msgsize()
    plot_cycles_per_byte()

    print()
    print("All plots generated successfully.")
    print("Include these plots in your report (not as separate files).")
