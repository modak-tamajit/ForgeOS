/*
 * ForgeOS - Multithreaded HTTP/1.1 Server
 * src/server/server.c
 *
 * Thread-pool model: accept thread → task queue → worker threads.
 * Supports GET/HEAD, static file serving, directory listing, MIME types.
 */

#include "../../include/server.h"
#include "../../include/memory.h"

/* ── MIME Table ──────────────────────────────────────────────────────────── */
static const struct { const char *ext; const char *mime; } s_mime_table[] = {
    { ".html", "text/html; charset=utf-8"        },
    { ".htm",  "text/html; charset=utf-8"        },
    { ".css",  "text/css"                        },
    { ".js",   "application/javascript"          },
    { ".json", "application/json"                },
    { ".xml",  "application/xml"                 },
    { ".txt",  "text/plain; charset=utf-8"       },
    { ".md",   "text/plain; charset=utf-8"       },
    { ".c",    "text/plain; charset=utf-8"       },
    { ".h",    "text/plain; charset=utf-8"       },
    { ".png",  "image/png"                       },
    { ".jpg",  "image/jpeg"                      },
    { ".jpeg", "image/jpeg"                      },
    { ".gif",  "image/gif"                       },
    { ".svg",  "image/svg+xml"                   },
    { ".ico",  "image/x-icon"                    },
    { ".webp", "image/webp"                      },
    { ".pdf",  "application/pdf"                 },
    { ".zip",  "application/zip"                 },
    { ".gz",   "application/gzip"                },
    { ".wasm", "application/wasm"                },
    { NULL,    "application/octet-stream"        },
};

const char *server_mime_type(const char *path) {
    if (!path) return "application/octet-stream";
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    for (int i = 0; s_mime_table[i].ext; i++)
        if (strcasecmp(dot, s_mime_table[i].ext) == 0)
            return s_mime_table[i].mime;
    return "application/octet-stream";
}

const char *server_status_text(http_status_t status) {
    switch (status) {
        case HTTP_200_OK:           return "OK";
        case HTTP_301_MOVED:        return "Moved Permanently";
        case HTTP_304_NOT_MODIFIED: return "Not Modified";
        case HTTP_400_BAD_REQUEST:  return "Bad Request";
        case HTTP_403_FORBIDDEN:    return "Forbidden";
        case HTTP_404_NOT_FOUND:    return "Not Found";
        case HTTP_405_METHOD_NOT_A: return "Method Not Allowed";
        case HTTP_500_SERVER_ERROR: return "Internal Server Error";
        case HTTP_501_NOT_IMPL:     return "Not Implemented";
        default:                    return "Unknown";
    }
}

http_method_t server_parse_method(const char *s) {
    if (!s) return HTTP_UNKNOWN;
    if (strcmp(s, "GET")    == 0) return HTTP_GET;
    if (strcmp(s, "HEAD")   == 0) return HTTP_HEAD;
    if (strcmp(s, "POST")   == 0) return HTTP_POST;
    if (strcmp(s, "PUT")    == 0) return HTTP_PUT;
    if (strcmp(s, "DELETE") == 0) return HTTP_DELETE;
    return HTTP_UNKNOWN;
}

/* ── URL Decode ──────────────────────────────────────────────────────────── */
static void url_decode(const char *src, char *dst, int dstsz) {
    int i = 0, j = 0;
    while (src[i] && j < dstsz - 1) {
        if (src[i] == '%' && isxdigit((unsigned char)src[i+1]) && isxdigit((unsigned char)src[i+2])) {
            char h[3] = { src[i+1], src[i+2], '\0' };
            dst[j++] = (char)strtol(h, NULL, 16);
            i += 3;
        } else if (src[i] == '+') {
            dst[j++] = ' '; i++;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
}

/* ── Path Sanitization ───────────────────────────────────────────────────── */
static bool safe_path(const char *path) {
    /* Prevent directory traversal */
    if (strstr(path, "..")) return false;
    if (path[0] != '/') return false;
    return true;
}

/* ── Request Parser ──────────────────────────────────────────────────────── */
int server_parse_request(const char *raw, size_t len, http_request_t *req) {
    UNUSED(len);
    memset(req, 0, sizeof(*req));
    req->keep_alive = false;

    char buf[SERVER_MAX_REQUEST_SZ];
    strncpy(buf, raw, sizeof(buf) - 1);

    /* Parse request line: METHOD SP path[?query] SP HTTP/1.x */
    char *line = buf;
    char *nl = strpbrk(line, "\r\n");
    if (!nl) return FORGE_ERR;
    *nl = '\0';

    char method_str[16] = "", path_raw[SERVER_MAX_PATH_LEN] = "", version[16] = "";
    if (sscanf(line, "%15s %1023s %15s", method_str, path_raw, version) < 2)
        return FORGE_ERR;

    req->method = server_parse_method(method_str);
    strncpy(req->version, version, sizeof(req->version) - 1);

    /* Split path from query string */
    char *q = strchr(path_raw, '?');
    if (q) {
        *q = '\0';
        strncpy(req->query, q + 1, sizeof(req->query) - 1);
    }
    url_decode(path_raw, req->path, sizeof(req->path));

    /* Parse headers */
    char *cur = nl + 1;
    if (*cur == '\n') cur++;

    while (*cur && *cur != '\r' && *cur != '\n' && req->headers.header_count < SERVER_MAX_HEADERS) {
        nl = strpbrk(cur, "\r\n");
        if (!nl) break;
        *nl = '\0';

        char *colon = strchr(cur, ':');
        if (colon) {
            *colon = '\0';
            char *val = colon + 1;
            while (*val == ' ') val++;
            int idx = req->headers.header_count++;
            strncpy(req->headers.header_names [idx], cur, 127);
            strncpy(req->headers.header_values[idx], val, 511);

            /* Detect keep-alive */
            if (strcasecmp(cur, "Connection") == 0)
                req->keep_alive = (strcasecmp(val, "keep-alive") == 0);
        }
        cur = nl + 1;
        if (*cur == '\n') cur++;
    }

    return FORGE_OK;
}

/* ── Response Sender ─────────────────────────────────────────────────────── */
static void write_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, buf + sent, len - sent);
        if (n <= 0) break;
        sent += n;
    }
}

void server_send_error(int fd, http_status_t status) {
    char body[512];
    int blen = snprintf(body, sizeof(body),
        "<!DOCTYPE html><html><head><title>%d %s</title></head>"
        "<body><h1>%d %s</h1><hr><p>ForgeOS HTTP Server</p></body></html>",
        status, server_status_text(status),
        status, server_status_text(status));

    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "Server: ForgeOS/" FORGEOS_VERSION_STR "\r\n"
        "\r\n",
        status, server_status_text(status), blen);

    write_all(fd, header, hlen);
    write_all(fd, body, blen);
}

void server_send_response(int fd, http_response_t *res) {
    char header[4096];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Server: ForgeOS/" FORGEOS_VERSION_STR "\r\n",
        res->status, server_status_text(res->status));

    /* Add custom headers */
    for (int i = 0; i < res->headers.header_count; i++) {
        int n = snprintf(header + hlen, sizeof(header) - hlen,
            "%s: %s\r\n",
            res->headers.header_names[i],
            res->headers.header_values[i]);
        hlen += n;
    }

    if (res->body_is_file) {
        char clen[64];
        snprintf(clen, sizeof(clen), "Content-Length: %lld\r\n\r\n", (long long)res->file_size);
        strncat(header, clen, sizeof(header) - hlen - 1);
        hlen += strlen(clen);
    } else if (res->body) {
        char clen[64];
        snprintf(clen, sizeof(clen), "Content-Length: %zu\r\n\r\n", res->body_len);
        strncat(header, clen, sizeof(header) - hlen - 1);
        hlen += strlen(clen);
    } else {
        strncat(header, "\r\n", sizeof(header) - hlen - 1);
        hlen += 2;
    }

    write_all(fd, header, hlen);

    if (res->body_is_file && res->file_fd >= 0) {
        char buf[SERVER_READ_BUF_SZ];
        ssize_t n;
        while ((n = read(res->file_fd, buf, sizeof(buf))) > 0)
            write_all(fd, buf, n);
        close(res->file_fd);
    } else if (res->body) {
        write_all(fd, res->body, res->body_len);
        free(res->body);
        res->body = NULL;
    }
}

/* ── Directory Listing ───────────────────────────────────────────────────── */
static char *build_dir_listing(const char *path, const char *url_path) {
    DIR *dir = opendir(path);
    if (!dir) return NULL;

    char *buf = malloc(SERVER_SEND_BUF_SZ);
    if (!buf) { closedir(dir); return NULL; }

    int len = snprintf(buf, SERVER_SEND_BUF_SZ,
        "<!DOCTYPE html><html><head>"
        "<title>Index of %s</title>"
        "<style>body{font-family:monospace;padding:2em;background:#0d1117;color:#c9d1d9}"
        "a{color:#58a6ff}h1{border-bottom:1px solid #30363d;padding-bottom:.5em}"
        "table{width:100%%}td{padding:.2em .8em}tr:hover{background:#161b22}</style>"
        "</head><body><h1>📁 Index of %s</h1>"
        "<table><tr><th>Name</th><th>Size</th><th>Modified</th></tr>"
        "<tr><td><a href='..'>../</a></td><td>-</td><td>-</td></tr>",
        url_path, url_path);

    struct dirent *de;
    while ((de = readdir(dir))) {
        if (de->d_name[0] == '.') continue;
        char full[FORGE_MAX_PATH];
        snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
        struct stat st;
        stat(full, &st);
        bool isdir = S_ISDIR(st.st_mode);
        char size[32], mtime[64];
        if (isdir) strcpy(size, "-");
        else snprintf(size, sizeof(size), "%lld", (long long)st.st_size);
        struct tm *tm = localtime(&st.st_mtime);
        strftime(mtime, sizeof(mtime), "%Y-%m-%d %H:%M", tm);
        len += snprintf(buf + len, SERVER_SEND_BUF_SZ - len,
            "<tr><td><a href='%s%s'>%s%s</a></td><td>%s</td><td>%s</td></tr>",
            de->d_name, isdir ? "/" : "",
            de->d_name, isdir ? "/" : "",
            size, mtime);
    }
    closedir(dir);
    len += snprintf(buf + len, SERVER_SEND_BUF_SZ - len,
        "</table><hr><p><em>ForgeOS/" FORGEOS_VERSION_STR "</em></p></body></html>");
    UNUSED(len);
    return buf;
}

/* ── Request Handler ─────────────────────────────────────────────────────── */
void server_handle_request(http_server_t *srv, int fd, http_request_t *req) {
    pthread_mutex_lock(&srv->stats.lock);
    srv->stats.requests_total++;
    pthread_mutex_unlock(&srv->stats.lock);

    /* Only GET and HEAD */
    if (req->method != HTTP_GET && req->method != HTTP_HEAD) {
        server_send_error(fd, HTTP_405_METHOD_NOT_A);
        pthread_mutex_lock(&srv->stats.lock);
        srv->stats.requests_err++;
        pthread_mutex_unlock(&srv->stats.lock);
        return;
    }

    /* Sanitize path */
    if (!safe_path(req->path)) {
        server_send_error(fd, HTTP_403_FORBIDDEN);
        return;
    }

    /* Build filesystem path */
    char fspath[FORGE_MAX_PATH * 2];
    snprintf(fspath, sizeof(fspath), "%s%s", srv->root_dir, req->path);

    /* Remove trailing slash for stat */
    size_t plen = strlen(fspath);
    if (plen > 1 && fspath[plen-1] == '/') fspath[plen-1] = '\0';

    struct stat st;
    if (stat(fspath, &st) != 0) {
        server_send_error(fd, HTTP_404_NOT_FOUND);
        pthread_mutex_lock(&srv->stats.lock);
        srv->stats.requests_err++;
        pthread_mutex_unlock(&srv->stats.lock);
        forge_log("404 %s %s", req->method == HTTP_GET ? "GET" : "HEAD", req->path);
        return;
    }

    http_response_t res;
    memset(&res, 0, sizeof(res));
    res.status = HTTP_200_OK;
    res.file_fd = -1;

    /* Add common headers */
    int hi = 0;
#define ADD_HDR(n, v) do { \
    strncpy(res.headers.header_names[hi],  n, 127); \
    strncpy(res.headers.header_values[hi], v, 511); \
    hi++; res.headers.header_count = hi; \
} while(0)

    ADD_HDR("Connection", "close");

    /* Directory */
    if (S_ISDIR(st.st_mode)) {
        /* Check for index.html */
        char index_path[FORGE_MAX_PATH * 2];
        snprintf(index_path, sizeof(index_path), "%s/index.html", fspath);
        if (stat(index_path, &st) == 0) {
            strncpy(fspath, index_path, sizeof(fspath) - 1);
        } else if (srv->dir_listing) {
            char *listing = build_dir_listing(fspath, req->path);
            if (!listing) { server_send_error(fd, HTTP_500_SERVER_ERROR); return; }
            ADD_HDR("Content-Type", "text/html; charset=utf-8");
            res.body     = listing;
            res.body_len = strlen(listing);
            server_send_response(fd, &res);
            pthread_mutex_lock(&srv->stats.lock);
            srv->stats.requests_ok++;
            srv->stats.bytes_sent += res.body_len;
            pthread_mutex_unlock(&srv->stats.lock);
            return;
        } else {
            server_send_error(fd, HTTP_403_FORBIDDEN);
            return;
        }
    }

    /* Regular file */
    int file_fd = open(fspath, O_RDONLY);
    if (file_fd < 0) { server_send_error(fd, HTTP_403_FORBIDDEN); return; }

    char size_str[32], mime_str[SERVER_MAX_MIME_LEN];
    snprintf(size_str, sizeof(size_str), "%lld", (long long)st.st_size);
    strncpy(mime_str, server_mime_type(fspath), sizeof(mime_str) - 1);
    ADD_HDR("Content-Type", mime_str);
    ADD_HDR("Content-Length", size_str);

    /* Last-Modified */
    char last_mod[64];
    struct tm *gmt = gmtime(&st.st_mtime);
    strftime(last_mod, sizeof(last_mod), "%a, %d %b %Y %H:%M:%S GMT", gmt);
    ADD_HDR("Last-Modified", last_mod);

    forge_log("200 GET %s (%s bytes)", req->path, size_str);

    if (req->method == HTTP_HEAD) {
        close(file_fd);
        server_send_response(fd, &res);
    } else {
        res.body_is_file = true;
        res.file_fd      = file_fd;
        res.file_size    = st.st_size;
        server_send_response(fd, &res);
    }

    pthread_mutex_lock(&srv->stats.lock);
    srv->stats.requests_ok++;
    srv->stats.bytes_sent += st.st_size;
    pthread_mutex_unlock(&srv->stats.lock);
}

/* ── Client Handler (runs in worker thread) ──────────────────────────────── */
static void handle_client(http_server_t *srv, int client_fd) {
    /* Set timeouts */
    struct timeval tv = { .tv_sec = SERVER_RECV_TIMEOUT_S, .tv_usec = 0 };
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    tv.tv_sec = SERVER_SEND_TIMEOUT_S;
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    char *buf = malloc(SERVER_MAX_REQUEST_SZ);
    if (!buf) { close(client_fd); return; }

    ssize_t total = 0;
    ssize_t n;
    while (total < SERVER_MAX_REQUEST_SZ - 1) {
        n = recv(client_fd, buf + total, SERVER_MAX_REQUEST_SZ - 1 - total, 0);
        if (n <= 0) break;
        total += n;
        /* Simple check: headers ended? */
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n") || strstr(buf, "\n\n")) break;
    }

    pthread_mutex_lock(&srv->stats.lock);
    srv->stats.bytes_recv += total;
    pthread_mutex_unlock(&srv->stats.lock);

    if (total > 0) {
        buf[total] = '\0';
        http_request_t req;
        if (server_parse_request(buf, total, &req) == FORGE_OK)
            server_handle_request(srv, client_fd, &req);
        else
            server_send_error(client_fd, HTTP_400_BAD_REQUEST);
    }

    free(buf);
    close(client_fd);
}

/* ── Thread Pool Worker ──────────────────────────────────────────────────── */
static void *worker_thread(void *arg) {
    http_server_t *srv = (http_server_t *)arg;
    thread_pool_t *pool = &srv->pool;

    while (1) {
        pthread_mutex_lock(&pool->queue_lock);
        while (!pool->queue_head && !pool->shutdown)
            pthread_cond_wait(&pool->queue_cond, &pool->queue_lock);

        if (pool->shutdown && !pool->queue_head) {
            pthread_mutex_unlock(&pool->queue_lock);
            break;
        }

        server_task_t *task = pool->queue_head;
        if (task) {
            pool->queue_head = task->next;
            if (!pool->queue_head) pool->queue_tail = NULL;
            pool->queue_size--;
        }
        pthread_mutex_unlock(&pool->queue_lock);

        if (task) {
            handle_client(srv, task->client_fd);
            free(task);
        }
    }
    return NULL;
}

/* ── Accept Loop ─────────────────────────────────────────────────────────── */
static void *accept_loop(void *arg) {
    http_server_t *srv = (http_server_t *)arg;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    while (srv->running) {
        int client_fd = accept(srv->listen_fd, (struct sockaddr *)&addr, &addrlen);
        if (client_fd < 0) {
            if (!srv->running) break;
            if (errno == EINTR || errno == EAGAIN) continue;
            perror("accept");
            continue;
        }

        server_task_t *task = malloc(sizeof(server_task_t));
        if (!task) { close(client_fd); continue; }
        task->client_fd   = client_fd;
        task->client_addr = addr;
        task->next        = NULL;

        pthread_mutex_lock(&srv->pool.queue_lock);
        if (srv->pool.queue_tail) srv->pool.queue_tail->next = task;
        else                       srv->pool.queue_head = task;
        srv->pool.queue_tail = task;
        srv->pool.queue_size++;
        pthread_cond_signal(&srv->pool.queue_cond);
        pthread_mutex_unlock(&srv->pool.queue_lock);
    }
    return NULL;
}

/* ── Init ────────────────────────────────────────────────────────────────── */
int server_init(http_server_t *srv, int port, const char *root) {
    memset(srv, 0, sizeof(*srv));
    srv->port = port;
    srv->dir_listing = true;
    strncpy(srv->root_dir, root ? root : SERVER_DEFAULT_ROOT, sizeof(srv->root_dir) - 1);

    /* Strip trailing slash from root */
    size_t rlen = strlen(srv->root_dir);
    if (rlen > 1 && srv->root_dir[rlen-1] == '/') srv->root_dir[rlen-1] = '\0';

    /* Socket */
    srv->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->listen_fd < 0) { perror("socket"); return FORGE_ERR; }

    int opt = 1;
    setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port        = htons(port),
    };
    if (bind(srv->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(srv->listen_fd); return FORGE_ERR;
    }
    if (listen(srv->listen_fd, SERVER_BACKLOG) < 0) {
        perror("listen"); close(srv->listen_fd); return FORGE_ERR;
    }

    /* Thread pool */
    pthread_mutex_init(&srv->pool.queue_lock, NULL);
    pthread_cond_init(&srv->pool.queue_cond,  NULL);
    pthread_mutex_init(&srv->stats.lock, NULL);
    srv->stats.start_time = time(NULL);

    return FORGE_OK;
}

int server_start(http_server_t *srv) {
    srv->running = true;

    /* Start workers */
    for (int i = 0; i < SERVER_THREAD_COUNT; i++) {
        if (pthread_create(&srv->pool.threads[i], NULL, worker_thread, srv) != 0) {
            perror("pthread_create"); return FORGE_ERR;
        }
    }

    /* Start accept loop */
    if (pthread_create(&srv->accept_thread, NULL, accept_loop, srv) != 0) {
        perror("pthread_create accept"); return FORGE_ERR;
    }

    return FORGE_OK;
}

void server_stop(http_server_t *srv) {
    srv->running = false;
    close(srv->listen_fd);

    pthread_mutex_lock(&srv->pool.queue_lock);
    srv->pool.shutdown = true;
    pthread_cond_broadcast(&srv->pool.queue_cond);
    pthread_mutex_unlock(&srv->pool.queue_lock);

    for (int i = 0; i < SERVER_THREAD_COUNT; i++)
        pthread_join(srv->pool.threads[i], NULL);

    pthread_join(srv->accept_thread, NULL);
}

void server_destroy(http_server_t *srv) {
    pthread_mutex_destroy(&srv->pool.queue_lock);
    pthread_cond_destroy(&srv->pool.queue_cond);
    pthread_mutex_destroy(&srv->stats.lock);
}

void server_print_stats(http_server_t *srv) {
    server_stats_t *st = &srv->stats;
    time_t uptime = time(NULL) - st->start_time;
    printf(C_BCYN "\n┌─────────── HTTP Server Statistics ───────────┐\n" C_RESET);
    printf("  Port           : " C_BWHT "%d" C_RESET "\n", srv->port);
    printf("  Root           : " C_BWHT "%s" C_RESET "\n", srv->root_dir);
    printf("  Uptime         : " C_BWHT "%lds" C_RESET "\n", uptime);
    printf("  Requests total : " C_BWHT "%llu" C_RESET "\n", (unsigned long long)st->requests_total);
    printf("  Requests OK    : " C_BGRN "%llu" C_RESET "\n", (unsigned long long)st->requests_ok);
    printf("  Requests error : " C_BRED "%llu" C_RESET "\n", (unsigned long long)st->requests_err);
    printf("  Bytes sent     : " C_BWHT "%llu KB" C_RESET "\n", (unsigned long long)(st->bytes_sent / 1024));
    printf("  Bytes recv     : " C_BWHT "%llu KB" C_RESET "\n", (unsigned long long)(st->bytes_recv / 1024));
    printf(C_BCYN "└──────────────────────────────────────────────┘\n" C_RESET);
}

void server_run_interactive(http_server_t *srv) {
    printf(C_BGRN "\n[forge-server] Listening on http://localhost:%d\n" C_RESET, srv->port);
    printf(C_BGRN "[forge-server] Serving: %s\n" C_RESET, srv->root_dir);
    printf(C_DIM  "               Workers: %d threads\n", SERVER_THREAD_COUNT);
    printf("               Press Ctrl-C to stop.\n\n" C_RESET);

    /* Block until SIGINT */
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
    int sig = 0;
    sigwait(&set, &sig);

    printf(C_BYEL "\n[forge-server] Shutting down (signal %d)...\n" C_RESET, sig);
    server_print_stats(srv);
    server_stop(srv);
}

/* ── Entry Point ─────────────────────────────────────────────────────────── */
int server_main(int port, const char *root) {
    http_server_t srv;
    if (server_init(&srv, port, root) != FORGE_OK) {
        forge_err("Failed to initialize server");
        return 1;
    }
    if (server_start(&srv) != FORGE_OK) {
        forge_err("Failed to start server");
        server_destroy(&srv);
        return 1;
    }
    server_run_interactive(&srv);
    server_destroy(&srv);
    return 0;
}
