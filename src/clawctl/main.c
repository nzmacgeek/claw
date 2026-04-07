#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "claw.h"
#include "ipc.h"

static const char *g_socket_path = CLAW_SOCKET_PATH;

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [-S <socket>] <command> [unit]\n"
        "\n"
        "Options:\n"
        "  -S <socket>          Path to claw daemon socket (default: %s)\n"
        "\n"
        "Commands:\n"
        "  start   <service>    Start a service\n"
        "  stop    <service>    Stop a service\n"
        "  restart <service>    Restart a service\n"
        "  status  <unit>       Show unit status\n"
        "  list                 List all units\n"
        "  isolate <target>     Switch to target\n"
        "  reload               Reload daemon configuration\n"
        "  shutdown             Shut the system down\n"
        "\n"
        "Claw " CLAW_VERSION " (build " CLAW_BUILD_ID ")\n",
        prog, CLAW_SOCKET_PATH);
}

static void build_request(char *buf, size_t len, const char *unit) {
    if (unit && *unit)
        snprintf(buf, len, "{\"unit\":\"%s\"}", unit);
    else
        snprintf(buf, len, "{}");
}

static int send_command(ipc_command_t cmd, const char *unit) {
    int fd = ipc_client_connect(g_socket_path);
    if (fd < 0) {
        fprintf(stderr, "error: cannot connect to claw daemon (%s)\n"
                        "       Is claw running? Socket: %s\n",
                strerror(errno), g_socket_path);
        return 1;
    }

    char body[256];
    build_request(body, sizeof(body), unit);

    if (ipc_send(fd, IPC_REQUEST, cmd, body) != 0) {
        fprintf(stderr, "error: failed to send command\n");
        close(fd);
        return 1;
    }

    ipc_msg_t response;
    if (ipc_recv(fd, &response) != 0) {
        fprintf(stderr, "error: no response from daemon\n");
        close(fd);
        return 1;
    }

    close(fd);

    if (response.header.body_len > 0)
        printf("%s\n", response.body);

    return strstr(response.body, "\"error\"") ? 1 : 0;
}

int main(int argc, char *argv[]) {
    /* Parse optional flags */
    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "-S") == 0 && i + 1 < argc) {
            g_socket_path = argv[++i];
        } else {
            usage(argv[0]);
            return 1;
        }
        i++;
    }

    if (i >= argc) {
        usage(argv[0]);
        return 1;
    }

    const char *cmd  = argv[i];
    const char *unit = (i + 1 < argc) ? argv[i + 1] : NULL;

    if      (strcmp(cmd, "start")    == 0) {
        if (!unit) { fprintf(stderr, "start requires a unit name\n"); return 1; }
        return send_command(CLAW_CMD_START, unit);
    } else if (strcmp(cmd, "stop")   == 0) {
        if (!unit) { fprintf(stderr, "stop requires a unit name\n"); return 1; }
        return send_command(CLAW_CMD_STOP, unit);
    } else if (strcmp(cmd, "restart") == 0) {
        if (!unit) { fprintf(stderr, "restart requires a unit name\n"); return 1; }
        return send_command(CLAW_CMD_RESTART, unit);
    } else if (strcmp(cmd, "status") == 0) {
        if (!unit) { fprintf(stderr, "status requires a unit name\n"); return 1; }
        return send_command(CLAW_CMD_STATUS, unit);
    } else if (strcmp(cmd, "list")   == 0) {
        return send_command(CLAW_CMD_LIST, NULL);
    } else if (strcmp(cmd, "isolate") == 0) {
        if (!unit) { fprintf(stderr, "isolate requires a target name\n"); return 1; }
        return send_command(CLAW_CMD_ISOLATE, unit);
    } else if (strcmp(cmd, "reload") == 0) {
        return send_command(CLAW_CMD_RELOAD, NULL);
    } else if (strcmp(cmd, "shutdown") == 0) {
        return send_command(CLAW_CMD_SHUTDOWN, NULL);
    } else {
        fprintf(stderr, "Unknown command: %s\n\n", cmd);
        usage(argv[0]);
        return 1;
    }
}
