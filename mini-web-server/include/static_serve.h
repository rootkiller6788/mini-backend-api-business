#ifndef STATIC_SERVE_H
#define STATIC_SERVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "http_core.h"

/* ── Max Constants ──────────────────────────────────────────────────────── */
#define STATIC_MAX_MIME_TYPES    128
#define STATIC_MAX_EXT_LEN        16
#define STATIC_MAX_MIME_LEN       64
#define STATIC_MAX_ROOT_PATH    1024
#define STATIC_DEFAULT_INDEX    "index.html"
#define STATIC_MAX_RANGE_HEADER  256

/* ── MIME Type Entry ────────────────────────────────────────────────────── */
typedef struct {
    char extension[STATIC_MAX_EXT_LEN];
    char mime_type[STATIC_MAX_MIME_LEN];
} MimeEntry;

/* ── Static Server Config ───────────────────────────────────────────────── */
typedef struct {
    char root_dir[STATIC_MAX_ROOT_PATH];
    MimeEntry mime_table[STATIC_MAX_MIME_TYPES];
    int  mime_count;
    bool enable_caching;
    bool enable_range;
    bool directory_listing;
    char index_file[64];
} StaticConfig;

/* ── File Info (cache header helper) ────────────────────────────────────── */
typedef struct {
    char     etag[64];
    char     last_modified[128];
    uint64_t file_size;
    time_t   mtime;
} FileCacheInfo;

/* ── Function Declarations ──────────────────────────────────────────────── */
void static_config_init(StaticConfig *cfg, const char *root_dir);
void static_config_add_mime(StaticConfig *cfg, const char *ext,
                             const char *mime_type);
void static_config_load_default_mimes(StaticConfig *cfg);

const char *static_get_mime_type(const StaticConfig *cfg, const char *path);
bool static_serve_file(const StaticConfig *cfg, const char *url_path,
                       const HttpRequest *req, HttpResponse *res);
bool static_serve_range(const StaticConfig *cfg, const char *url_path,
                        const HttpRequest *req, HttpResponse *res,
                        int64_t range_start, int64_t range_end);

void static_build_etag(const char *path, time_t mtime, uint64_t size,
                        char *buf, size_t buf_sz);
void static_build_cache_headers(const FileCacheInfo *info, HttpResponse *res);
bool static_check_not_modified(const HttpRequest *req,
                                const FileCacheInfo *info);

#endif /* STATIC_SERVE_H */
