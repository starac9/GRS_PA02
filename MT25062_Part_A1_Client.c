/*
 * MT25062_Part_A1_Client.c
 * Two-Copy TCP Client (Sender Side)
 * Roll No: MT25062
 *
 * Part A1: Baseline two-copy implementation using send().
 * The client spawns multiple threads, each connecting to the server.
 * Each thread serializes a message_t (8 heap-allocated string fields)
 * into a contiguous buffer and sends using send().
 *
 * TWO COPIES on the send path:
 *   Copy 1 (User-space):  8 scattered fields --> contiguous send buffer
 *                          (memcpy during serialization)
 *   Copy 2 (Kernel):      User send buffer --> kernel socket buffer
 *                          (performed by send() system call)
 *
 * Usage: ./a1_client <server_ip> <port> <msg_size> <threads> <duration>
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
#include <errno.h>
#include <signal.h>

/* ========================= Constants ================================= */
#define SERVER_PORT  8080
#define NUM_FIELDS   8     /* Number of string fields in message struct */
#define BACKLOG      64

/* ========================= Structures ================================ */

/* Configuration sent to server at connection start */
typedef struct {
    int msg_size;
    int duration;
} config_t;

/*
 * Message structure comprising 8 dynamically allocated string fields.
 * Each field is heap-allocated via malloc().
 */
typedef struct {
    char *fields[NUM_FIELDS];
    int   field_size;
} message_t;

/* Thread arguments with input params and output metrics */
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

/* Returns current time in microseconds */
static double get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1e6 + (double)tv.tv_usec;
}

/* Returns current time in seconds (double precision) */
static double get_time_sec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
}

/* ========================= Message Management ======================== */

/*
 * alloc_message - Allocates a message_t with 8 heap-allocated string fields.
 * @msg_size: Total message size; each field gets msg_size / NUM_FIELDS bytes.
 * Each field is filled with a repeating pattern to ensure pages are faulted in.
 */
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

/* free_message - Frees all 8 fields and the message structure */
static void free_message(message_t *msg) {
    if (!msg) return;
    for (int i = 0; i < NUM_FIELDS; i++) free(msg->fields[i]);
    free(msg);
}

/* ========================= Network Utilities ========================= */

/*
 * send_all - Sends exactly len bytes, handling partial sends.
 * Returns: Total bytes sent, or -1 on error.
 */
static ssize_t send_all(int sock, const void *buf, size_t len, int flags) {
    const char *p = (const char *)buf;
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t sent = send(sock, p, remaining, flags);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p         += sent;
        remaining -= sent;
    }
    return (ssize_t)len;
}

/*
 * connect_to_server - Creates a TCP socket and connects to server.
 * Returns: Connected socket fd, or -1 on error.
 */
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

/* print_results - Prints benchmark results in parseable CSV format */
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
 * client_thread - Thread function for sending data to server.
 *
 * Each thread independently:
 *   1. Connects to the server.
 *   2. Sends configuration (msg_size, duration).
 *   3. Allocates a message_t with 8 heap-allocated fields.
 *   4. Allocates a contiguous serialization buffer.
 *   5. Serializes + sends messages in a tight loop for 'duration' seconds.
 *   6. Records throughput and latency metrics.
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

    /* --- Step 4: Allocate contiguous serialization buffer --- */
    char *send_buf = (char *)malloc(targs->msg_size);
    if (!send_buf) {
        perror("malloc send_buf");
        free_message(msg);
        close(sock);
        return NULL;
    }

    /* --- Step 5: Send loop for 'duration' seconds --- */
    double    start_time    = get_time_sec();
    long long total_bytes   = 0;
    long long msg_count     = 0;
    double    total_latency = 0.0;

    while (get_time_sec() - start_time < targs->duration) {
        /*
         * COPY 1 (User-space serialization):
         * Copy each of the 8 dynamically allocated fields into a
         * single contiguous buffer. Required because send() needs
         * a single contiguous memory region.
         */
        int offset = 0;
        for (int i = 0; i < NUM_FIELDS; i++) {
            memcpy(send_buf + offset, msg->fields[i], msg->field_size);
            offset += msg->field_size;
        }

        /*
         * COPY 2 (Kernel copy):
         * send() copies the contiguous user buffer into the kernel
         * socket buffer (sk_buff). The kernel then transmits from
         * its own buffer.
         */
        double  msg_start = get_time_us();
        ssize_t sent      = send_all(sock, send_buf, targs->msg_size, 0);
        double  msg_end   = get_time_us();

        if (sent < 0) {
            if (errno == EPIPE || errno == ECONNRESET) break;
            perror("send");
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

    /* Cleanup */
    free(send_buf);
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

    printf("[Client] Two-Copy (send/recv) Implementation\n");
    printf("[Client] Server=%s:%d, MsgSize=%d, Threads=%d, Duration=%d sec\n",
           server_ip, port, msg_size, threads, duration);

    signal(SIGPIPE, SIG_IGN);

    pthread_t     *tids  = malloc(sizeof(pthread_t)     * threads);
    thread_args_t *targs = malloc(sizeof(thread_args_t) * threads);
    if (!tids || !targs) { perror("malloc threads"); return EXIT_FAILURE; }

    /* Spawn client threads */
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

    /* Wait for all threads */
    for (int i = 0; i < threads; i++) {
        pthread_join(tids[i], NULL);
    }

    /* Aggregate and print results */
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
    print_results("two_copy", msg_size, threads,
                  total_bytes, max_elapsed, avg_latency);

    free(tids);
    free(targs);
    return 0;
}
