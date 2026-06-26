#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/*
 * L1: Core Definitions ¡ª Configuration Manager
 *
 * A hierarchical configuration system supporting:
 *   - Key-value pairs organized in named sections
 *   - Environment variable override (12-factor app principle)
 *   - Default values
 *   - Type-safe getters (string, int, float, bool)
 *
 * L2: Twelve-Factor App (Adam Wiggins, 2011) ¡ª Config should be
 * stored in environment variables, not in code. This module
 * supports env var override with a prefix convention.
 *
 * L4: Principle of Least Astonishment ¡ª configuration lookup
 * follows a defined precedence:
 *   1. Explicit set (highest priority)
 *   2. Environment variable override
 *   3. Default value (lowest priority)
 */

#define CFG_MAX_SECTIONS  32
#define CFG_MAX_KEYS      256
#define CFG_MAX_NAME      64
#define CFG_MAX_VALUE     512
#define CFG_MAX_ENV_PREFIX 32

/* A single key-value pair with metadata */
typedef struct {
    char key[CFG_MAX_NAME];
    char value[CFG_MAX_VALUE];
    char section[CFG_MAX_NAME]; /* empty = global section */
    bool is_set;                 /* explicitly set (not default) */
} CFGEntry;

typedef struct {
    CFGEntry entries[CFG_MAX_KEYS];
    int      count;
    char     env_prefix[CFG_MAX_ENV_PREFIX]; /* e.g., "APP_" */
    bool     env_override_enabled;
} Config;

/* Initialize config manager */
void cfg_init(Config *cfg);

/* Enable env var override with given prefix. Env vars named
 * PREFIX_SECTION_KEY override config values. */
void cfg_enable_env(Config *cfg, const char *prefix);

/* Set a configuration value in a section (section can be "" or NULL for global).
 * Overwrites existing key in same section. */
void cfg_set(Config *cfg, const char *section, const char *key, const char *value);

/* Get a string value. Falls back to default_val if not set.
 * Checks: 1) explicit set, 2) env var, 3) default_val */
const char *cfg_get(Config *cfg, const char *section, const char *key,
                    const char *default_val);

/* Typed getters with defaults */
int    cfg_get_int(Config *cfg, const char *section, const char *key, int def);
double cfg_get_float(Config *cfg, const char *section, const char *key, double def);
bool   cfg_get_bool(Config *cfg, const char *section, const char *key, bool def);

/* Check if a key exists (explicitly set or env var) */
bool   cfg_has(Config *cfg, const char *section, const char *key);

/* L2: Load key=value pairs from a file. Format:
 *   [section]
 *   key = value
 *   # comment
 * Lines starting with # or ; are comments.
 * Returns number of keys loaded, -1 on file error. */
int    cfg_load_file(Config *cfg, const char *filename);

/* L3: Dump all configuration to a string buffer (for debugging/export) */
int    cfg_dump(const Config *cfg, char *out, int max_len);

/* Get all keys in a section. Returns count. */
int    cfg_section_keys(const Config *cfg, const char *section,
                        char keys[][CFG_MAX_NAME], int max_keys);

/* Remove a key from a section */
int    cfg_remove(Config *cfg, const char *section, const char *key);

/* Clear all configuration */
void   cfg_clear(Config *cfg);

#endif
