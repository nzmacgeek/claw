#ifndef __IPC_H__
#define __IPC_H__

#include "claw.h"
#include <stdint.h>
#include <stddef.h>

#define CLAW_SOCKET_PATH "/run/claw/claw.sock"
#define IPC_MAGIC        0xC1A04000u
#define IPC_MAX_BODY     4096

/* On-wire message header (fixed size, little-endian) */
typedef struct __attribute__((packed)) {
    uint32_t magic;       /* IPC_MAGIC */
    uint32_t type;        /* ipc_message_type_t */
    uint32_t command;     /* ipc_command_t (for REQUEST) */
    uint32_t body_len;    /* Length of body following this header */
} ipc_header_t;

/* In-memory representation of a received message */
typedef struct {
    ipc_header_t header;
    char         body[IPC_MAX_BODY + 1]; /* NUL-terminated JSON */
} ipc_msg_t;

/* ---- Server side (runs inside claw daemon) ---- */

/* Create and bind the server socket.
 * Returns fd on success, -1 on error. */
int  ipc_server_create(const char *socket_path);

/* Accept a connection (blocking).
 * Returns client fd, -1 on error. */
int  ipc_server_accept(int server_fd);

/* Read one message from client_fd into msg.
 * Returns 0 on success, -1 on error or disconnect. */
int  ipc_recv(int fd, ipc_msg_t *msg);

/* Send a response message.
 * body is a NUL-terminated JSON string (may be NULL for empty body). */
int  ipc_send(int fd, ipc_message_type_t type, ipc_command_t cmd,
              const char *body);

/* Close and unlink the socket */
void ipc_server_destroy(int server_fd, const char *socket_path);

/* ---- Client side (runs inside clawctl) ---- */

/* Connect to the server socket.
 * Returns fd on success, -1 on error. */
int  ipc_client_connect(const char *socket_path);

#endif /* __IPC_H__ */
