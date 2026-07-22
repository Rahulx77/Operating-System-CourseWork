#define _POSIX_C_SOURCE 200809L

#include "common.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* =========================================================================
 * User "database" (in-memory, for demonstration).
 *
 * Passwords are never stored in plaintext: only a hash is kept, computed
 * once at startup from the plaintext table below. This keeps the demo
 * self-contained while still avoiding plaintext-password storage.
 *
 * NOTE: djb2 is a fast non-cryptographic hash used here purely to avoid
 * storing raw passwords in memory. It is NOT a substitute for a real
 * credential store. A production system should use a salted password
 * hash (bcrypt/Argon2/scrypt) plus TLS to protect credentials in transit.
 * ========================================================================= */
typedef struct {
    char username[MAX_USERNAME_LEN];
    unsigned long pass_hash;
} user_record_t;

static const char *g_seed_usernames[]  = { "pradip",      "ram",        "admin"        };
static const char *g_seed_passwords[]  = { "pradip456", "ram789", "admin123"  };
#define NUM_USERS (sizeof(g_seed_usernames) / sizeof(g_seed_usernames[0]))

static user_record_t g_userdb[NUM_USERS];

static volatile sig_atomic_t g_running = 1;
static int g_listen_fd = -1;
static int g_client_count = 0;
static pthread_mutex_t g_count_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ---- Helpers ----------------------------------------------------------- */

static unsigned long djb2_hash(const char *str)
{
    unsigned long hash = 5381;
    int c;
    while ((c = (unsigned char)*str++) != 0) {
        hash = ((hash << 5) + hash) + (unsigned long)c; /* hash * 33 + c */
    }
    return hash;
}

static void init_userdb(void)
{
    for (size_t i = 0; i < NUM_USERS; i++) {
        strncpy(g_userdb[i].username, g_seed_usernames[i], MAX_USERNAME_LEN - 1);
        g_userdb[i].username[MAX_USERNAME_LEN - 1] = '\0';
        g_userdb[i].pass_hash = djb2_hash(g_seed_passwords[i]);
    }
}

static int authenticate(const char *username, const char *password)
{
    unsigned long h = djb2_hash(password);
    for (size_t i = 0; i < NUM_USERS; i++) {
        if (strncmp(g_userdb[i].username, username, MAX_USERNAME_LEN) == 0 &&
            g_userdb[i].pass_hash == h) {
            return 1;
        }
    }
    return 0;
}

static void handle_signal(int sig)
{
    (void)sig;
    g_running = 0;
    if (g_listen_fd >= 0) {
        shutdown(g_listen_fd, SHUT_RDWR);
        close(g_listen_fd);
        g_listen_fd = -1;
    }
}

static void to_upper_str(char *s)
{
    for (; *s != '\0'; s++) *s = (char)toupper((unsigned char)*s);
}

static void reverse_str(char *s)
{
    size_t len = strlen(s);
    for (size_t i = 0; i < len / 2; i++) {
        char t = s[i];
        s[i] = s[len - 1 - i];
        s[len - 1 - i] = t;
    }
}

/* The application-level "simple protocol": the text payload of a MSG_DATA
 * message is treated as a command line, and a text result is produced. */
static void process_command(const char *cmd, char *out, size_t outsize)
{
    if (strcmp(cmd, "TIME") == 0) {
        time_t now = time(NULL);
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        strftime(out, outsize, "%Y-%m-%d %H:%M:%S", &tm_info);
    } else if (strncmp(cmd, "UPPER ", 6) == 0) {
        snprintf(out, outsize, "%s", cmd + 6);
        to_upper_str(out);
    } else if (strncmp(cmd, "REVERSE ", 8) == 0) {
        snprintf(out, outsize, "%s", cmd + 8);
        reverse_str(out);
    } else if (strcmp(cmd, "PING") == 0) {
        snprintf(out, outsize, "PONG");
    } else {
        snprintf(out, outsize, "ECHO: %s", cmd);
    }
}

typedef struct {
    int sockfd;
    char ip[INET_ADDRSTRLEN];
    int port;
} client_ctx_t;

static void *handle_client(void *arg)
{
    client_ctx_t *ctx = (client_ctx_t *)arg;
    int sockfd = ctx->sockfd;
    char peer[64];
    snprintf(peer, sizeof(peer), "%s:%d", ctx->ip, ctx->port);
    free(ctx);

    log_msg("Client connected: %s", peer);

    msg_header_t hdr;
    char buf[MAX_PAYLOAD_SIZE];
    int authenticated = 0;
    int attempts = 0;

    /* ---------------- Authentication phase ---------------- */
    while (!authenticated && attempts < MAX_AUTH_ATTEMPTS) {
        int rc = recv_msg(sockfd, &hdr, buf, sizeof(buf) - 1);
        if (rc != 0) {
            log_msg("Client %s disconnected before authenticating", peer);
            goto cleanup;
        }
        if (hdr.type != MSG_AUTH_REQUEST) {
            const char *err = "Expected AUTH_REQUEST";
            send_msg(sockfd, MSG_ERROR, err, (uint32_t)strlen(err));
            continue;
        }

        /* Expected payload layout: "username\0password\0".
         * Every field below is bounds-checked before use - the header's
         * claimed length is never trusted beyond the buffer size, and we
         * require explicit NUL terminators inside that length before
         * treating any byte as part of a string. */
        if (hdr.length < 2 || hdr.length >= sizeof(buf)) {
            const char *err = "Malformed credentials";
            send_msg(sockfd, MSG_AUTH_FAILURE, err, (uint32_t)strlen(err));
            attempts++;
            continue;
        }
        buf[hdr.length] = '\0';

        char *username = buf;
        size_t ulen = strnlen(username, hdr.length);
        if (ulen >= hdr.length) { /* no NUL terminator found -> reject */
            const char *err = "Malformed credentials";
            send_msg(sockfd, MSG_AUTH_FAILURE, err, (uint32_t)strlen(err));
            attempts++;
            continue;
        }

        char *password = username + ulen + 1;
        size_t remaining = hdr.length - (ulen + 1);
        size_t plen = strnlen(password, remaining);

        if (ulen == 0 || ulen >= MAX_USERNAME_LEN ||
            remaining == 0 || plen >= remaining ||
            plen == 0 || plen >= MAX_PASSWORD_LEN) {
            const char *err = "Invalid username/password";
            send_msg(sockfd, MSG_AUTH_FAILURE, err, (uint32_t)strlen(err));
            attempts++;
            continue;
        }

        if (authenticate(username, password)) {
            authenticated = 1;
            send_msg(sockfd, MSG_AUTH_SUCCESS, "OK", 2);
            log_msg("Client %s authenticated as '%s'", peer, username);
        } else {
            attempts++;
            char msg[64];
            snprintf(msg, sizeof(msg), "Auth failed (%d/%d)", attempts, MAX_AUTH_ATTEMPTS);
            send_msg(sockfd, MSG_AUTH_FAILURE, msg, (uint32_t)strlen(msg));
            log_msg("Client %s failed authentication attempt %d", peer, attempts);
        }
    }

    if (!authenticated) {
        log_msg("Client %s exceeded max auth attempts (%d), closing", peer, MAX_AUTH_ATTEMPTS);
        goto cleanup;
    }

    /* ---------------- Data exchange phase ---------------- */
    for (;;) {
        int rc = recv_msg(sockfd, &hdr, buf, sizeof(buf) - 1);
        if (rc == -2) {
            log_msg("Client %s closed the connection", peer);
            break;
        } else if (rc != 0) {
            log_msg("Client %s: connection error, closing", peer);
            break;
        }

        if (hdr.type == MSG_DISCONNECT) {
            log_msg("Client %s requested disconnect", peer);
            break;
        } else if (hdr.type == MSG_PING) {
            send_msg(sockfd, MSG_PONG, NULL, 0);
        } else if (hdr.type == MSG_DATA) {
            buf[hdr.length] = '\0';
            char response[MAX_PAYLOAD_SIZE];
            process_command(buf, response, sizeof(response));
            send_msg(sockfd, MSG_DATA_ACK, response, (uint32_t)strlen(response));
        } else {
            const char *err = "Unknown message type";
            send_msg(sockfd, MSG_ERROR, err, (uint32_t)strlen(err));
        }
    }

cleanup:
    close(sockfd);
    pthread_mutex_lock(&g_count_mutex);
    g_client_count--;
    int remaining_clients = g_client_count;
    pthread_mutex_unlock(&g_count_mutex);
    log_msg("Client %s session ended. Active clients: %d", peer, remaining_clients);
    return NULL;
}

int main(int argc, char *argv[])
{
    int port = SERVER_PORT_DEFAULT;
    if (argc >= 2) {
        int parsed = atoi(argv[1]);
        if (parsed <= 0 || parsed > 65535) {
            fprintf(stderr, "Invalid port '%s', using default %d\n", argv[1], SERVER_PORT_DEFAULT);
        } else {
            port = parsed;
        }
    }

    init_userdb();

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN); /* don't die if a client vanishes mid-write */

    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    int opt = 1;
    if (setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(g_listen_fd);
        return EXIT_FAILURE;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(g_listen_fd);
        return EXIT_FAILURE;
    }

    if (listen(g_listen_fd, 16) < 0) {
        perror("listen");
        close(g_listen_fd);
        return EXIT_FAILURE;
    }

    log_msg("Server listening on port %d (max %d concurrent clients)", port, MAX_CLIENTS);
    log_msg("Demo accounts: pradip/pradip456, ram/ram789, admin/admin123");

    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(g_listen_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd < 0) {
            if (!g_running) break; /* shutdown requested via signal */
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        pthread_mutex_lock(&g_count_mutex);
        int current = g_client_count;
        pthread_mutex_unlock(&g_count_mutex);

        if (current >= MAX_CLIENTS) {
            log_msg("Connection limit (%d) reached, rejecting new client", MAX_CLIENTS);
            const char *err = "Server full, try again later";
            send_msg(client_fd, MSG_ERROR, err, (uint32_t)strlen(err));
            close(client_fd);
            continue;
        }

        client_ctx_t *ctx = malloc(sizeof(client_ctx_t));
        if (ctx == NULL) {
            log_msg("malloc failed while accepting client, dropping connection");
            close(client_fd);
            continue;
        }
        ctx->sockfd = client_fd;
        inet_ntop(AF_INET, &client_addr.sin_addr, ctx->ip, sizeof(ctx->ip));
        ctx->port = ntohs(client_addr.sin_port);

        pthread_mutex_lock(&g_count_mutex);
        g_client_count++;
        pthread_mutex_unlock(&g_count_mutex);

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, ctx) != 0) {
            perror("pthread_create");
            pthread_mutex_lock(&g_count_mutex);
            g_client_count--;
            pthread_mutex_unlock(&g_count_mutex);
            close(client_fd);
            free(ctx);
            continue;
        }
        pthread_detach(tid);
    }

    log_msg("Server shutting down");
    if (g_listen_fd >= 0) close(g_listen_fd);
    return EXIT_SUCCESS;
}
