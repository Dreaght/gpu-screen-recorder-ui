#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/un.h>

static bool get_hyprland_socket_path(char *path, int path_len) {
    const char* xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
    const char* instance_sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!xdg_runtime_dir || !instance_sig) {
        fprintf(stderr, "Error: gsr-hypland-helper: environment variables not set\n");
        return false;
    }
    
    if (snprintf(path, path_len, "%s/hypr/%s/.socket2.sock", xdg_runtime_dir, instance_sig) >= path_len) {
        fprintf(stderr, "Error: gsr-hypland-helper: path to hyprland socket (%s/hypr/%s/.socket2.sock) is more than %d characters long\n", xdg_runtime_dir, instance_sig, path_len);
        return false;
    }

    return true;
}

static void print_window_title(const char *window_title) {
    if (window_title[0] == 0) {
        printf("Window title changed: %s\n", "Desktop");
        fflush(stdout);
        return;
    }
    printf("Window title changed: %s\n", window_title);
    fflush(stdout);
}

static int handle_ipc(void) {
    const int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("Error: gsr-hyprland-helper: socket");
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    if (!get_hyprland_socket_path(addr.sun_path, sizeof(addr.sun_path))) {
        return 1;
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


int main(void) {
    return handle_ipc();
}
