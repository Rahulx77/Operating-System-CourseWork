#define _POSIX_C_SOURCE 200809L

#include "common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_msg(const char *fmt, ...)
{
    char timebuf[32];
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm_info);

    va_list args;
    pthread_mutex_lock(&g_log_mutex);
    fprintf(stdout, "[%s] ", timebuf);
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
    fprintf(stdout, "\n");
    fflush(stdout);
    pthread_mutex_unlock(&g_log_mutex);
}

int write_full(int sockfd, const void *buf, size_t n)
{
    size_t total = 0;
    const char *p = (const char *)buf;

    while (total < n) {
        ssize_t sent = send(sockfd, p + total, n - total, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (sent == 0) return -1;
        total += (size_t)sent;
    }
    return 0;
}

int read_full(int sockfd, void *buf, size_t n)
{
    size_t total = 0;
    char *p = (char *)buf;

    while (total < n) {
        ssize_t got = recv(sockfd, p + total, n - total, 0);
        if (got < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (got == 0) {
            /* peer closed the connection */
            return (total == 0) ? -2 : -1;
        }
        total += (size_t)got;
    }
    return 0;
}

int send_msg(int sockfd, uint32_t type, const void *payload, uint32_t len)
{
    msg_header_t hdr;
    hdr.magic  = htonl(PROTOCOL_MAGIC);
    hdr.type   = htonl(type);
    hdr.length = htonl(len);

    if (write_full(sockfd, &hdr, sizeof(hdr)) != 0) return -1;
    if (len > 0 && payload != NULL) {
        if (write_full(sockfd, payload, len) != 0) return -1;
    }
    return 0;
}

int recv_msg(int sockfd, msg_header_t *hdr, void *buf, uint32_t bufsize)
{
    int rc = read_full(sockfd, hdr, sizeof(*hdr));
    if (rc != 0) return rc; /* -1 error, -2 clean disconnect */

    hdr->magic  = ntohl(hdr->magic);
    hdr->type   = ntohl(hdr->type);
    hdr->length = ntohl(hdr->length);

    /* --- Data validation / security checks --- */
    if (hdr->magic != PROTOCOL_MAGIC) {
        log_msg("Rejected message: bad magic number 0x%08X", hdr->magic);
        return -1;
    }
    if (hdr->length > bufsize) {
        log_msg("Rejected message: payload length %u exceeds buffer size %u",
                 hdr->length, bufsize);
        return -1;
    }

    if (hdr->length > 0) {
        rc = read_full(sockfd, buf, hdr->length);
        if (rc != 0) return rc;
    }
    return 0;
}
