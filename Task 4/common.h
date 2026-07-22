#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stddef.h>

/* ---- Protocol configuration ------------------------------------------ */
#define SERVER_PORT_DEFAULT 5555
#define MAX_PAYLOAD_SIZE    1024      /* hard cap on any single message body */
#define MAX_USERNAME_LEN    32
#define MAX_PASSWORD_LEN    32
#define MAX_CLIENTS         50        /* concurrent connection limit (DoS guard) */
#define MAX_AUTH_ATTEMPTS   3
#define PROTOCOL_MAGIC      0xC0FFEE01U

/* ---- Message types ----------------------------------------------------
 * A tiny, explicit application protocol layered on top of TCP.
 * Every message on the wire is: [msg_header_t][payload bytes]
 * -----------------------------------------------------------------------*/
typedef enum {
    MSG_AUTH_REQUEST  = 1,   /* client -> server: "username\0password\0" */
    MSG_AUTH_SUCCESS  = 2,   /* server -> client */
    MSG_AUTH_FAILURE  = 3,   /* server -> client, payload = reason text   */
    MSG_DATA          = 4,   /* client -> server: command/text line       */
    MSG_DATA_ACK      = 5,   /* server -> client: result text             */
    MSG_ERROR         = 6,   /* either direction: payload = error text    */
    MSG_DISCONNECT    = 7,   /* client -> server: graceful close request  */
    MSG_PING          = 8,   /* client -> server: liveness check          */
    MSG_PONG          = 9    /* server -> client: liveness reply          */
} msg_type_t;

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;    /* PROTOCOL_MAGIC, used to reject garbage/foreign data */
    uint32_t type;     /* one of msg_type_t                                  */
    uint32_t length;   /* length of payload that follows, in bytes           */
} msg_header_t;
#pragma pack(pop)

#define HEADER_SIZE (sizeof(msg_header_t))

/* Reliable, fully-buffered I/O helpers.
 * Return 0 on success, -1 on fatal/IO error, -2 on a clean peer disconnect. */
int read_full(int sockfd, void *buf, size_t n);
int write_full(int sockfd, const void *buf, size_t n);

/* Framed message helpers built on top of read_full/write_full.
 * recv_msg validates the magic number and enforces that the reported
 * payload length never exceeds the caller-supplied buffer size, which
 * is the main defence against buffer overflow / oversized-message DoS. */
int send_msg(int sockfd, uint32_t type, const void *payload, uint32_t len);
int recv_msg(int sockfd, msg_header_t *hdr, void *buf, uint32_t bufsize);

/* Thread-safe timestamped logging used by the server. */
void log_msg(const char *fmt, ...);

#endif /* COMMON_H */
