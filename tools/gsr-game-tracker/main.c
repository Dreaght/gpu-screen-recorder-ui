#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/connector.h>
#include <linux/cn_proc.h>

#define MAX_GAMES        32
#define ENVIRON_BUF_SIZE (64 * 1024)
#define CMDLINE_BUF_SIZE 4096
#define RECV_BUF_SIZE    8192

static pid_t games[MAX_GAMES]; /* 0 = empty slot */
static int   game_count = 0;
static char  environ_buf[ENVIRON_BUF_SIZE];
static char  cmdline_buf[CMDLINE_BUF_SIZE];
static char  recv_buf[RECV_BUF_SIZE];

static int find_slot_by_pid(pid_t pid) {
    for (int i = 0; i < MAX_GAMES; i++) {
        if (games[i] == pid)
            return i;
    }
    return -1;
}

static int find_free_slot(void) {
    for (int i = 0; i < MAX_GAMES; i++) {
        if (!games[i])
            return i;
    }
    return -1;
}

static void add_game(pid_t pid) {
    int slot = find_free_slot();
    if (slot < 0) return;
    games[slot] = pid;
    if (game_count++ == 0) {
        printf("Game launched\n");
        fflush(stdout);
    }
}

static void remove_game(pid_t pid) {
    int slot = find_slot_by_pid(pid);
    if (slot < 0) return;
    games[slot] = 0;
    if (--game_count == 0) {
        printf("Game exited\n");
        fflush(stdout);
    }
}

static ssize_t read_file(const char *path, char *buf, size_t size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, size - 1);
    close(fd);
    if (n > 0) buf[n] = '\0';
    return n;
}

/* Return pointer to value inside null-separated environ block, or NULL. */
static const char *env_get(const char *env, ssize_t size, const char *key) {
    size_t      klen = strlen(key);
    const char *p    = env;
    const char *end  = env + size;
    while (p < end) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=')
            return p + klen + 1;
        size_t elen = strnlen(p, (size_t)(end - p));
        p += elen + 1;
    }
    return NULL;
}

static int is_wine_binary(const char *argv0) {
    const char *base = strrchr(argv0, '/');
    base = base ? base + 1 : argv0;
    return strcmp(base, "wine") == 0 ||
           strcmp(base, "wine64") == 0 ||
           strcmp(base, "wine-preloader") == 0 ||
           strcmp(base, "wine64-preloader") == 0;
}

static int has_game_arch_suffix(const char *argv0) {
    const char *base = strrchr(argv0, '/');
    base = base ? base + 1 : argv0;
    size_t len = strlen(base);
    static const char *suffixes[] = { ".x86_64", ".x64", ".x86" };
    for (int i = 0; i < 3; i++) {
        size_t slen = strlen(suffixes[i]);
        if (len > slen && strcmp(base + len - slen, suffixes[i]) == 0)
            return 1;
    }
    return 0;
}

static void check_process(pid_t pid) {
    if (find_slot_by_pid(pid) >= 0) return;

    char    path[64];
    ssize_t env_n, cmd_n;
    bool is_steam_fossilize = false;

    snprintf(path, sizeof(path), "/proc/%d/environ", pid);
    env_n = read_file(path, environ_buf, sizeof(environ_buf));

    if (env_n > 0) {
        is_steam_fossilize = env_get(environ_buf, env_n, "STEAM_FOSSILIZE_DUMP_PATH_READ_ONLY");
        const char *appid = env_get(environ_buf, env_n, "SteamAppId");
        if (!appid) appid = env_get(environ_buf, env_n, "SteamGameId");
        if (appid && appid[0] >= '1' && appid[0] <= '9') {
            add_game(pid);
            return;
        }
    }

    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    cmd_n = read_file(path, cmdline_buf, sizeof(cmdline_buf));

    if (cmd_n > 0 && (is_wine_binary(cmdline_buf) || has_game_arch_suffix(cmdline_buf)) && !is_steam_fossilize)
        add_game(pid);
}

static void handle_proc_event(const struct proc_event *ev) {
    switch (ev->what) {
    case PROC_EVENT_EXEC:
        check_process(ev->event_data.exec.process_tgid);
        break;
    case PROC_EVENT_EXIT:
        /* Only act when the whole process (not just a thread) exits */
        if (ev->event_data.exit.process_pid == ev->event_data.exit.process_tgid)
            remove_game(ev->event_data.exit.process_tgid);
        break;
    default:
        break;
    }
}

static void process_netlink_msg(const char *buf, size_t len) {
    const struct nlmsghdr *nl = (const struct nlmsghdr *)buf;
    for (; NLMSG_OK(nl, (unsigned int)len); nl = NLMSG_NEXT(nl, len)) {
        if (nl->nlmsg_type == NLMSG_NOOP  ||
            nl->nlmsg_type == NLMSG_ERROR ||
            nl->nlmsg_type == NLMSG_OVERRUN)
            continue;
        if (nl->nlmsg_len < NLMSG_HDRLEN + sizeof(struct cn_msg))
            continue;
        const struct cn_msg *cn = (const struct cn_msg *)NLMSG_DATA(nl);
        if (cn->id.idx != CN_IDX_PROC || cn->id.val != CN_VAL_PROC)
            continue;
        if (cn->len < sizeof(struct proc_event))
            continue;
        handle_proc_event((const struct proc_event *)cn->data);
    }
}

static int send_mcast_op(int sock, enum proc_cn_mcast_op op) {
    /* cn_msg ends with data[0], so we can't place fields after it in a struct.
     * Use a flat buffer and fill via pointers instead. */
    char buf[NLMSG_SPACE(sizeof(struct cn_msg) + sizeof(enum proc_cn_mcast_op))];
    memset(buf, 0, sizeof(buf));

    struct nlmsghdr *nl = (struct nlmsghdr *)buf;
    nl->nlmsg_len  = sizeof(buf);
    nl->nlmsg_type = NLMSG_DONE;
    nl->nlmsg_pid  = (unsigned int)getpid();

    struct cn_msg *cn = (struct cn_msg *)NLMSG_DATA(nl);
    cn->id.idx = CN_IDX_PROC;
    cn->id.val = CN_VAL_PROC;
    cn->len    = sizeof(enum proc_cn_mcast_op);
    memcpy(cn->data, &op, sizeof(op));

    return send(sock, buf, sizeof(buf), 0) < 0 ? -1 : 0;
}

static int setup_netlink(void) {
    int sock = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
    if (sock < 0) {
        perror("socket(AF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR)");
        return -1;
    }

    struct sockaddr_nl sa;
    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;
    sa.nl_groups = CN_IDX_PROC;
    sa.nl_pid    = (unsigned int)getpid();

    if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("bind");
        close(sock);
        return -1;
    }

    if (send_mcast_op(sock, PROC_CN_MCAST_LISTEN) < 0) {
        perror("send PROC_CN_MCAST_LISTEN");
        close(sock);
        return -1;
    }

    return sock;
}

static void scan_existing_processes(void) {
    DIR *d = opendir("/proc");
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *s = ent->d_name;
        if (*s < '1' || *s > '9') continue;
        pid_t pid = 0;
        for (; *s; s++) {
            if (*s < '0' || *s > '9') { pid = 0; break; }
            pid = pid * 10 + (*s - '0');
        }
        if (pid > 0) check_process(pid);
    }
    closedir(d);
}

int main(void) {
    memset(games, 0, sizeof(games));

    int sock = setup_netlink();
    if (sock < 0) return 1;

    scan_existing_processes();

    for (;;) {
        ssize_t n = recv(sock, recv_buf, sizeof(recv_buf), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("recv");
            break;
        }
        process_netlink_msg(recv_buf, (size_t)n);
    }

    send_mcast_op(sock, PROC_CN_MCAST_IGNORE);
    close(sock);
    return 0;
}
