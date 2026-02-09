/*
 * MT25062_Part_A3_Server.c
 * Zero-Copy TCP Server (Receiver Side)
 * Roll No: MT25062
 *
 * Part A3: Zero-copy implementation. Server side uses recv() identically
 * to A1 and A2. The zero-copy optimization (MSG_ZEROCOPY) is on the
 * CLIENT (sender) side only.
 *
 * Usage: ./a3_server [port]
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
#include <errno.h>
#include <signal.h>

/* ========================= Constants ================================= */
#define SERVER_PORT  8080
#define BACKLOG      64

/* ========================= Configuration Struct ====================== */
typedef struct {
    int msg_size;
    int duration;
} config_t;

/* ========================= Server Socket Setup ======================= */
static int create_server_socket(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(EXIT_FAILURE);
    }
    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen"); exit(EXIT_FAILURE);
    }

    printf("[Server] Listening on port %d\n", port);
    return server_fd;
}

/* ========================= Global State ============================== */
static volatile int g_running = 1;

static void handle_signal(int sig) { (void)sig; g_running = 0; }

/* ========================= Client Thread Arguments =================== */
typedef struct {
    int client_fd;
    int thread_id;
} client_thread_args_t;

/* ========================= Client Handler ============================ */
/*
 * handle_client - Receives data from one client connection.
 * Identical to A1/A2 servers. The zero-copy benefit is on the send path.
 */
static void *handle_client(void *arg) {
    client_thread_args_t *targs = (client_thread_args_t *)arg;
    int    client_fd = targs->client_fd;
    int    thread_id = targs->thread_id;

    config_t config;
    ssize_t cfg_bytes = recv(client_fd, &config, sizeof(config), MSG_WAITALL);
    if (cfg_bytes != sizeof(config)) {
        fprintf(stderr, "[Server T%d] Failed to receive config\n", thread_id);
        close(client_fd); free(targs); return NULL;
    }

    int msg_size = config.msg_size;
    printf("[Server T%d] Client connected: msg_size=%d, duration=%d\n",
           thread_id, msg_size, config.duration);

    char *recv_buf = (char *)malloc(msg_size);
    if (!recv_buf) {
        perror("malloc recv_buf");
        close(client_fd); free(targs); return NULL;
    }

    long long total_bytes = 0;
    while (g_running) {
        ssize_t bytes = recv(client_fd, recv_buf, msg_size, 0);
        if (bytes <= 0) break;
        total_bytes += bytes;
    }

    printf("[Server T%d] Received %lld bytes (%.2f MB)\n",
           thread_id, total_bytes, total_bytes / (1024.0 * 1024.0));

    free(recv_buf);
    close(client_fd);
    free(targs);
    return NULL;
}

/* ========================= Main ====================================== */
int main(int argc, char *argv[]) {
    int port = (argc > 1) ? atoi(argv[1]) : SERVER_PORT;

    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    int server_fd = create_server_socket(port);
    int thread_id = 0;

    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(server_fd,
                               (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept"); continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        printf("[Server] Accepted client %d from %s:%d\n",
               thread_id, client_ip, ntohs(client_addr.sin_port));

        client_thread_args_t *targs = malloc(sizeof(client_thread_args_t));
        if (!targs) { perror("malloc"); close(client_fd); continue; }
        targs->client_fd = client_fd;
        targs->thread_id = thread_id++;

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, targs) != 0) {
            perror("pthread_create"); close(client_fd); free(targs); continue;
        }
        pthread_detach(tid);
    }

    printf("[Server] Shutting down.\n");
    close(server_fd);
    return 0;
}
