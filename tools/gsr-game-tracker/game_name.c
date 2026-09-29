#include "game_name.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

/* Returns NULL on failure. The returned data is null-terminated and has to be freed.
   Only regular files are read, to not block forever on something like a fifo */
static char* read_file_alloc(const char *path, size_t *size) {
    const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return NULL;

    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return NULL;
    }

    char *data = NULL;
    size_t data_size = 0;
    size_t capacity = 0;
    for (;;) {
        if (data_size + 1 >= capacity) {
            capacity = capacity == 0 ? 16384 : capacity * 2;
            if (capacity > 64 * 1024 * 1024) {
                free(data);
                close(fd);
                return NULL;
            }

            char *new_data = realloc(data, capacity);
            if (!new_data) {
                free(data);
                close(fd);
                return NULL;
            }
            data = new_data;
        }

        const ssize_t n = read(fd, data + data_size, capacity - data_size - 1);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        data_size += (size_t)n;
    }

    close(fd);
    data[data_size] = '\0';
    *size = data_size;
    return data;
}

static bool ends_with_case(const char *str, size_t str_len, const char *suffix) {
    const size_t suffix_len = strlen(suffix);
    return str_len >= suffix_len && strncasecmp(str + str_len - suffix_len, suffix, suffix_len) == 0;
}

typedef bool (*VdfTextCallback)(const char *key, const char *value, void *userdata);

/* |data[*index]| has to be '"'. Returns false if the string isn't terminated */
static bool vdf_text_read_string(const char *data, size_t size, size_t *index, char *out, size_t out_size) {
    size_t i = *index + 1;
    size_t out_len = 0;
    while (i < size && data[i] != '"') {
        char c = data[i];
        if (c == '\\' && i + 1 < size) {
            ++i;
            c = data[i];
            if (c == 'n') c = '\n';
            else if (c == 't') c = '\t';
        }

        if (out_len + 1 < out_size)
            out[out_len++] = c;
        ++i;
    }

    if (i >= size) return false;
    out[out_len] = '\0';
    *index = i + 1;
    return true;
}

/* Calls |callback| for every "key" "value" pair in a text vdf file (such as steam appmanifest files), until the callback returns false */
static void vdf_text_for_each(const char *data, size_t size, VdfTextCallback callback, void *userdata) {
    char key[256];
    char value[4096];
    bool has_key = false;

    size_t i = 0;
    while (i < size) {
        const char c = data[i];
        if (c == '"') {
            if (!vdf_text_read_string(data, size, &i, has_key ? value : key, has_key ? sizeof(value) : sizeof(key)))
                return;

            if (has_key) {
                if (!callback(key, value, userdata))
                    return;
                has_key = false;
            } else {
                has_key = true;
            }
        } else if (c == '{' || c == '}') {
            has_key = false;
            ++i;
        } else if (c == '/' && i + 1 < size && data[i + 1] == '/') {
            const char *line_end = memchr(data + i, '\n', size - i);
            if (!line_end) return;
            i = line_end - data + 1;
        } else {
            ++i;
        }
    }
}

typedef struct {
    char *name;
    size_t name_size;
    bool found;
} FindNameUserdata;

static bool appmanifest_find_name_callback(const char *key, const char *value, void *userdata) {
    FindNameUserdata *find_name = userdata;
    if (strcasecmp(key, "name") != 0)
        return true;

    snprintf(find_name->name, find_name->name_size, "%s", value);
    find_name->found = true;
    return false;
}

static bool steam_get_app_name_in_library(const char *library_path, const char *app_id, char *name, size_t name_size) {
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/steamapps/appmanifest_%s.acf", library_path, app_id) >= (int)sizeof(path))
        return false;

    size_t size = 0;
    char *data = read_file_alloc(path, &size);
    if (!data) return false;

    FindNameUserdata find_name = { name, name_size, false };
    vdf_text_for_each(data, size, appmanifest_find_name_callback, &find_name);
    free(data);
    return find_name.found && name[0] != '\0';
}

typedef struct {
    const char *app_id;
    FindNameUserdata find_name;
} LibraryFoldersUserdata;

static bool library_folders_find_app_callback(const char *key, const char *value, void *userdata) {
    LibraryFoldersUserdata *library_folders = userdata;
    if (strcasecmp(key, "path") != 0)
        return true;

    library_folders->find_name.found = steam_get_app_name_in_library(value, library_folders->app_id, library_folders->find_name.name, library_folders->find_name.name_size);
    return !library_folders->find_name.found;
}

/* Looks up the game name in the appmanifest file of every steam library */
static bool steam_get_app_name(const char *steam_path, const char *app_id, char *name, size_t name_size) {
    if (steam_get_app_name_in_library(steam_path, app_id, name, name_size))
        return true;

    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/steamapps/libraryfolders.vdf", steam_path) >= (int)sizeof(path))
        return false;

    size_t size = 0;
    char *data = read_file_alloc(path, &size);
    if (!data) return false;

    LibraryFoldersUserdata library_folders = { app_id, { name, name_size, false } };
    vdf_text_for_each(data, size, library_folders_find_app_callback, &library_folders);
    free(data);
    return library_folders.find_name.found;
}

static bool vdf_binary_read_string(const char *data, size_t size, size_t *index, const char **str) {
    const char *end = memchr(data + *index, '\0', size - *index);
    if (!end) return false;
    *str = data + *index;
    *index = end - data + 1;
    return true;
}

/* shortcuts.vdf is a binary vdf file that contains the non-steam games added to steam */
static bool steam_get_shortcut_name_in_file(const char *data, size_t size, uint32_t shortcut_app_id, char *name, size_t name_size) {
    const unsigned char *udata = (const unsigned char*)data;
    int depth = 0;
    bool has_app_id = false;
    uint32_t app_id = 0;
    const char *app_name = NULL;

    size_t i = 0;
    while (i < size) {
        const unsigned char type = udata[i++];
        if (type == 0x08) { /* End of map */
            if (depth == 2 && has_app_id && app_id == shortcut_app_id && app_name && app_name[0] != '\0') {
                /* Steam uses the executable name as the name when adding a non-steam game */
                size_t app_name_len = strlen(app_name);
                if (app_name_len > 4 && ends_with_case(app_name, app_name_len, ".exe"))
                    app_name_len -= 4;
                snprintf(name, name_size, "%.*s", (int)app_name_len, app_name);
                return true;
            }

            if (depth == 0) return false;
            --depth;
            continue;
        }

        const char *key = NULL;
        if (!vdf_binary_read_string(data, size, &i, &key))
            return false;

        switch (type) {
            case 0x00: { /* Map. Depth 1 is "shortcuts" and depth 2 is a shortcut */
                ++depth;
                if (depth == 2) {
                    has_app_id = false;
                    app_name = NULL;
                }
                break;
            }
            case 0x01: { /* String */
                const char *value = NULL;
                if (!vdf_binary_read_string(data, size, &i, &value))
                    return false;
                if (depth == 2 && strcasecmp(key, "AppName") == 0)
                    app_name = value;
                break;
            }
            case 0x02: /* Int32 */
            case 0x03: { /* Float */
                if (i + 4 > size) return false;
                if (type == 0x02 && depth == 2 && strcasecmp(key, "appid") == 0) {
                    app_id = (uint32_t)udata[i] | ((uint32_t)udata[i + 1] << 8) | ((uint32_t)udata[i + 2] << 16) | ((uint32_t)udata[i + 3] << 24);
                    has_app_id = true;
                }
                i += 4;
                break;
            }
            case 0x07: { /* Uint64 */
                if (i + 8 > size) return false;
                i += 8;
                break;
            }
            default:
                return false;
        }
    }
    return false;
}

static bool steam_get_shortcut_name(const char *steam_path, uint32_t shortcut_app_id, char *name, size_t name_size) {
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/userdata", steam_path) >= (int)sizeof(path))
        return false;

    DIR *dir = opendir(path);
    if (!dir) return false;

    bool found = false;
    struct dirent *entry;
    while (!found && (entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        if (snprintf(path, sizeof(path), "%s/userdata/%s/config/shortcuts.vdf", steam_path, entry->d_name) >= (int)sizeof(path))
            continue;

        size_t size = 0;
        char *data = read_file_alloc(path, &size);
        if (!data)
            continue;

        found = steam_get_shortcut_name_in_file(data, size, shortcut_app_id, name, name_size);
        free(data);
    }

    closedir(dir);
    return found;
}

static bool steam_get_game_name(const char *app_id, const char *steam_base_folder, char *name, size_t name_size) {
    /* Non-steam games added to steam have the shortcut app id in the upper 32 bits of the game id */
    const unsigned long long app_id_num = strtoull(app_id, NULL, 10);
    const uint32_t shortcut_app_id = app_id_num > 0xFFFFFFFFULL ? (uint32_t)(app_id_num >> 32) : (uint32_t)app_id_num;

    const char *home = getenv("HOME");
    char steam_paths[4][PATH_MAX];
    int num_steam_paths = 0;
    if (steam_base_folder && steam_base_folder[0] != '\0')
        snprintf(steam_paths[num_steam_paths++], PATH_MAX, "%s", steam_base_folder);
    if (home) {
        snprintf(steam_paths[num_steam_paths++], PATH_MAX, "%s/.local/share/Steam", home);
        snprintf(steam_paths[num_steam_paths++], PATH_MAX, "%s/.steam/steam", home);
        snprintf(steam_paths[num_steam_paths++], PATH_MAX, "%s/.var/app/com.valvesoftware.Steam/.local/share/Steam", home);
    }

    for (int i = 0; i < num_steam_paths; ++i) {
        if (steam_get_app_name(steam_paths[i], app_id, name, name_size))
            return true;
        if (steam_get_shortcut_name(steam_paths[i], shortcut_app_id, name, name_size))
            return true;
    }
    return false;
}

/* Replaces control characters (such as newline, which would break the output) with spaces and trims the name */
static void sanitize_game_name(char *name) {
    for (char *p = name; *p; ++p) {
        if ((unsigned char)*p < 0x20 || *p == 0x7f)
            *p = ' ';
    }

    char *start = name;
    while (*start == ' ') ++start;
    size_t len = strlen(start);
    while (len > 0 && start[len - 1] == ' ') --len;
    memmove(name, start, len);
    name[len] = '\0';
}

bool is_valid_app_id(const char *app_id) {
    if (!app_id || app_id[0] < '1' || app_id[0] > '9' || strlen(app_id) > 20)
        return false;

    for (const char *p = app_id; *p; ++p) {
        if (*p < '0' || *p > '9')
            return false;
    }
    return true;
}

void game_name_resolve(const char *app_id, const char *steam_base_folder, char *name, size_t name_size) {
    if (name_size == 0)
        return;

    name[0] = '\0';
    if (is_valid_app_id(app_id) && steam_get_game_name(app_id, steam_base_folder, name, name_size))
        sanitize_game_name(name);
    else
        name[0] = '\0';
}
