#define _POSIX_C_SOURCE 200809L

#include "common.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

static void trim_newline(char *s)
{
    s[strcspn(s, "\n")] = '\0';
}

/* Reads a password from stdin with terminal echo disabled, so it is not
 * shown on screen. This is a simple, self-contained security measure for
 * interactive use (it does not protect the password on the wire - see
 * README for the TLS caveat). */
static void read_password(char *buf, size_t size)
{
    struct termios oldt, newt;
    int have_tty = (tcgetattr(STDIN_FILENO, &oldt) == 0);

    if (have_tty) {
        newt = oldt;
        newt.c_lflag &= ~((tcflag_t)ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    }

    if (fgets(buf, (int)size, stdin) == NULL) {
        buf[0] = '\0';
    }
    trim_newline(buf);

    if (have_tty) {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        printf("\n");
    }
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port> [username]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port number: %s\n", argv[2]);
        return EXIT_FAILURE;
    }

    char username[MAX_USERNAME_LEN];
    if (argc >= 4) {
        strncpy(username, argv[3], sizeof(username) - 1);
        username[sizeof(username) - 1] = '\0';
    } else {
        printf("Username: ");
        fflush(stdout);
        if (fgets(username, sizeof(username), stdin) == NULL) {
            fprintf(stderr, "Failed to read username\n");
            return EXIT_FAILURE;
        }
        trim_newline(username);
    }

    char password[MAX_PASSWORD_LEN];
    printf("Password: ");
    fflush(stdout);
    read_password(password, sizeof(password));

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid server address: %s\n", server_ip);
        close(sockfd);
        return EXIT_FAILURE;
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("Connected to %s:%d\n", server_ip, port);

    /* Build AUTH_REQUEST payload: "username\0password\0" */
    size_t ulen = strlen(username);
    size_t plen = strlen(password);
    if (ulen == 0 || ulen >= MAX_USERNAME_LEN || plen == 0 || plen >= MAX_PASSWORD_LEN) {
        fprintf(stderr, "Username/password length is invalid\n");
        memset(password, 0, sizeof(password));
        close(sockfd);
        return EXIT_FAILURE;
    }

    char authbuf[MAX_USERNAME_LEN + MAX_PASSWORD_LEN];
    memcpy(authbuf, username, ulen + 1);
    memcpy(authbuf + ulen + 1, password, plen + 1);
    uint32_t authlen = (uint32_t)(ulen + 1 + plen + 1);

    /* Wipe the plaintext password from local memory as soon as it has
     * been copied into the outgoing buffer. */
    memset(password, 0, sizeof(password));

    if (send_msg(sockfd, MSG_AUTH_REQUEST, authbuf, authlen) != 0) {
        fprintf(stderr, "Failed to send authentication request\n");
        memset(authbuf, 0, sizeof(authbuf));
        close(sockfd);
        return EXIT_FAILURE;
    }
    memset(authbuf, 0, sizeof(authbuf));

    msg_header_t hdr;
    char buf[MAX_PAYLOAD_SIZE];
    int rc = recv_msg(sockfd, &hdr, buf, sizeof(buf) - 1);
    if (rc != 0) {
        fprintf(stderr, "Lost connection during authentication\n");
        close(sockfd);
        return EXIT_FAILURE;
    }
    buf[hdr.length] = '\0';

    if (hdr.type != MSG_AUTH_SUCCESS) {
        printf("Authentication failed: %s\n", buf);
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("Authentication successful.\n");
    printf("Commands: TIME | PING | UPPER <text> | REVERSE <text> | <anything else echoes back>\n");
    printf("Type 'quit' to disconnect.\n");

    char line[MAX_PAYLOAD_SIZE];
    while (1) {
        printf("> ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL) break; /* EOF / Ctrl-D */
        trim_newline(line);
        if (strlen(line) == 0) continue;

        if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
            send_msg(sockfd, MSG_DISCONNECT, NULL, 0);
            break;
        }

        if (send_msg(sockfd, MSG_DATA, line, (uint32_t)strlen(line)) != 0) {
            fprintf(stderr, "Failed to send data - connection lost\n");
            break;
        }

        rc = recv_msg(sockfd, &hdr, buf, sizeof(buf) - 1);
        if (rc != 0) {
            fprintf(stderr, "Server connection lost\n");
            break;
        }
        buf[hdr.length] = '\0';

        if (hdr.type == MSG_DATA_ACK) {
            printf("%s\n", buf);
        } else if (hdr.type == MSG_ERROR) {
            printf("Server error: %s\n", buf);
        } else {
            printf("Unexpected message type: %u\n", hdr.type);
        }
    }

    close(sockfd);
    printf("Disconnected.\n");
    return EXIT_SUCCESS;
}
