/*
 * MT25062_Part_A2_Client.c
 * One-Copy TCP Client (Sender Side)
 * Roll No: MT25062
 *
 * Part A2: One-copy implementation using sendmsg() with scatter-gather I/O.
 * The client uses struct iovec to point directly to each of the 8
 * dynamically allocated message fields, avoiding user-space serialization.
 *
 * ONE COPY on the send path:
 *   ELIMINATED Copy (User-space): No serialization of 8 fields
 *     into a contiguous buffer. The iovec array references
 *     each field's heap pointer directly.
 *   Remaining Copy (Kernel): sendmsg() copies from scattered user
 *     buffers (via iovec) into kernel socket buffer (sk_buff).
 *
 * Comparison with A1 (two-copy):
 *   A1: memcpy(fields -> buf) + send(buf -> kernel) = 2 copies
 *   A2: sendmsg(fields -> kernel via iovec)          = 1 copy
 *   The user-space serialization copy is explicitly eliminated.
 *
 * Usage: ./a2_client <server_ip> <port> <msg_size> <threads> <duration>
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

/* ========================= Constants ================================= */
#define SERVER_PORT  8080
#define NUM_FIELDS   8

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

/* ========================= Client Thread ============================ */
/*
 * client_thread - Thread function using sendmsg() with iovec.
 *
 * Key difference from A1: Instead of copying 8 fields into a contiguous
 * buffer and calling send(), we set up an iovec array pointing to each
 * field and call sendmsg(), which performs scatter-gather I/O.
 */
static void *client_thread(void *arg) {
    thread_args_t *targs = (thread_args_t *)arg;

    /* --- Step 1: Connect to server --- */
    int sock = connect_to_server(targs->server_ip, targs->server_port);
    if (sock < 0) {
        fprintf(stderr, "[Client T%d] Connection failed\n", targs->thread_id);
        return NULL;
    }

    /* --- Step 2: Send configuration to server --- */
    config_t config;
    config.msg_size = targs->msg_size;
    config.duration = targs->duration;
    if (send(sock, &config, sizeof(config), 0) != sizeof(config)) {
        fprintf(stderr, "[Client T%d] Failed to send config\n", targs->thread_id);
        close(sock);
        return NULL;
    }

    /* --- Step 3: Allocate message with 8 heap-allocated string fields --- */
    message_t *msg = alloc_message(targs->msg_size);

    /* --- Step 4: Set up iovec for scatter-gather I/O --- */
    /*
     * Each iov entry points directly to one of the 8 dynamically
     * allocated fields in the message_t structure. This eliminates
     * the need for a contiguous serialization buffer.
     */
    struct iovec iov[NUM_FIELDS];
    for (int i = 0; i < NUM_FIELDS; i++) {
        iov[i].iov_base = msg->fields[i];
        iov[i].iov_len  = msg->field_size;
    }

    struct msghdr mhdr;
    memset(&mhdr, 0, sizeof(mhdr));
    mhdr.msg_iov    = iov;
    mhdr.msg_iovlen = NUM_FIELDS;

    /* --- Step 5: Send loop for 'duration' seconds --- */
    double    start_time    = get_time_sec();
    long long total_bytes   = 0;
    long long msg_count     = 0;
    double    total_latency = 0.0;

    while (get_time_sec() - start_time < targs->duration) {
        /*
         * ONE COPY (Kernel copy only):
         * sendmsg() reads from the scattered iovec buffers and copies
         * the data into the kernel socket buffer. There is NO prior
         * user-space serialization copy; the kernel handles gathering
         * data from multiple non-contiguous buffers.
         *
         * This eliminates the user-space memcpy that was required in
         * the A1 (two-copy) implementation.
         */
        double  msg_start = get_time_us();
        ssize_t sent      = sendmsg(sock, &mhdr, 0);
        double  msg_end   = get_time_us();

        if (sent < 0) {
            if (errno == EPIPE || errno == ECONNRESET) break;
            if (errno == EINTR) continue;
            perror("sendmsg");
            break;
        }

        total_bytes   += sent;
        msg_count     += 1;
        total_latency += (msg_end - msg_start);
    }

    double elapsed = get_time_sec() - start_time;

    /* --- Step 6: Record metrics --- */
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

    printf("[Client] One-Copy (sendmsg/iovec) Implementation\n");
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
    print_results("one_copy", msg_size, threads,
                  total_bytes, max_elapsed, avg_latency);

    free(tids);
    free(targs);
    return 0;
}
