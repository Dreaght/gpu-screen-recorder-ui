#include "unistd.h"
#include "string.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>


struct ActiveHyprlandWindow {
    char* window_id;
    char* title;
};


struct ActiveHyprlandWindow* active_window;


const char* get_hyprland_socket_path() {
    const char* xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
    const char* instance_sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!xdg_runtime_dir || !instance_sig) {
        fprintf(stderr, "Error: Env vars not set.\n");
        exit(1);
    }
    
    // allocate buffer for the full path
    size_t path_len = strlen(xdg_runtime_dir) + strlen(instance_sig) + 32;
    char* socket_path = malloc(path_len);
    if (!socket_path) {
        fprintf(stderr, "Error: Memory allocation failed.\n");
        exit(1);
    }
    
    snprintf(socket_path, path_len, "%s/hypr/%s/.socket2.sock", xdg_runtime_dir, instance_sig);
    return socket_path;
}


void print_window_title(void) {
    if (strlen(active_window->title) == 0) {
        printf("Window title changed: %s\n", "Desktop");
        fflush(stdout);
        return;
    }
    printf("Window title changed: %s\n", active_window->title);
    fflush(stdout);
}


void handle_ipc(void) {
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("socket");
        return;
    }


    active_window = malloc(sizeof(struct ActiveHyprlandWindow));
    active_window->window_id = NULL;
    active_window->title = NULL;


    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;


    const char* socket_path = get_hyprland_socket_path();
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);


    if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("connect");
        close(sock_fd);
        free((void*)socket_path);
        return;
    }


    printf("Connected to Hyprland socket: %s\n", socket_path);
    fflush(stdout);
    free((void*)socket_path);


    char buffer[4096];
    char* incomplete_line = NULL;
    size_t incomplete_len = 0;


    while (1) {
        ssize_t bytes_read = read(sock_fd, buffer, sizeof(buffer));


        if (bytes_read == 0) {
            fprintf(stderr, "Connection closed by Hyprland\n");
            break;
        }


        if (bytes_read < 0) {
            perror("read");
            break;
        }


        size_t total_len = incomplete_len + bytes_read;
        char *data = malloc(total_len + 1);
        if (!data) {
            perror("malloc");
            break;
        }


        if (incomplete_line) {
            memcpy(data, incomplete_line, incomplete_len);
            fflush(stdout);
            free(incomplete_line);
            incomplete_line = NULL;
        }
        memcpy(data + incomplete_len, buffer, bytes_read);
        data[total_len] = '\0';


        char* line_start = data;
        char* newline_pos;


        while ((newline_pos = strchr(line_start, '\n')) != NULL) {
            *newline_pos = '\0';
            char *line = line_start;


            char *delimiter = strstr(line, ">>");


            if (delimiter != NULL) {
                *delimiter = '\0';
                char *event_name = line;
                char *event_data = delimiter + 2;


                if (strcmp(event_name, "activewindowv2") == 0) {
                    fflush(stdout);
                    if (active_window->window_id) {
                        free(active_window->window_id);
                    }
                    active_window->window_id = strdup(event_data);
                }


                if (strcmp(event_name, "activewindow") == 0) {
                    char *title_delimiter = strchr(event_data, ',');
                    if (title_delimiter != NULL) {
                        char *window_title = title_delimiter + 1;
                        if (strcmp(window_title, "gsr ui") == 0 || 
                            strcmp(window_title, "gsr notify") == 0) {
                            line_start = newline_pos + 1;
                            continue;
                        }
                        fflush(stdout);
                        if (active_window->title) {
                            free(active_window->title);
                        }
                        active_window->title = strdup(window_title);
                        print_window_title();
                    }
                }


                if (strcmp(event_name, "windowtitlev2") == 0) {
                    char *data_delimiter = strchr(event_data, ',');
                    if (data_delimiter != NULL) {
                        *data_delimiter = '\0';
                        char *win_id = event_data;
                        char *new_window_title = data_delimiter + 1;
                        
                        if (strcmp(new_window_title, "gsr ui") == 0 || 
                            strcmp(new_window_title, "gsr notify") == 0) {
                            line_start = newline_pos + 1;
                            continue;
                        }
                        
                        if (strcmp(active_window->window_id, win_id) != 0) {
                            line_start = newline_pos + 1;
                            continue;
                        }
                        
                        fflush(stdout);
                        if (active_window->title) {
                            free(active_window->title);
                        }
                        active_window->title = strdup(new_window_title);
                        print_window_title();
                    }
                }
            }


            line_start = newline_pos + 1;
        }


        size_t remaining = strlen(line_start);
        if (remaining > 0) {
            incomplete_line = malloc(remaining + 1);
            if (incomplete_line) {
                memcpy(incomplete_line, line_start, remaining);
                incomplete_line[remaining] = '\0';
                incomplete_len = remaining;
            }
        } else {
            incomplete_len = 0;
        }


        fflush(stdout);
        free(data);
    }


    if (incomplete_line) {
        fflush(stdout);
        free(incomplete_line);
    }
    
    if (active_window) {
        if (active_window->window_id) {
            free(active_window->window_id);
        }
        if (active_window->title) {
            free(active_window->title);
        }
        free(active_window);
    }
    
    close(sock_fd);
}


int main(int argc, char** argv) {
    handle_ipc();
    return 0;
}
