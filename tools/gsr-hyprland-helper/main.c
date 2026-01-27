#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/un.h>

static void print_window_title(const char *window_title) {
    if (window_title[0] == 0) {
        printf("Window title changed: %s\n", "Desktop");
        fflush(stdout);
        return;
    }
    printf("Window title changed: %s\n", window_title);
    fflush(stdout);
}

static int handle_ipc(const char *hyprland_socket_path) {
    const int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("Error: gsr-hyprland-helper: socket");
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    if (snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", hyprland_socket_path) >= (int)sizeof(addr.sun_path)) {
        fprintf(stderr, "Error: gsr-hypland-helper: path to hyprland socket (%s) is more than %d characters long\n", hyprland_socket_path, (int)sizeof(addr.sun_path));
        return false;
    }

    if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("Error: gsr-hyprland-helper: connect");
        close(sock_fd);
        return 1;
    }

    fprintf(stderr, "Info: gsr-hyprland-helper: connected to Hyprland socket: %s\n", addr.sun_path);

    FILE *sock_file = fdopen(sock_fd, "r");
    if (!sock_file) {
        perror("Error: gsr-hyprland-helper: fdopen");
        close(sock_fd);
        return 1;
    }

    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), sock_file)) {
        int line_len = strlen(buffer);
        if(line_len > 0 && buffer[line_len - 1] == '\n') {
            buffer[line_len - 1] = '\0';
            line_len -= 1;
        }

        if(line_len >= 14 && memcmp(buffer, "activewindow>>", 14) == 0) {
            char *window_id = buffer + 14;
            char *window_title = strchr(buffer + 14, ',');
            if(!window_title)
                continue;

            window_title[0] = '\0';
            window_title += 1;
            if(strcmp(window_id, "gsr-ui") == 0 || strcmp(window_id, "gsr-notify") == 0)
                continue;

            print_window_title(window_title);
        }
    }
    
    fclose(sock_file);
    return 0;
}


int main(int argc, char **argv) {
    if(argc != 2) {
        fprintf(stderr, "usage: gsr-hyprland-helper <hyprland-socket-path>\n");
        return 1;
    }
    return handle_ipc(argv[1]);
}
