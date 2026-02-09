/*
 * MT25062_Part_A3_Client.c
 * Zero-Copy TCP Client (Sender Side)
 * Roll No: MT25062
 *
 * Part A3: Zero-copy implementation using sendmsg() with MSG_ZEROCOPY.
 *
 * ZERO COPIES on the send path:
 *   With MSG_ZEROCOPY, the kernel pins the user-space pages and constructs
 *   sk_buff entries that point directly to user memory. The NIC DMA engine
 *   reads from the user pages without any CPU-mediated copy.
 *
 * Kernel behavior with MSG_ZEROCOPY:
 *   1. Application calls sendmsg() with MSG_ZEROCOPY flag.
 *   2. Kernel pins the user-space pages (get_user_pages).
 *   3. Kernel creates sk_buff with frags pointing to user pages.
 *   4. NIC DMA reads directly from user-space pages.
 *   5. After transmission, kernel sends completion notification
 *      via the socket error queue (SO_EE_ORIGIN_ZEROCOPY).
 *   6. Application must drain the error queue to release page pins.
 *
 * Note: MSG_ZEROCOPY has overhead for small messages due to page pinning
 * and completion notification. Benefits appear for large messages (>10KB).
 *
 * Requirements: Linux kernel >= 4.14, SO_ZEROCOPY socket option.
 *
 * Usage: ./a3_client <server_ip> <port> <msg_size> <threads> <duration>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <errno.h>
#include <signal.h>
#include <linux/errqueue.h>

/* ========================= Constants ================================= */
#define SERVER_PORT  8080
#define NUM_FIELDS   8

/* Fallback definitions for older kernel headers */
#ifndef SO_ZEROCOPY
#define SO_ZEROCOPY 60
#endif

#ifndef MSG_ZEROCOPY
#define MSG_ZEROCOPY 0x4000000
#endif

#ifndef SO_EE_ORIGIN_ZEROCOPY
#define SO_EE_ORIGIN_ZEROCOPY 5
#endif

/* ========================= Structures ================================ */

typedef struct {
    int msg_size;
    int duration;
} config_t;

typedef struct {
    char *fields[NUM_FIELDS];
    int   field_size;
} message_t;

typedef struct {
    int       thread_id;
    char      server_ip[64];
    int       server_port;
    int       msg_size;
    int       duration;
    long long bytes_transferred;
    double    elapsed_time;
    double    avg_latency_us;
} thread_args_t;

/* ========================= Timing Utilities ========================== */

static double get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1e6 + (double)tv.tv_usec;
}

static double get_time_sec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
}

/* ========================= Message Management ======================== */

static message_t *alloc_message(int msg_size) {
    message_t *msg = (message_t *)malloc(sizeof(message_t));
    if (!msg) { perror("malloc message_t"); exit(EXIT_FAILURE); }

    msg->field_size = msg_size / NUM_FIELDS;
    if (msg->field_size <= 0) {
        fprintf(stderr, "Error: msg_size must be >= %d bytes\n", NUM_FIELDS);
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < NUM_FIELDS; i++) {
        msg->fields[i] = (char *)malloc(msg->field_size);
        if (!msg->fields[i]) { perror("malloc field"); exit(EXIT_FAILURE); }
        memset(msg->fields[i], 'A' + i, msg->field_size);
    }
    return msg;
}

static void free_message(message_t *msg) {
    if (!msg) return;
    for (int i = 0; i < NUM_FIELDS; i++) free(msg->fields[i]);
    free(msg);
}

/* ========================= Network Utilities ========================= */

static int connect_to_server(const char *server_ip, int server_port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return -1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &addr.sin_addr) <= 0) {
        perror("inet_pton"); close(sock); return -1;
    }
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect"); close(sock); return -1;
    }
    return sock;
}

/* ========================= Output Utilities ========================== */

static void print_results(const char *impl, int msg_size, int threads,
                           long long total_bytes, double elapsed,
                           double avg_lat) {
    double throughput_gbps = (total_bytes * 8.0) / (elapsed * 1e9);
    printf("RESULT,%s,%d,%d,%.4f,%.2f,%lld,%.4f\n",
           impl, msg_size, threads, throughput_gbps, avg_lat,
           total_bytes, elapsed);
}

/* ========================= Zero-Copy Completion ===================== */
/*
 * drain_completions - Drain MSG_ZEROCOPY completion notifications.
 *
 * After sendmsg(MSG_ZEROCOPY), the kernel sends completion notifications
 * via the socket's error queue. The application MUST drain these to
 * release pinned user-space pages and avoid resource leaks.
 */
static void drain_completions(int sock) {
    struct msghdr   msg   = {0};
    char            cbuf[128];
    struct iovec    iov   = {0};
    char            dummy[1];

    iov.iov_base = dummy;
    iov.iov_len  = sizeof(dummy);

    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cbuf;
    msg.msg_controllen = sizeof(cbuf);

    /* Non-blocking drain of error queue */
    while (1) {
        int ret = recvmsg(sock, &msg, MSG_ERRQUEUE | MSG_DONTWAIT);
        if (ret < 0) break;

        struct cmsghdr *cm = CMSG_FIRSTHDR(&msg);
        while (cm) {
            if (cm->cmsg_level == SOL_IP &&
                cm->cmsg_type  == IP_RECVERR) {
                struct sock_extended_err *serr;
                serr = (struct sock_extended_err *)CMSG_DATA(cm);
                if (serr->ee_origin == SO_EE_ORIGIN_ZEROCOPY) {
                    /* Zero-copy completion confirmed */
                }
            }
            cm = CMSG_NXTHDR(&msg, cm);
        }

        msg.msg_controllen = sizeof(cbuf);
    }
}

/* ========================= Client Thread ============================ */
/*
 * client_thread - Thread function using sendmsg() with MSG_ZEROCOPY.
 *
 * Key differences from A1/A2:
 *   - Socket has SO_ZEROCOPY enabled.
 *   - sendmsg() called with MSG_ZEROCOPY flag.
 *   - Must drain completion notifications from error queue.
 *   - No user-space copy AND no kernel copy on send path.
 */
static void *client_thread(void *arg) {
    thread_args_t *targs = (thread_args_t *)arg;

    /* --- Step 1: Connect to server --- */
    int sock = connect_to_server(targs->server_ip, targs->server_port);
    if (sock < 0) {
        fprintf(stderr, "[Client T%d] Connection failed\n", targs->thread_id);
        return NULL;
    }

    /* --- Step 2: Enable SO_ZEROCOPY on socket --- */
    /*
     * SO_ZEROCOPY must be set before using MSG_ZEROCOPY flag.
     * Tells the kernel the application will handle page pinning
     * and completion notifications.
     */
    int val = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_ZEROCOPY, &val, sizeof(val)) < 0) {
        perror("setsockopt SO_ZEROCOPY");
        fprintf(stderr, "[Client T%d] Zero-copy not supported, falling back\n",
                targs->thread_id);
    }

    /* --- Step 3: Send configuration to server --- */
    config_t config;
    config.msg_size = targs->msg_size;
    config.duration = targs->duration;
    if (send(sock, &config, sizeof(config), 0) != sizeof(config)) {
        fprintf(stderr, "[Client T%d] Failed to send config\n", targs->thread_id);
        close(sock);
        return NULL;
    }

    /* --- Step 4: Allocate message with 8 heap-allocated string fields --- */
    message_t *msg = alloc_message(targs->msg_size);

    /* --- Step 5: Set up iovec for scatter-gather I/O --- */
    struct iovec iov[NUM_FIELDS];
    for (int i = 0; i < NUM_FIELDS; i++) {
        iov[i].iov_base = msg->fields[i];
        iov[i].iov_len  = msg->field_size;
    }

    struct msghdr mhdr;
    memset(&mhdr, 0, sizeof(mhdr));
    mhdr.msg_iov    = iov;
    mhdr.msg_iovlen = NUM_FIELDS;

    /* --- Step 6: Send loop for 'duration' seconds --- */
    double    start_time    = get_time_sec();
    long long total_bytes   = 0;
    long long msg_count     = 0;
    double    total_latency = 0.0;
    int       drain_counter = 0;

    while (get_time_sec() - start_time < targs->duration) {
        /*
         * ZERO COPY (MSG_ZEROCOPY):
         * The kernel pins the user-space pages referenced by the iovec
         * array. It creates sk_buff fragments pointing directly to user
         * memory. The NIC DMA engine reads from these pages without any
         * CPU-mediated copy.
         *
         * After the NIC finishes DMA, the kernel sends a completion
         * notification via the socket error queue, allowing the
         * application to safely modify or free the buffers.
         *
         * No user-space copy + no kernel copy = zero copies.
         */
        double  msg_start = get_time_us();
        ssize_t sent      = sendmsg(sock, &mhdr, MSG_ZEROCOPY);
        double  msg_end   = get_time_us();

        if (sent < 0) {
            if (errno == ENOBUFS) {
                /* Kernel ran out of pinnable pages; drain completions */
                drain_completions(sock);
                continue;
            }
            if (errno == EPIPE || errno == ECONNRESET) break;
            if (errno == EINTR) continue;
            perror("sendmsg MSG_ZEROCOPY");
            break;
        }

        total_bytes   += sent;
        msg_count     += 1;
        total_latency += (msg_end - msg_start);

        /*
         * Periodically drain completion notifications to release
         * pinned pages and avoid ENOBUFS. Every 64 messages.
         */
        if (++drain_counter >= 64) {
            drain_completions(sock);
            drain_counter = 0;
        }
    }

    /* Final drain of remaining completions */
    drain_completions(sock);

    double elapsed = get_time_sec() - start_time;

    /* --- Step 7: Record metrics --- */
    targs->bytes_transferred = total_bytes;
    targs->elapsed_time      = elapsed;
    targs->avg_latency_us    = (msg_count > 0) ? (total_latency / msg_count) : 0.0;

    printf("[Client T%d] Sent %lld bytes in %.2f sec (%lld msgs, avg_lat=%.2f us)\n",
           targs->thread_id, total_bytes, elapsed, msg_count,
           targs->avg_latency_us);

    free_message(msg);
    close(sock);
    return NULL;
}

/* ========================= Main ====================================== */
int main(int argc, char *argv[]) {
    if (argc < 6) {
        fprintf(stderr, "Usage: %s <server_ip> <port> <msg_size> <threads> <duration>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    const char *server_ip = argv[1];
    int         port      = atoi(argv[2]);
    int         msg_size  = atoi(argv[3]);
    int         threads   = atoi(argv[4]);
    int         duration  = atoi(argv[5]);

    printf("[Client] Zero-Copy (MSG_ZEROCOPY) Implementation\n");
    printf("[Client] Server=%s:%d, MsgSize=%d, Threads=%d, Duration=%d sec\n",
           server_ip, port, msg_size, threads, duration);

    signal(SIGPIPE, SIG_IGN);

    pthread_t     *tids  = malloc(sizeof(pthread_t)     * threads);
    thread_args_t *targs = malloc(sizeof(thread_args_t) * threads);
    if (!tids || !targs) { perror("malloc threads"); return EXIT_FAILURE; }

    for (int i = 0; i < threads; i++) {
        targs[i].thread_id = i;
        strncpy(targs[i].server_ip, server_ip, sizeof(targs[i].server_ip) - 1);
        targs[i].server_ip[sizeof(targs[i].server_ip) - 1] = '\0';
        targs[i].server_port       = port;
        targs[i].msg_size          = msg_size;
        targs[i].duration          = duration;
        targs[i].bytes_transferred = 0;
        targs[i].elapsed_time      = 0.0;
        targs[i].avg_latency_us    = 0.0;

        if (pthread_create(&tids[i], NULL, client_thread, &targs[i]) != 0) {
            perror("pthread_create");
            return EXIT_FAILURE;
        }
    }

    for (int i = 0; i < threads; i++) pthread_join(tids[i], NULL);

    long long total_bytes   = 0;
    double    max_elapsed   = 0.0;
    double    total_latency = 0.0;

    for (int i = 0; i < threads; i++) {
        total_bytes   += targs[i].bytes_transferred;
        total_latency += targs[i].avg_latency_us;
        if (targs[i].elapsed_time > max_elapsed)
            max_elapsed = targs[i].elapsed_time;
    }

    double avg_latency = total_latency / threads;
    print_results("zero_copy", msg_size, threads,
                  total_bytes, max_elapsed, avg_latency);

    free(tids);
    free(targs);
    return 0;
}
