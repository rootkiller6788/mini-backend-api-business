#ifndef CONFIG_CENTER_H
#define CONFIG_CENTER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CC_MAX_KEY_LEN        128
#define CC_MAX_VALUE_LEN      65536
#define CC_MAX_NAMESPACE_LEN  64
#define CC_MAX_GROUP_LEN      64
#define CC_MAX_INSTANCE_IP    46
#define CC_MAX_VERSIONS       100

typedef enum {
    CC_ENC_NONE  = 0,
    CC_ENC_AES   = 1
} cc_encrypt_type_t;

typedef enum {
    CC_GRAY_ALL   = 0,
    CC_GRAY_IP    = 1
} cc_gray_type_t;

typedef struct {
    char      config_key[CC_MAX_KEY_LEN];
    char     *value;
    size_t    value_len;
    int64_t   version;
    int64_t   created_at;
    int64_t   updated_at;
    char      namespace_[CC_MAX_NAMESPACE_LEN];
    char      group_[CC_MAX_GROUP_LEN];
    cc_encrypt_type_t encrypt_type;
} cc_config_entry_t;

typedef struct {
    int64_t   version;
    char     *value;
    size_t    value_len;
    int64_t   created_at;
    char      operator_[64];
} cc_config_version_t;

typedef struct {
    cc_gray_type_t type;
    char            target_ips[256][CC_MAX_INSTANCE_IP];
    int             ip_count;
    char           *gray_value;
    size_t          gray_value_len;
    int64_t         gray_version;
} cc_gray_release_t;

typedef void (*cc_config_change_callback)(const char *namespace_, const char *key,
                                          const char *new_value, size_t new_len,
                                          void *user_data);

typedef struct cc_config_center cc_config_center_t;

cc_config_center_t *cc_center_create(void);
void                cc_center_destroy(cc_config_center_t *center);

int                 cc_config_put(cc_config_center_t *center, const char *namespace_,
                                  const char *group_, const char *key,
                                  const char *value);
int                 cc_config_get(cc_config_center_t *center, const char *namespace_,
                                  const char *group_, const char *key,
                                  cc_config_entry_t *entry);
int                 cc_config_delete(cc_config_center_t *center, const char *namespace_,
                                     const char *group_, const char *key);

int                 cc_config_put_encrypted(cc_config_center_t *center, const char *namespace_,
                                            const char *group_, const char *key,
                                            const char *value, const char *enc_key);
int                 cc_config_get_decrypted(cc_config_center_t *center, const char *namespace_,
                                            const char *group_, const char *key,
                                            char *out_value, size_t out_size,
                                            const char *enc_key);

int                 cc_config_get_version(cc_config_center_t *center, const char *namespace_,
                                          const char *group_, const char *key,
                                          int64_t version, cc_config_entry_t *entry);
int                 cc_config_list_versions(cc_config_center_t *center, const char *namespace_,
                                            const char *group_, const char *key,
                                            cc_config_version_t *versions, int *count);
int                 cc_config_rollback(cc_config_center_t *center, const char *namespace_,
                                       const char *group_, const char *key, int64_t version);

int                 cc_gray_release_set(cc_config_center_t *center, const char *namespace_,
                                        const char *group_, const char *key,
                                        const cc_gray_release_t *gray);
int                 cc_gray_release_get(cc_config_center_t *center, const char *namespace_,
                                        const char *group_, const char *key,
                                        const char *instance_ip, cc_config_entry_t *entry);

int                 cc_subscribe(cc_config_center_t *center, const char *namespace_,
                                 cc_config_change_callback cb, void *user_data);
int                 cc_unsubscribe(cc_config_center_t *center, const char *namespace_);

int                 cc_poll_update(cc_config_center_t *center, const char *namespace_,
                                  const char *group_);
void                cc_poll_all(cc_config_center_t *center);

int                 cc_namespace_list(cc_config_center_t *center, char namespaces[][CC_MAX_NAMESPACE_LEN], int *count);
int                 cc_group_list(cc_config_center_t *center, const char *namespace_,
                                  char groups[][CC_MAX_GROUP_LEN], int *count);
int                 cc_key_list(cc_config_center_t *center, const char *namespace_,
                                const char *group_, char keys[][CC_MAX_KEY_LEN], int *count);

#ifdef __cplusplus
}
#endif

#endif
