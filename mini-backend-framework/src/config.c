/*
 * config.c - Configuration Manager Implementation
 *
 * L1: Flat key-value store organized by sections.
 * L2: Twelve-Factor App principle III - env var override.
 * L3: INI-style file parser with comment support.
 * L4: Precedence: explicit > env > default.
 *
 * Reference: Adam Wiggins, "The Twelve-Factor App" (2011).
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char *cfg_trim(char *s) {
    char *end;
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

static CFGEntry *cfg_find_entry(Config *cfg, const char *section, const char *key) {
    int i;
    const char *sec = section ? section : "";
    for (i = 0; i < cfg->count; i++) {
        if (strcmp(cfg->entries[i].section, sec) == 0 &&
            strcmp(cfg->entries[i].key, key) == 0)
            return &cfg->entries[i];
    }
    return NULL;
}

void cfg_init(Config *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(Config));
}

void cfg_enable_env(Config *cfg, const char *prefix) {
    if (!cfg) return;
    cfg->env_override_enabled = true;
    if (prefix) {
        strncpy(cfg->env_prefix, prefix, CFG_MAX_ENV_PREFIX - 1);
        cfg->env_prefix[CFG_MAX_ENV_PREFIX - 1] = '\0';
    }
}

void cfg_set(Config *cfg, const char *section, const char *key, const char *value) {
    CFGEntry *entry;
    const char *sec;
    if (!cfg || !key || !value) return;
    if (cfg->count >= CFG_MAX_KEYS) return;
    sec = section ? section : "";
    entry = cfg_find_entry(cfg, section, key);
    if (entry) {
        strncpy(entry->value, value, CFG_MAX_VALUE - 1);
        entry->value[CFG_MAX_VALUE - 1] = '\0';
        entry->is_set = true;
        return;
    }
    entry = &cfg->entries[cfg->count];
    strncpy(entry->key, key, CFG_MAX_NAME - 1);
    entry->key[CFG_MAX_NAME - 1] = '\0';
    strncpy(entry->value, value, CFG_MAX_VALUE - 1);
    entry->value[CFG_MAX_VALUE - 1] = '\0';
    strncpy(entry->section, sec, CFG_MAX_NAME - 1);
    entry->section[CFG_MAX_NAME - 1] = '\0';
    entry->is_set = true;
    cfg->count++;
}

static const char *cfg_lookup_env(Config *cfg, const char *section, const char *key) {
    char env_name[256];
    char sanitized[CFG_MAX_NAME];
    const char *sec, *env_val;
    int i;
    if (!cfg->env_override_enabled || !cfg->env_prefix[0]) return NULL;
    sec = (section && section[0]) ? section : "";
    for (i = 0; key[i] && i < CFG_MAX_NAME - 1; i++)
        sanitized[i] = (key[i] == '.' || key[i] == '-') ? '_' : (char)toupper((unsigned char)key[i]);
    sanitized[i] = '\0';
    if (sec[0]) {
        char sec_u[CFG_MAX_NAME];
        for (i = 0; sec[i] && i < CFG_MAX_NAME - 1; i++)
            sec_u[i] = (char)toupper((unsigned char)sec[i]);
        sec_u[i] = '\0';
        snprintf(env_name, sizeof(env_name), "%s%s_%s", cfg->env_prefix, sec_u, sanitized);
    } else {
        snprintf(env_name, sizeof(env_name), "%s%s", cfg->env_prefix, sanitized);
    }
    env_val = getenv(env_name);
    return (env_val && env_val[0]) ? env_val : NULL;
}

const char *cfg_get(Config *cfg, const char *section, const char *key,
                    const char *default_val) {
    CFGEntry *entry;
    const char *env_val;
    if (!cfg || !key) return default_val;
    entry = cfg_find_entry(cfg, section, key);
    if (entry && entry->is_set) return entry->value;
    env_val = cfg_lookup_env(cfg, section, key);
    if (env_val) return env_val;
    return default_val;
}

int cfg_get_int(Config *cfg, const char *section, const char *key, int def) {
    const char *val = cfg_get(cfg, section, key, NULL);
    return val ? atoi(val) : def;
}

double cfg_get_float(Config *cfg, const char *section, const char *key, double def) {
    const char *val = cfg_get(cfg, section, key, NULL);
    return val ? atof(val) : def;
}

bool cfg_get_bool(Config *cfg, const char *section, const char *key, bool def) {
    const char *val = cfg_get(cfg, section, key, NULL);
    if (!val) return def;
    if (strcmp(val, "true") == 0 || strcmp(val, "1") == 0 ||
        strcmp(val, "yes") == 0 || strcmp(val, "on") == 0) return true;
    if (strcmp(val, "false") == 0 || strcmp(val, "0") == 0 ||
        strcmp(val, "no") == 0 || strcmp(val, "off") == 0) return false;
    return def;
}

bool cfg_has(Config *cfg, const char *section, const char *key) {
    if (!cfg || !key) return false;
    if (cfg_find_entry(cfg, section, key)) return true;
    return cfg_lookup_env(cfg, section, key) != NULL;
}

int cfg_load_file(Config *cfg, const char *filename) {
    FILE *fp;
    char line[1024];
    char current_section[CFG_MAX_NAME] = "";
    int loaded = 0;
    if (!cfg || !filename) return -1;
    fp = fopen(filename, "r");
    if (!fp) return -1;
    while (fgets(line, sizeof(line), fp)) {
        char *t = cfg_trim(line);
        if (t[0] == '\0' || t[0] == '#' || t[0] == ';') continue;
        if (t[0] == '[') {
            char *end = strchr(t, ']');
            if (end) {
                *end = '\0';
                strncpy(current_section, cfg_trim(t + 1), CFG_MAX_NAME - 1);
                current_section[CFG_MAX_NAME - 1] = '\0';
            }
            continue;
        }
        {
            char *eq = strchr(t, '=');
            if (eq) {
                *eq = '\0';
                char *k = cfg_trim(t);
                char *v = cfg_trim(eq + 1);
                if (k[0] != '\0') {
                    cfg_set(cfg, current_section[0] ? current_section : NULL, k, v);
                    loaded++;
                }
            }
        }
    }
    fclose(fp);
    return loaded;
}

int cfg_dump(const Config *cfg, char *out, int max_len) {
    const char *last_sec = NULL;
    int i, pos = 0, written;
    if (!cfg || !out || max_len <= 0) return -1;
    for (i = 0; i < cfg->count && pos < max_len - 1; i++) {
        const char *sec = cfg->entries[i].section[0] ? cfg->entries[i].section : NULL;
        if (sec != last_sec && (!sec || !last_sec || strcmp(sec, last_sec) != 0)) {
            if (sec) {
                written = snprintf(out + pos, max_len - pos, "[%s]\n", sec);
                if (written < 0 || pos + written >= max_len) break;
                pos += written;
            }
            last_sec = sec;
        }
        written = snprintf(out + pos, max_len - pos, "%s = %s\n",
                          cfg->entries[i].key, cfg->entries[i].value);
        if (written < 0 || pos + written >= max_len) break;
        pos += written;
    }
    out[pos] = '\0';
    return pos;
}

int cfg_section_keys(const Config *cfg, const char *section,
                     char keys[][CFG_MAX_NAME], int max_keys) {
    int i, count = 0;
    const char *sec = section ? section : "";
    if (!cfg || !keys) return 0;
    for (i = 0; i < cfg->count && count < max_keys; i++) {
        if (strcmp(cfg->entries[i].section, sec) == 0) {
            strncpy(keys[count], cfg->entries[i].key, CFG_MAX_NAME - 1);
            keys[count][CFG_MAX_NAME - 1] = '\0';
            count++;
        }
    }
    return count;
}

int cfg_remove(Config *cfg, const char *section, const char *key) {
    CFGEntry *entry;
    int idx, remaining;
    if (!cfg || !key) return -1;
    entry = cfg_find_entry(cfg, section, key);
    if (!entry) return -1;
    idx = (int)(entry - cfg->entries);
    remaining = cfg->count - idx - 1;
    if (remaining > 0)
        memmove(entry, entry + 1, remaining * sizeof(CFGEntry));
    cfg->count--;
    return 0;
}

void cfg_clear(Config *cfg) {
    if (cfg) memset(cfg, 0, sizeof(Config));
}
