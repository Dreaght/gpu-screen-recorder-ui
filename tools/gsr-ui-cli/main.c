#include <limits.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

static void get_socket_filepath(char *buffer, size_t buffer_size, const char *filename) {
    char dir[PATH_MAX];

    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if(runtime_dir)
        snprintf(dir, sizeof(dir), "%s", runtime_dir);
    else
        snprintf(dir, sizeof(dir), "/run/user/%d", geteuid());

    if(access(dir, F_OK) != 0)
        snprintf(dir, sizeof(dir), "/tmp");

    snprintf(buffer, buffer_size, "%s/%s", dir, filename);
}

/* Assumes |str| size is less than 256 */
static void file_write_all(int file_fd, const char *str) {
    char command[256];
    const ssize_t command_size = snprintf(command, sizeof(command), "%s\n", str);
    if(command_size >= (ssize_t)sizeof(command)) {
        fprintf(stderr, "Error: command too long: %s\n", str);
        return;
    }

    ssize_t offset = 0;
    while(offset < (ssize_t)command_size) {
        const ssize_t bytes_written = write(file_fd, command + offset, command_size - offset);
        if(bytes_written > 0)
            offset += bytes_written;
    }
}

static void usage(void) {
    printf("usage: gsr-ui-cli <command>\n");
    printf("Run commands on the running gsr-ui instance.\n");
    printf("\n");
    printf("COMMANDS:\n");
    printf("  toggle-show\n");
    printf("      Show/hide the UI.\n");
    printf("  toggle-record\n");
    printf("      Start/stop recording.\n");
    printf("  toggle-pause\n");
    printf("      Pause/unpause recording. Only applies to regular recording.\n");
    printf("  toggle-stream\n");
    printf("      Start/stop streaming.\n");
    printf("  toggle-replay\n");
    printf("      Start/stop replay.\n");
    printf("  replay-save\n");
    printf("      Save replay.\n");
    printf("  replay-save-1-min\n");
    printf("      Save 1 minute replay.\n");
    printf("  replay-save-10-min\n");
    printf("      Save 10 minute replay.\n");
    printf("  take-screenshot\n");
    printf("      Take a screenshot.\n");
    printf("  take-screenshot-region\n");
    printf("      Take a screenshot of a region.\n");
    printf("  take-screenshot-window\n");
    printf("      Take a screenshot of a window (or desktop portal on Wayland).\n");
    printf("\n");
    printf("EXAMPLES:\n");
    printf("  gsr-ui-cli toggle-show\n");
    printf("  gsr-ui-cli toggle-record\n");
    exit(1);
}

static bool is_valid_command(const char *command) {
    const char *commands[] = {
        "toggle-show",
        "toggle-record",
        "toggle-pause",
        "toggle-stream",
        "toggle-replay",
        "replay-save",
        "replay-save-1-min",
        "replay-save-10-min",
        "take-screenshot",
        "take-screenshot-region",
        "take-screenshot-window",
        NULL
    };

    for(int i = 0; commands[i]; ++i) {
        if(strcmp(command, commands[i]) == 0)
            return true;
    }

    return false;
}

int main(int argc, char **argv) {
    if(argc != 2) {
        printf("Error: expected 1 argument, %d provided\n", argc - 1);
        usage();
    }

    const char *command = argv[1];
    if(strcmp(command, "-h") == 0 || strcmp(command, "--help") == 0)
        usage();

    if(!is_valid_command(command)) {
        fprintf(stderr, "Error: invalid command: \"%s\"\n", command);
        usage();
    }

    char socket_filepath[PATH_MAX];
    get_socket_filepath(socket_filepath, sizeof(socket_filepath), "gsr-ui");

    const int socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if(socket_fd <= 0) {
        fprintf(stderr, "Error: failed to create socket\n");
        exit(2);
    }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_filepath);

    for(;;) {
        if(connect(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
            const int err = errno;
            if(err == EWOULDBLOCK) {
                usleep(10 * 1000);
            } else {
                fprintf(stderr, "Error: failed to connect, error: %s. Maybe gsr-ui is not running?\n", strerror(err));
                exit(2);
            }
        } else {
            break;
        }
    }

    file_write_all(socket_fd, command);
    close(socket_fd);
    return 0;
}
