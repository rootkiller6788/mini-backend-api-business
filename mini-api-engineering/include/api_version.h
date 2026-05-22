#ifndef API_VERSION_H
#define API_VERSION_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define AV_MAX_URI_LEN        2048
#define AV_MAX_VERSION_LEN    16
#define AV_MAX_HEADER_LEN     256
#define AV_MAX_DATE_LEN       64
#define AV_MAX_SUNSET_LINK    512
#define AV_MAX_DEPRECATIONS   32
#define AV_MAX_VERSIONS       16

typedef enum {
    AV_URI_PATH    = 0,
    AV_HEADER      = 1,
    AV_QUERY_PARAM = 2,
    AV_CONTENT_NEG = 3,
    AV_CUSTOM      = 4
} av_version_strategy_t;

typedef enum {
    AV_NONE        = 0,
    AV_MINOR       = 1,
    AV_MAJOR       = 2,
    AV_SUNSET      = 3
} av_deprecation_level_t;

typedef struct {
    char          version[AV_MAX_VERSION_LEN];
    int32_t       major;
    int32_t       minor;
    int32_t       patch;
    bool          is_prerelease;
    char          prerelease_label[32];
} av_version_t;

typedef struct {
    char                uri[AV_MAX_URI_LEN];
    av_version_t        version;
    char                raw_version[AV_MAX_VERSION_LEN];
    av_version_strategy_t strategy;
    bool                is_valid;
} av_request_t;

typedef struct {
    av_version_t          version;
    av_deprecation_level_t level;
    char                 deprecation_date[AV_MAX_DATE_LEN];
    char                 sunset_date[AV_MAX_DATE_LEN];
    char                 sunset_link[AV_MAX_SUNSET_LINK];
    char                 message[512];
    char                 alternative[AV_MAX_URI_LEN];
} av_deprecation_t;

typedef struct {
    char               base_path[AV_MAX_URI_LEN];
    av_version_strategy_t default_strategy;
    av_version_t        versions[AV_MAX_VERSIONS];
    int32_t             version_count;
    av_deprecation_t    deprecations[AV_MAX_DEPRECATIONS];
    int32_t             deprecation_count;
    av_version_t        current_version;
    av_version_t        min_supported;
    bool                strict_mode;
} av_router_t;

const char* av_strategy_string(av_version_strategy_t s);
const char* av_deprecation_level_string(av_deprecation_level_t l);

void av_version_parse(av_version_t* v, const char* str);
int  av_version_compare(av_version_t a, av_version_t b);
bool av_version_is_supported(av_version_t v, av_version_t min_ver, av_version_t max_ver);
void av_version_format(av_version_t v, char* buf, size_t len);
void av_version_bump_major(av_version_t* v);
void av_version_bump_minor(av_version_t* v);
void av_version_bump_patch(av_version_t* v);

void av_router_init(av_router_t* r, const char* base_path, av_version_strategy_t strategy);
void av_router_register_version(av_router_t* r, const char* version_str);
void av_router_set_current(av_router_t* r, const char* version_str);
void av_router_set_min_supported(av_router_t* r, const char* version_str);

av_deprecation_t* av_router_set_deprecation(av_router_t* r, const char* version_str,
                                             av_deprecation_level_t level,
                                             const char* date, const char* message);
void av_router_set_sunset(av_router_t* r, const char* version_str,
                          const char* sunset_date, const char* link);

bool av_router_parse_request(av_router_t* r, av_request_t* req, const char* uri,
                              const char* accept_header, const char* query_v);
bool av_router_match(av_router_t* r, av_request_t* req);

const av_deprecation_t* av_router_check_deprecation(av_router_t* r, av_request_t* req);
bool av_router_is_sunset(av_router_t* r, av_request_t* req);

char* av_build_sunset_header(av_deprecation_t* dep, char* buf, size_t len);
char* av_build_deprecation_header(av_deprecation_t* dep, char* buf, size_t len);
char* av_build_version_uri(av_router_t* r, const char* version_str, const char* resource, char* buf, size_t len);

#endif
