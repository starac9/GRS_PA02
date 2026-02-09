# MT25062
# Makefile for PA02: Network I/O Primitives Analysis
# Roll No: MT25062
#
# Compiles all three implementations (A1, A2, A3).
# Each .c file is fully self-contained (no shared header).
#
# Usage:
#   make all       - Build all binaries
#   make a1        - Build two-copy implementation only
#   make a2        - Build one-copy implementation only
#   make a3        - Build zero-copy implementation only
#   make clean     - Remove all binaries

# ========================= Compiler Settings ==========================
CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -pthread -std=gnu11
LDFLAGS  = -pthread

# ========================= Targets ====================================
A1_SERVER = a1_server
A1_CLIENT = a1_client
A2_SERVER = a2_server
A2_CLIENT = a2_client
A3_SERVER = a3_server
A3_CLIENT = a3_client

ALL_BINS = $(A1_SERVER) $(A1_CLIENT) \
           $(A2_SERVER) $(A2_CLIENT) \
           $(A3_SERVER) $(A3_CLIENT)

# ========================= Build Rules ================================

.PHONY: all a1 a2 a3 clean

all: a1 a2 a3
	@echo "[Makefile] All implementations compiled successfully."

# --- A1: Two-Copy (send/recv) ---
a1: $(A1_SERVER) $(A1_CLIENT)

$(A1_SERVER): MT25062_Part_A1_Server.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo "[Makefile] Built $@"

$(A1_CLIENT): MT25062_Part_A1_Client.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo "[Makefile] Built $@"

# --- A2: One-Copy (sendmsg/iovec) ---
a2: $(A2_SERVER) $(A2_CLIENT)

$(A2_SERVER): MT25062_Part_A2_Server.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo "[Makefile] Built $@"

$(A2_CLIENT): MT25062_Part_A2_Client.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo "[Makefile] Built $@"

# --- A3: Zero-Copy (MSG_ZEROCOPY) ---
a3: $(A3_SERVER) $(A3_CLIENT)

$(A3_SERVER): MT25062_Part_A3_Server.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo "[Makefile] Built $@"

$(A3_CLIENT): MT25062_Part_A3_Client.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo "[Makefile] Built $@"

# --- Cleanup ---
clean:
	rm -f $(ALL_BINS)
	rm -rf perf_output/
	@echo "[Makefile] Cleaned all binaries and perf output."
