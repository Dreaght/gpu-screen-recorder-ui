#include <limits.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static void get_runtime_filepath(char *buffer, size_t buffer_size, const char *filename) {
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
static void fifo_write_all(int file_fd, const char *str) {
    char command[256];
    const ssize_t command_size = snprintf(command, sizeof(command), "%s\n", str);
    if(command_size >= (ssize_t)sizeof(command)) {
        fprintf(stderr, "Error: command too long: %s\n", str);
        return;
    }

    ssize_t offset = 0;
    while(offset < (ssize_t)command_size) {
        const ssize_t bytes_written = write(file_fd, str + offset, command_size - offset);
        if(bytes_written > 0)
            offset += bytes_written;
    }
}

static void usage(void) {
    printf("usage: gsr-ui-cli <command>\n");
    printf("Run commands on the running gsr-ui instance.\n");
    printf("\n");
    printf("COMMANDS:\n");
    printf("  toggle-show      Show/hide the UI.\n");
    printf("  toggle-record    Start/stop recording.\n");
    printf("  toggle-pause     Pause/unpause recording. Only applies to regular recording.\n");
    printf("  toggle-stream    Start/stop streaming.\n");
    printf("  toggle-replay    Start/stop replay.\n");
    printf("  replay-save      Save replay.\n");
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

    char fifo_filepath[PATH_MAX];
    get_runtime_filepath(fifo_filepath, sizeof(fifo_filepath), "gsr-ui");
    const int fifo_fd = open(fifo_filepath, O_RDWR | O_NONBLOCK);
    if(fifo_fd <= 0) {
        fprintf(stderr, "Error: failed to open fifo file %s. Maybe gsr-ui is not running?\n", fifo_filepath);
        exit(2);
    }

    fifo_write_all(fifo_fd, command);
    close(fifo_fd);
    return 0;
}
