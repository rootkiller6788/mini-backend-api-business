#include "api_version.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

const char* av_strategy_string(av_version_strategy_t s) {
    static const char* names[] = { "URI_PATH", "HEADER", "QUERY_PARAM", "CONTENT_NEG", "CUSTOM" };
    if (s > AV_CUSTOM) return "UNKNOWN";
    return names[s];
}

const char* av_deprecation_level_string(av_deprecation_level_t l) {
    static const char* names[] = { "NONE", "MINOR", "MAJOR", "SUNSET" };
    if (l > AV_SUNSET) return "UNKNOWN";
    return names[l];
}

void av_version_parse(av_version_t* v, const char* str) {
    if (!v || !str) return;
    memset(v, 0, sizeof(*v));
    strncpy(v->version, str, sizeof(v->version) - 1);
    const char* p = str;
    if (*p == 'v' || *p == 'V') p++;
    v->major = atoi(p);
    while (*p >= '0' && *p <= '9') p++;
    if (*p == '.') { p++; v->minor = atoi(p); while (*p >= '0' && *p <= '9') p++; }
    if (*p == '.') { p++; v->patch = atoi(p); while (*p >= '0' && *p <= '9') p++; }
    if (*p == '-') {
        p++;
        v->is_prerelease = true;
        const char* ls = p;
        while (*p && *p != '+' && *p != '.') p++;
        size_t n = (size_t)(p - ls);
        if (n >= sizeof(v->prerelease_label)) n = sizeof(v->prerelease_label) - 1;
        memcpy(v->prerelease_label, ls, n);
        v->prerelease_label[n] = '\0';
    }
}

int av_version_compare(av_version_t a, av_version_t b) {
    if (a.major != b.major) return a.major - b.major;
    if (a.minor != b.minor) return a.minor - b.minor;
    if (a.patch != b.patch) return a.patch - b.patch;
    if (a.is_prerelease && !b.is_prerelease) return -1;
    if (!a.is_prerelease && b.is_prerelease) return 1;
    return strcmp(a.prerelease_label, b.prerelease_label);
}

bool av_version_is_supported(av_version_t v, av_version_t min_ver, av_version_t max_ver) {
    if (av_version_compare(v, min_ver) < 0) return false;
    if (max_ver.major > 0 && av_version_compare(v, max_ver) > 0) return false;
    return true;
}

void av_version_format(av_version_t v, char* buf, size_t len) {
    if (!buf) return;
    int off = snprintf(buf, len, "%d.%d.%d", v.major, v.minor, v.patch);
    if (v.is_prerelease && strlen(v.prerelease_label) > 0)
        snprintf(buf + off, len - off, "-%s", v.prerelease_label);
}

void av_version_bump_major(av_version_t* v) {
    if (!v) return;
    v->major++;
    v->minor = 0;
    v->patch = 0;
    v->is_prerelease = false;
    v->prerelease_label[0] = '\0';
}

void av_version_bump_minor(av_version_t* v) {
    if (!v) return;
    v->minor++;
    v->patch = 0;
    v->is_prerelease = false;
    v->prerelease_label[0] = '\0';
}

void av_version_bump_patch(av_version_t* v) {
    if (!v) return;
    v->patch++;
    v->is_prerelease = false;
    v->prerelease_label[0] = '\0';
}

void av_router_init(av_router_t* r, const char* base_path, av_version_strategy_t strategy) {
    if (!r) return;
    memset(r, 0, sizeof(*r));
    if (base_path) strncpy(r->base_path, base_path, sizeof(r->base_path) - 1);
    r->default_strategy = strategy;
    r->version_count = 0;
    r->deprecation_count = 0;
    r->strict_mode = true;
}

void av_router_register_version(av_router_t* r, const char* version_str) {
    if (!r || r->version_count >= AV_MAX_VERSIONS) return;
    av_version_t v;
    av_version_parse(&v, version_str);
    memcpy(&r->versions[r->version_count++], &v, sizeof(av_version_t));
}

void av_router_set_current(av_router_t* r, const char* version_str) {
    if (!r) return;
    av_version_parse(&r->current_version, version_str);
}

void av_router_set_min_supported(av_router_t* r, const char* version_str) {
    if (!r) return;
    av_version_parse(&r->min_supported, version_str);
}

av_deprecation_t* av_router_set_deprecation(av_router_t* r, const char* version_str,
                                             av_deprecation_level_t level,
                                             const char* date, const char* message) {
    if (!r || r->deprecation_count >= AV_MAX_DEPRECATIONS) return NULL;
    av_deprecation_t* d = &r->deprecations[r->deprecation_count++];
    memset(d, 0, sizeof(*d));
    av_version_parse(&d->version, version_str);
    d->level = level;
    if (date) strncpy(d->deprecation_date, date, sizeof(d->deprecation_date) - 1);
    if (message) strncpy(d->message, message, sizeof(d->message) - 1);
    return d;
}

void av_router_set_sunset(av_router_t* r, const char* version_str,
                          const char* sunset_date, const char* link) {
    if (!r) return;
    av_deprecation_t* d = av_router_set_deprecation(r, version_str, AV_SUNSET, sunset_date, "This API version has been sunset.");
    if (d && link) strncpy(d->sunset_link, link, sizeof(d->sunset_link) - 1);
}

bool av_router_parse_request(av_router_t* r, av_request_t* req, const char* uri,
                              const char* accept_header, const char* query_v) {
    if (!r || !req || !uri) return false;
    memset(req, 0, sizeof(*req));
    strncpy(req->uri, uri, sizeof(req->uri) - 1);

    av_version_strategy_t strategy = r->default_strategy;

    if (strategy == AV_URI_PATH) {
        const char* vs = strstr(uri, "/v");
        if (!vs) { req->is_valid = false; return false; }
        vs += 2;
        const char* ve = vs;
        while (*ve >= '0' && *ve <= '9') ve++;
        char vbuf[16];
        size_t vlen = (size_t)(ve - vs);
        if (vlen >= sizeof(vbuf)) vlen = sizeof(vbuf) - 1;
        memcpy(vbuf, vs, vlen);
        vbuf[vlen] = '\0';
        av_version_parse(&req->version, vbuf);
        strncpy(req->raw_version, vbuf, sizeof(req->raw_version) - 1);
        req->strategy = AV_URI_PATH;
        req->is_valid = true;
    } else if (strategy == AV_HEADER && accept_header) {
        const char* vs = strstr(accept_header, "version=");
        if (vs) {
            vs += 8;
            char vbuf[16];
            const char* ve = vs;
            while ((*ve >= '0' && *ve <= '9') || *ve == '.') ve++;
            size_t vlen = (size_t)(ve - vs);
            if (vlen >= sizeof(vbuf)) vlen = sizeof(vbuf) - 1;
            memcpy(vbuf, vs, vlen);
            vbuf[vlen] = '\0';
            av_version_parse(&req->version, vbuf);
            strncpy(req->raw_version, vbuf, sizeof(req->raw_version) - 1);
            req->is_valid = true;
        }
        req->strategy = AV_HEADER;
    } else if (strategy == AV_QUERY_PARAM && query_v) {
        av_version_parse(&req->version, query_v);
        strncpy(req->raw_version, query_v, sizeof(req->raw_version) - 1);
        req->strategy = AV_QUERY_PARAM;
        req->is_valid = true;
    }

    return req->is_valid;
}

bool av_router_match(av_router_t* r, av_request_t* req) {
    if (!r || !req) return false;
    for (int32_t i = 0; i < r->version_count; i++) {
        if (av_version_compare(req->version, r->versions[i]) == 0) return true;
    }
    return false;
}

const av_deprecation_t* av_router_check_deprecation(av_router_t* r, av_request_t* req) {
    if (!r || !req) return NULL;
    for (int32_t i = 0; i < r->deprecation_count; i++) {
        if (av_version_compare(req->version, r->deprecations[i].version) == 0 &&
            r->deprecations[i].level > AV_NONE) {
            return &r->deprecations[i];
        }
    }
    return NULL;
}

bool av_router_is_sunset(av_router_t* r, av_request_t* req) {
    if (!r || !req) return false;
    for (int32_t i = 0; i < r->deprecation_count; i++) {
        if (av_version_compare(req->version, r->deprecations[i].version) == 0 &&
            r->deprecations[i].level == AV_SUNSET) {
            return true;
        }
    }
    return false;
}

char* av_build_sunset_header(av_deprecation_t* dep, char* buf, size_t len) {
    if (!dep || !buf) return NULL;
    snprintf(buf, len, "Sunset: %s", dep->sunset_date[0] ? dep->sunset_date : "unknown");
    return buf;
}

char* av_build_deprecation_header(av_deprecation_t* dep, char* buf, size_t len) {
    if (!dep || !buf) return NULL;
    snprintf(buf, len, "Deprecation: true");
    if (dep->sunset_date[0]) {
        size_t l = strlen(buf);
        snprintf(buf + l, len - l, "; Sunset=%s", dep->sunset_date);
    }
    return buf;
}

char* av_build_version_uri(av_router_t* r, const char* version_str, const char* resource, char* buf, size_t len) {
    if (!r || !buf) return NULL;
    if (r->default_strategy == AV_URI_PATH) {
        snprintf(buf, len, "%s/v%s/%s",
                 r->base_path, version_str ? version_str : "1", resource ? resource : "");
    } else {
        snprintf(buf, len, "%s/%s?v=%s",
                 r->base_path, resource ? resource : "", version_str ? version_str : "1");
    }
    return buf;
}

bool av_version_is_prerelease(av_version_t v) {
    return v.is_prerelease;
}

bool av_version_has_major_change(av_version_t a, av_version_t b) {
    return a.major != b.major;
}

bool av_version_has_minor_change(av_version_t a, av_version_t b) {
    return a.major == b.major && a.minor != b.minor;
}

bool av_version_is_backward_compatible(av_version_t a, av_version_t b) {
    if (a.major == 0) {
        return a.major == b.major && a.minor == b.minor;
    }
    return a.major == b.major && a.minor <= b.minor;
}

bool av_range_parse(const char* str, av_version_range_t* range) {
    if (!str || !range) return false;
    memset(range, 0, sizeof(*range));

    const char* p = str;
    while (*p == ' ' || *p == 'v' || *p == 'V') p++;

    if (*p == '^') {
        range->type = AV_RANGE_CARET;
        p++;
        av_version_parse(&range->lower, p);
        range->upper = range->lower;
        if (range->lower.major > 0) {
            range->upper.major = range->lower.major + 1;
            range->upper.minor = 0;
            range->upper.patch = 0;
            range->upper.is_prerelease = false;
        } else if (range->lower.minor > 0) {
            range->upper.minor = range->lower.minor + 1;
            range->upper.patch = 0;
        } else {
            range->upper.patch = range->lower.patch + 1;
        }
        range->has_upper = true;
        return true;
    }

    if (*p == '~') {
        range->type = AV_RANGE_TILDE;
        p++;
        av_version_parse(&range->lower, p);
        range->upper = range->lower;
        range->upper.minor = range->lower.minor + 1;
        range->upper.patch = 0;
        range->upper.is_prerelease = false;
        range->has_upper = true;
        return true;
    }

    if (*p == '>') {
        p++;
        if (*p == '=') p++;
        range->type = AV_RANGE_GTE;
        av_version_parse(&range->lower, p);
        range->has_upper = false;
        return true;
    }

    if (*p == '=') p++;
    range->type = AV_RANGE_EQ;
    av_version_parse(&range->lower, p);
    range->upper = range->lower;
    range->has_upper = true;
    return true;
}

bool av_range_satisfies(av_version_range_t* range, av_version_t version) {
    if (!range) return false;
    int cmp_lower = av_version_compare(version, range->lower);
    if (cmp_lower < 0) return false;
    if (range->has_upper) {
        int cmp_upper = av_version_compare(version, range->upper);
        if (cmp_upper >= 0) return false;
    }
    return true;
}

char* av_changelog_render(av_router_t* r, char* buf, size_t len) {
    if (!r || !buf) return NULL;
    int off = snprintf(buf, len, "# API Changelog\n\n");
    if (r->version_count == 0) {
        off += snprintf(buf + off, len - off, "No versions registered.\n");
        return buf;
    }

    for (int32_t i = r->deprecation_count - 1; i >= 0; i--) {
        av_deprecation_t* d = &r->deprecations[i];
        av_version_t v = d->version;
        char vstr[32];
        av_version_format(v, vstr, sizeof(vstr));
        off += snprintf(buf + off, len - off, "## %s — %s %s\n",
                        vstr,
                        av_deprecation_level_string(d->level),
                        d->level == AV_SUNSET ? "SUNSET" : "DEPRECATED");
        if (d->deprecation_date[0])
            off += snprintf(buf + off, len - off, "- **Date**: %s\n", d->deprecation_date);
        if (d->sunset_date[0])
            off += snprintf(buf + off, len - off, "- **Sunset**: %s\n", d->sunset_date);
        if (d->message[0])
            off += snprintf(buf + off, len - off, "- **Details**: %s\n", d->message);
        if (d->alternative[0])
            off += snprintf(buf + off, len - off, "- **Alternative**: %s\n", d->alternative);
        off += snprintf(buf + off, len - off, "\n");
    }

    if (r->current_version.major > 0) {
        char cur[32];
        av_version_format(r->current_version, cur, sizeof(cur));
        off += snprintf(buf + off, len - off, "## Current: %s\n\n", cur);
    }

    return buf;
}
