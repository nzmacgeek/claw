#include "ipc.h"
#include "log.h"
#include "mem.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

/* Ensure the directory containing socket_path exists */
static void ensure_socket_dir(const char *socket_path) {
    char dir[256];
    strncpy(dir, socket_path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';

    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) {
        *slash = '\0';
        mkdir(dir, 0755);
    }
}

/* -----------------------------------------------------------------------
 * Server
 * --------------------------------------------------------------------- */

int ipc_server_create(const char *socket_path) {
    if (!socket_path) return -1;

    ensure_socket_dir(socket_path);

    /* Remove stale socket file */
    unlink(socket_path);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        log_error("ipc", "socket() failed: %s", strerror(errno));
        return -1;
    }

    /* Set CLOEXEC so services don't inherit this fd */
    fcntl(fd, F_SETFD, FD_CLOEXEC);

    /* Set non-blocking for use with epoll/select */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("ipc", "bind() failed on %s: %s", socket_path, strerror(errno));
        close(fd);
        return -1;
    }

    /* Only root can talk to claw */
    chmod(socket_path, 0600);

    if (listen(fd, 8) < 0) {
        log_error("ipc", "listen() failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    log_info("ipc", "Listening on %s", socket_path);
    return fd;
}

int ipc_server_accept(int server_fd) {
    struct sockaddr_un addr;
    socklen_t len = sizeof(addr);

    int client_fd = accept(server_fd, (struct sockaddr *)&addr, &len);
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_error("ipc", "accept() failed: %s", strerror(errno));
        }
        return -1;
    }

    /* Block on client reads */
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags & ~O_NONBLOCK);
    fcntl(client_fd, F_SETFD, FD_CLOEXEC);

    return client_fd;
}

void ipc_server_destroy(int server_fd, const char *socket_path) {
    if (server_fd >= 0) close(server_fd);
    if (socket_path)    unlink(socket_path);
}

/* -----------------------------------------------------------------------
 * Client
 * --------------------------------------------------------------------- */

int ipc_client_connect(const char *socket_path) {
    if (!socket_path) return -1;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

/* -----------------------------------------------------------------------
 * Wire protocol — recv / send
 * --------------------------------------------------------------------- */

/* Read exactly n bytes from fd */
static int read_exact(int fd, void *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, (char *)buf + got, n - got);
        if (r <= 0) return -1;
        got += (size_t)r;
    }
    return 0;
}

/* Write exactly n bytes to fd */
static int write_exact(int fd, const void *buf, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        ssize_t w = write(fd, (const char *)buf + sent, n - sent);
        if (w <= 0) return -1;
        sent += (size_t)w;
    }
    return 0;
}

int ipc_recv(int fd, ipc_msg_t *msg) {
    if (!msg) return -1;

    if (read_exact(fd, &msg->header, sizeof(ipc_header_t)) != 0)
        return -1;

    if (msg->header.magic != IPC_MAGIC) {
        log_warning("ipc", "Received message with bad magic: 0x%08x",
                    msg->header.magic);
        return -1;
    }

    if (msg->header.body_len > IPC_MAX_BODY) {
        log_warning("ipc", "Message body too large: %u", msg->header.body_len);
        return -1;
    }

    if (msg->header.body_len > 0) {
        if (read_exact(fd, msg->body, msg->header.body_len) != 0)
            return -1;
    }
    msg->body[msg->header.body_len] = '\0';

    return 0;
}

int ipc_send(int fd, ipc_message_type_t type, ipc_command_t cmd,
             const char *body) {
    ipc_header_t hdr;
    hdr.magic    = IPC_MAGIC;
    hdr.type     = (uint32_t)type;
    hdr.command  = (uint32_t)cmd;
    hdr.body_len = body ? (uint32_t)strlen(body) : 0;

    if (write_exact(fd, &hdr, sizeof(hdr)) != 0) return -1;

    if (hdr.body_len > 0) {
        if (write_exact(fd, body, hdr.body_len) != 0) return -1;
    }

    return 0;
}
