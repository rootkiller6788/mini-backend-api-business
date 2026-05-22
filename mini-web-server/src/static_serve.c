#include "static_serve.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const MimeEntry g_default_mimes[] = {
    {".html",   "text/html; charset=utf-8"},
    {".htm",    "text/html; charset=utf-8"},
    {".css",    "text/css; charset=utf-8"},
    {".js",     "application/javascript; charset=utf-8"},
    {".json",   "application/json; charset=utf-8"},
    {".png",    "image/png"},
    {".jpg",    "image/jpeg"},
    {".jpeg",   "image/jpeg"},
    {".gif",    "image/gif"},
    {".svg",    "image/svg+xml"},
    {".ico",    "image/x-icon"},
    {".webp",   "image/webp"},
    {".mp4",    "video/mp4"},
    {".webm",   "video/webm"},
    {".mp3",    "audio/mpeg"},
    {".wav",    "audio/wav"},
    {".ogg",    "audio/ogg"},
    {".pdf",    "application/pdf"},
    {".zip",    "application/zip"},
    {".gz",     "application/gzip"},
    {".tar",    "application/x-tar"},
    {".txt",    "text/plain; charset=utf-8"},
    {".xml",    "application/xml; charset=utf-8"},
    {".woff2",  "font/woff2"},
    {".ttf",    "font/ttf"},
    {"",        "application/octet-stream"},
};

void static_config_init(StaticConfig *cfg, const char *root_dir) {
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->root_dir, root_dir, STATIC_MAX_ROOT_PATH - 1);
    cfg->enable_caching    = true;
    cfg->enable_range      = true;
    cfg->directory_listing = false;
    strncpy(cfg->index_file, STATIC_DEFAULT_INDEX, sizeof(cfg->index_file) - 1);
}

void static_config_add_mime(StaticConfig *cfg, const char *ext,
                             const char *mime_type) {
    if (cfg->mime_count >= STATIC_MAX_MIME_TYPES) return;
    strncpy(cfg->mime_table[cfg->mime_count].extension, ext,
            STATIC_MAX_EXT_LEN - 1);
    strncpy(cfg->mime_table[cfg->mime_count].mime_type, mime_type,
            STATIC_MAX_MIME_LEN - 1);
    cfg->mime_count++;
}

void static_config_load_default_mimes(StaticConfig *cfg) {
    size_t n = sizeof(g_default_mimes) / sizeof(g_default_mimes[0]);
    for (size_t i = 0; i < n; i++) {
        static_config_add_mime(cfg, g_default_mimes[i].extension,
                               g_default_mimes[i].mime_type);
    }
}

const char *static_get_mime_type(const StaticConfig *cfg, const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";

    for (int i = 0; i < cfg->mime_count; i++) {
        if (strcasecmp(dot, cfg->mime_table[i].extension) == 0)
            return cfg->mime_table[i].mime_type;
    }
    return "application/octet-stream";
}

static bool build_full_path(const StaticConfig *cfg, const char *url_path,
                             char *out, size_t out_sz) {
    int n = snprintf(out, out_sz, "%s%s", cfg->root_dir, url_path);
    if (n < 0 || (size_t)n >= out_sz) return false;

    size_t len = strlen(out);
    while (len > 0 && (out[len - 1] == '/' || out[len - 1] == '\\')) {
        out[--len] = '\0';
    }
    if (len == 0) {
        strncpy(out, cfg->index_file, out_sz - 1);
    }
    return true;
}

bool static_serve_file(const StaticConfig *cfg, const char *url_path,
                        const HttpRequest *req, HttpResponse *res) {
    char fpath[STATIC_MAX_ROOT_PATH + 256];
    if (!build_full_path(cfg, url_path, fpath, sizeof(fpath))) return false;

    struct stat st;
    if (stat(fpath, &st) != 0) {
        http_response_set_status(res, HTTP_STATUS_NOT_FOUND);
        http_response_set_body_str(res, "404 Not Found");
        return true;
    }

    if (S_ISDIR(st.st_mode)) {
        char ipath[STATIC_MAX_ROOT_PATH + 512];
        snprintf(ipath, sizeof(ipath), "%s/%s", fpath, cfg->index_file);
        if (stat(ipath, &st) == 0) {
            strncpy(fpath, ipath, sizeof(fpath) - 1);
        } else {
            http_response_set_status(res, HTTP_STATUS_FORBIDDEN);
            http_response_set_body_str(res, "403 Forbidden");
            return true;
        }
    }

    const char *mime = static_get_mime_type(cfg, fpath);
    http_response_add_header(res, "Content-Type", mime);
    http_response_add_header(res, "Accept-Ranges", "bytes");

    FileCacheInfo info;
    info.file_size = (uint64_t)st.st_size;
    info.mtime     = st.st_mtime;
    static_build_etag(fpath, st.st_mtime, (uint64_t)st.st_size,
                       info.etag, sizeof(info.etag));
    strftime(info.last_modified, sizeof(info.last_modified),
             "%a, %d %b %Y %H:%M:%S GMT", gmtime(&st.st_mtime));

    if (cfg->enable_caching) {
        static_build_cache_headers(&info, res);
        if (static_check_not_modified(req, &info)) {
            http_response_set_status(res, HTTP_STATUS_NOT_MODIFIED);
            return true;
        }
    }

    const char *range_hdr = http_request_get_header(req, "Range");
    if (cfg->enable_range && range_hdr) {
        int64_t rstart = 0, rend = -1;
        if (sscanf(range_hdr, "bytes=%lld-%lld", &rstart, &rend) >= 1) {
            if (rend < 0) rend = (int64_t)info.file_size - 1;
            return static_serve_range(cfg, fpath, req, res, rstart, rend);
        }
    }

    FILE *fp = fopen(fpath, "rb");
    if (!fp) return false;

    fseek(fp, 0, SEEK_END);
    long fsz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *buf = malloc((size_t)fsz + 1);
    if (!buf) { fclose(fp); return false; }
    size_t rd = fread(buf, 1, (size_t)fsz, fp);
    fclose(fp);

    http_response_set_status(res, HTTP_STATUS_OK);
    http_response_set_body(res, buf, rd);
    free(buf);
    return true;
}

bool static_serve_range(const StaticConfig *cfg, const char *path,
                         const HttpRequest *req, HttpResponse *res,
                         int64_t range_start, int64_t range_end) {
    (void)cfg;
    (void)req;
    struct stat st;
    if (stat(path, &st) != 0) return false;

    if (range_start < 0) range_start = 0;
    if (range_end >= (int64_t)st.st_size)
        range_end = (int64_t)st.st_size - 1;
    if (range_start > range_end) {
        http_response_set_status(res, HTTP_STATUS_RANGE_NOT_SATISFIABLE);
        return true;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) return false;

    int64_t chunk_size = range_end - range_start + 1;
    char *buf = malloc((size_t)chunk_size);
    if (!buf) { fclose(fp); return false; }

    fseek(fp, (long)range_start, SEEK_SET);
    size_t rd = fread(buf, 1, (size_t)chunk_size, fp);
    fclose(fp);

    char range_val[128];
    snprintf(range_val, sizeof(range_val), "bytes %lld-%lld/%lld",
             range_start, range_end, (long long)st.st_size);

    http_response_set_status(res, HTTP_STATUS_PARTIAL_CONTENT);
    http_response_add_header(res, "Content-Range", range_val);
    http_response_set_body(res, buf, rd);
    free(buf);
    return true;
}

void static_build_etag(const char *path, time_t mtime, uint64_t size,
                        char *buf, size_t buf_sz) {
    snprintf(buf, buf_sz, "\"%lx-%llx\"",
             (unsigned long)mtime, (unsigned long long)size);
}

void static_build_cache_headers(const FileCacheInfo *info, HttpResponse *res) {
    http_response_add_header(res, "ETag", info->etag);
    http_response_add_header(res, "Last-Modified", info->last_modified);
    http_response_add_header(res, "Cache-Control", "public, max-age=3600");
}

bool static_check_not_modified(const HttpRequest *req,
                                const FileCacheInfo *info) {
    const char *ims = http_request_get_header(req, "If-Modified-Since");
    if (ims && strcmp(ims, info->last_modified) >= 0) return true;

    const char *inm = http_request_get_header(req, "If-None-Match");
    if (inm && strcmp(inm, info->etag) == 0) return true;

    return false;
}
