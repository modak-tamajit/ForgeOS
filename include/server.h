/*
 * ForgeOS - Multithreaded HTTP/1.1 Server
 * include/server.h
 *
 * Thread-pool based static file server with keep-alive,
 * MIME detection, and basic directory listing.
 */

#ifndef FORGE_SERVER_H
#define FORGE_SERVER_H

#include "forgeos.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>

/* ── Configuration ───────────────────────────────────────────────────────── */
#define SERVER_DEFAULT_PORT     8080
#define SERVER_DEFAULT_ROOT     "./www"
#define SERVER_BACKLOG          128
#define SERVER_THREAD_COUNT     8
#define SERVER_RECV_TIMEOUT_S   10
#define SERVER_SEND_TIMEOUT_S   10
#define SERVER_MAX_REQUEST_SZ   (64 * 1024)   /* 64 KB                     */
#define SERVER_MAX_HEADERS      32
#define SERVER_MAX_PATH_LEN     1024
#define SERVER_MAX_MIME_LEN     64
#define SERVER_READ_BUF_SZ      8192
#define SERVER_SEND_BUF_SZ      (256 * 1024)  /* 256 KB file send buffer   */

/* ── HTTP Status Codes ───────────────────────────────────────────────────── */
typedef enum {
    HTTP_200_OK            = 200,
    HTTP_301_MOVED         = 301,
    HTTP_304_NOT_MODIFIED  = 304,
    HTTP_400_BAD_REQUEST   = 400,
    HTTP_403_FORBIDDEN     = 403,
    HTTP_404_NOT_FOUND     = 404,
    HTTP_405_METHOD_NOT_A  = 405,
    HTTP_500_SERVER_ERROR  = 500,
    HTTP_501_NOT_IMPL      = 501,
} http_status_t;

/* ── HTTP Methods ────────────────────────────────────────────────────────── */
typedef enum {
    HTTP_GET,
    HTTP_HEAD,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_UNKNOWN,
} http_method_t;

/* ── Request ─────────────────────────────────────────────────────────────── */
typedef struct {
    char          header_names [SERVER_MAX_HEADERS][128];
    char          header_values[SERVER_MAX_HEADERS][512];
    int           header_count;
} http_headers_t;

typedef struct {
    http_method_t  method;
    char           path   [SERVER_MAX_PATH_LEN];
    char           version[16];           /* "HTTP/1.0" or "HTTP/1.1"       */
    char           query  [512];
    http_headers_t headers;
    char          *body;
    size_t         body_len;
    bool           keep_alive;
} http_request_t;

/* ── Response ────────────────────────────────────────────────────────────── */
typedef struct {
    http_status_t  status;
    http_headers_t headers;
    char          *body;
    size_t         body_len;
    bool           body_is_file;    /* if true, body holds a filepath       */
    int            file_fd;
    off_t          file_size;
} http_response_t;

/* ── Thread Pool Task ────────────────────────────────────────────────────── */
typedef struct server_task {
    int                  client_fd;
    struct sockaddr_in   client_addr;
    struct server_task  *next;
} server_task_t;

/* ── Thread Pool ─────────────────────────────────────────────────────────── */
typedef struct {
    pthread_t        threads[SERVER_THREAD_COUNT];
    server_task_t   *queue_head;
    server_task_t   *queue_tail;
    int              queue_size;
    pthread_mutex_t  queue_lock;
    pthread_cond_t   queue_cond;
    bool             shutdown;
} thread_pool_t;

/* ── Server Stats ────────────────────────────────────────────────────────── */
typedef struct {
    uint64_t  requests_total;
    uint64_t  requests_ok;
    uint64_t  requests_err;
    uint64_t  bytes_sent;
    uint64_t  bytes_recv;
    time_t    start_time;
    pthread_mutex_t lock;
} server_stats_t;

/* ── Server Context ──────────────────────────────────────────────────────── */
typedef struct {
    int              listen_fd;
    int              port;
    char             root_dir[FORGE_MAX_PATH];
    bool             running;
    bool             dir_listing;
    thread_pool_t    pool;
    server_stats_t   stats;
    pthread_t        accept_thread;
} http_server_t;

/* ── Public API ──────────────────────────────────────────────────────────── */
int   server_init(http_server_t *srv, int port, const char *root);
int   server_start(http_server_t *srv);
void  server_stop(http_server_t *srv);
void  server_destroy(http_server_t *srv);
void  server_print_stats(http_server_t *srv);
void  server_run_interactive(http_server_t *srv);

/* Internal (exposed for testing) */
int          server_parse_request(const char *raw, size_t len, http_request_t *req);
void         server_handle_request(http_server_t *srv, int fd, http_request_t *req);
void         server_send_response(int fd, http_response_t *res);
void         server_send_error(int fd, http_status_t status);
const char  *server_mime_type(const char *path);
const char  *server_status_text(http_status_t status);
http_method_t server_parse_method(const char *s);

#endif /* FORGE_SERVER_H */

/* Entry point for standalone server invocation */
int server_main(int port, const char *root);
