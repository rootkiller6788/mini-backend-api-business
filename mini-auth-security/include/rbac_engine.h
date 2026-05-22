#ifndef RBAC_ENGINE_H
#define RBAC_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define RBAC_MAX_USERS        512
#define RBAC_MAX_ROLES        64
#define RBAC_MAX_PERMISSIONS  256
#define RBAC_MAX_USERNAME_LEN 128
#define RBAC_MAX_ROLENAME_LEN 64
#define RBAC_MAX_PERMNAME_LEN 128
#define RBAC_MAX_PARENT_ROLES 16
#define RBAC_MAX_PERMS_PER_ROLE 64

typedef enum {
    ROLE_NONE = 0,
    ROLE_ADMIN,
    ROLE_MODERATOR,
    ROLE_EDITOR,
    ROLE_VIEWER,
    ROLE_CUSTOM,
    ROLE_COUNT
} RbacRoleId;

typedef struct {
    char name[RBAC_MAX_PERMNAME_LEN];
    char resource[RBAC_MAX_ROLENAME_LEN];
    char action[RBAC_MAX_ROLENAME_LEN];
    uint32_t mask;
} RbacPermission;

typedef struct {
    char name[RBAC_MAX_ROLENAME_LEN];
    RbacRoleId role_id;
    uint64_t permission_mask;
    int parent_count;
    char parent_roles[RBAC_MAX_PARENT_ROLES][RBAC_MAX_ROLENAME_LEN];
    int priority;
} RbacRole;

typedef struct {
    char username[RBAC_MAX_USERNAME_LEN];
    char role_names[RBAC_MAX_ROLES][RBAC_MAX_ROLENAME_LEN];
    int role_count;
    uint64_t cached_permission_mask;
    int cache_valid;
    time_t created_at;
    time_t updated_at;
} RbacUser;

typedef struct {
    RbacRole roles[RBAC_MAX_ROLES];
    RbacPermission permissions[RBAC_MAX_PERMISSIONS];
    RbacUser users[RBAC_MAX_USERS];
    size_t role_count;
    size_t perm_count;
    size_t user_count;
} RbacEngine;

#define PERM_READ_USERS    (1ULL << 0)
#define PERM_WRITE_USERS   (1ULL << 1)
#define PERM_DELETE_USERS  (1ULL << 2)
#define PERM_READ_POSTS    (1ULL << 3)
#define PERM_WRITE_POSTS   (1ULL << 4)
#define PERM_DELETE_POSTS  (1ULL << 5)
#define PERM_READ_COMMENTS (1ULL << 6)
#define PERM_WRITE_COMMENTS (1ULL << 7)
#define PERM_DELETE_COMMENTS (1ULL << 8)
#define PERM_MANAGE_ROLES  (1ULL << 9)
#define PERM_MANAGE_SYSTEM (1ULL << 10)
#define PERM_READ_METRICS  (1ULL << 11)
#define PERM_MANAGE_USERS  (1ULL << 12)

void rbac_engine_init(RbacEngine *eng);
int  rbac_register_permission(RbacEngine *eng, const char *name,
                              const char *resource, const char *action,
                              uint64_t mask);
int  rbac_register_role(RbacEngine *eng, const char *name, RbacRoleId role_id,
                        int priority);

int  rbac_role_add_parent(RbacEngine *eng, const char *role_name,
                          const char *parent_role_name);
int  rbac_role_grant_permission(RbacEngine *eng, const char *role_name,
                                uint64_t perm_mask);

int  rbac_create_user(RbacEngine *eng, const char *username);
int  rbac_assign_role(RbacEngine *eng, const char *username, const char *role_name);
int  rbac_remove_role(RbacEngine *eng, const char *username, const char *role_name);

int  rbac_check_permission(RbacEngine *eng, const char *username,
                           uint64_t required_perm);
int  rbac_check_permission_multi(RbacEngine *eng, const char *username,
                                 uint64_t *required_perms, size_t count,
                                 int require_all);

uint64_t rbac_get_effective_permissions(RbacEngine *eng, const char *username);
int  rbac_get_user_roles(const RbacEngine *eng, const char *username,
                         char roles_out[][RBAC_MAX_ROLENAME_LEN], size_t *count);

void rbac_setup_default_roles(RbacEngine *eng);
void rbac_dump_user(const RbacEngine *eng, const char *username);

const char *rbac_role_id_name(RbacRoleId id);
RbacRoleId rbac_role_name_to_id(const char *name);

#endif
