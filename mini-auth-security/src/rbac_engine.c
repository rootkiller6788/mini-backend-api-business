#include "rbac_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void rbac_engine_init(RbacEngine *eng) {
    memset(eng, 0, sizeof(*eng));
}

const char *rbac_role_id_name(RbacRoleId id) {
    switch (id) {
    case ROLE_NONE:      return "NONE";
    case ROLE_ADMIN:     return "ADMIN";
    case ROLE_MODERATOR: return "MODERATOR";
    case ROLE_EDITOR:    return "EDITOR";
    case ROLE_VIEWER:    return "VIEWER";
    case ROLE_CUSTOM:    return "CUSTOM";
    default:             return "UNKNOWN";
    }
}

RbacRoleId rbac_role_name_to_id(const char *name) {
    if (!name) return ROLE_NONE;
    if (strcmp(name, "ADMIN") == 0)     return ROLE_ADMIN;
    if (strcmp(name, "MODERATOR") == 0) return ROLE_MODERATOR;
    if (strcmp(name, "EDITOR") == 0)    return ROLE_EDITOR;
    if (strcmp(name, "VIEWER") == 0)    return ROLE_VIEWER;
    if (strcmp(name, "CUSTOM") == 0)    return ROLE_CUSTOM;
    return ROLE_NONE;
}

int rbac_register_permission(RbacEngine *eng, const char *name,
                              const char *resource, const char *action,
                              uint64_t mask) {
    size_t i;
    if (eng->perm_count >= RBAC_MAX_PERMISSIONS) return -1;
    for (i = 0; i < eng->perm_count; i++) {
        if (strcmp(eng->permissions[i].name, name) == 0) return -2;
    }
    i = eng->perm_count;
    strncpy(eng->permissions[i].name, name, RBAC_MAX_PERMNAME_LEN - 1);
    strncpy(eng->permissions[i].resource, resource, RBAC_MAX_ROLENAME_LEN - 1);
    strncpy(eng->permissions[i].action, action, RBAC_MAX_ROLENAME_LEN - 1);
    eng->permissions[i].mask = mask;
    eng->perm_count++;
    return 0;
}

int rbac_register_role(RbacEngine *eng, const char *name, RbacRoleId role_id,
                        int priority) {
    size_t i;
    if (eng->role_count >= RBAC_MAX_ROLES) return -1;
    for (i = 0; i < eng->role_count; i++) {
        if (strcmp(eng->roles[i].name, name) == 0) return -2;
    }
    i = eng->role_count;
    strncpy(eng->roles[i].name, name, RBAC_MAX_ROLENAME_LEN - 1);
    eng->roles[i].role_id = role_id;
    eng->roles[i].priority = priority;
    eng->roles[i].permission_mask = 0;
    eng->roles[i].parent_count = 0;
    eng->role_count++;
    return 0;
}

int rbac_role_add_parent(RbacEngine *eng, const char *role_name,
                          const char *parent_role_name) {
    size_t i;
    int found_role = -1, found_parent = -1;
    for (i = 0; i < eng->role_count; i++) {
        if (strcmp(eng->roles[i].name, role_name) == 0) found_role = (int)i;
        if (strcmp(eng->roles[i].name, parent_role_name) == 0) found_parent = (int)i;
    }
    if (found_role < 0 || found_parent < 0) return -1;
    if (eng->roles[found_role].parent_count >= RBAC_MAX_PARENT_ROLES) return -2;

    strncpy(eng->roles[found_role].parent_roles[eng->roles[found_role].parent_count],
            parent_role_name, RBAC_MAX_ROLENAME_LEN - 1);
    eng->roles[found_role].parent_count++;
    return 0;
}

int rbac_role_grant_permission(RbacEngine *eng, const char *role_name,
                                uint64_t perm_mask) {
    size_t i;
    for (i = 0; i < eng->role_count; i++) {
        if (strcmp(eng->roles[i].name, role_name) == 0) {
            eng->roles[i].permission_mask |= perm_mask;
            return 0;
        }
    }
    return -1;
}

int rbac_create_user(RbacEngine *eng, const char *username) {
    size_t i;
    if (eng->user_count >= RBAC_MAX_USERS) return -1;
    for (i = 0; i < eng->user_count; i++) {
        if (strcmp(eng->users[i].username, username) == 0) return -2;
    }
    i = eng->user_count;
    strncpy(eng->users[i].username, username, RBAC_MAX_USERNAME_LEN - 1);
    eng->users[i].role_count = 0;
    eng->users[i].cached_permission_mask = 0;
    eng->users[i].cache_valid = 0;
    eng->users[i].created_at = time(NULL);
    eng->users[i].updated_at = time(NULL);
    eng->user_count++;
    return 0;
}

static RbacUser *rbac_find_user(RbacEngine *eng, const char *username) {
    size_t i;
    for (i = 0; i < eng->user_count; i++) {
        if (strcmp(eng->users[i].username, username) == 0) return &eng->users[i];
    }
    return NULL;
}

static RbacRole *rbac_find_role(const RbacEngine *eng, const char *role_name) {
    size_t i;
    for (i = 0; i < eng->role_count; i++) {
        if (strcmp(eng->roles[i].name, role_name) == 0) return &eng->roles[i];
    }
    return NULL;
}

int rbac_assign_role(RbacEngine *eng, const char *username, const char *role_name) {
    RbacUser *user = rbac_find_user(eng, username);
    RbacRole *role;
    if (!user) return -1;
    role = rbac_find_role(eng, role_name);
    if (!role) return -2;
    if (user->role_count >= RBAC_MAX_ROLES) return -3;

    strncpy(user->role_names[user->role_count], role_name, RBAC_MAX_ROLENAME_LEN - 1);
    user->role_count++;
    user->cache_valid = 0;
    user->updated_at = time(NULL);
    return 0;
}

int rbac_remove_role(RbacEngine *eng, const char *username, const char *role_name) {
    RbacUser *user = rbac_find_user(eng, username);
    size_t i;
    int found = -1;
    if (!user) return -1;

    for (i = 0; i < (size_t)user->role_count; i++) {
        if (strcmp(user->role_names[i], role_name) == 0) {
            found = (int)i;
            break;
        }
    }
    if (found < 0) return -2;

    for (i = (size_t)found; i + 1 < (size_t)user->role_count; i++) {
        strncpy(user->role_names[i], user->role_names[i + 1], RBAC_MAX_ROLENAME_LEN - 1);
    }
    user->role_count--;
    user->cache_valid = 0;
    user->updated_at = time(NULL);
    return 0;
}

static uint64_t rbac_resolve_perm_mask(const RbacEngine *eng, const char *role_name,
                                        int depth) {
    uint64_t mask = 0;
    size_t i;
    const RbacRole *role = rbac_find_role(eng, role_name);
    if (!role || depth > 10) return 0;

    mask = role->permission_mask;

    for (i = 0; i < (size_t)role->parent_count; i++) {
        mask |= rbac_resolve_perm_mask(eng, role->parent_roles[i], depth + 1);
    }
    return mask;
}

uint64_t rbac_get_effective_permissions(RbacEngine *eng, const char *username) {
    RbacUser *user = rbac_find_user(eng, username);
    uint64_t mask = 0;
    int i;

    if (!user) return 0;
    if (user->cache_valid) return user->cached_permission_mask;

    for (i = 0; i < user->role_count; i++) {
        mask |= rbac_resolve_perm_mask(eng, user->role_names[i], 0);
    }

    user->cached_permission_mask = mask;
    user->cache_valid = 1;
    return mask;
}

int rbac_check_permission(RbacEngine *eng, const char *username,
                           uint64_t required_perm) {
    uint64_t effective = rbac_get_effective_permissions(eng, username);
    if (required_perm == 0) return 1;
    return ((effective & required_perm) == required_perm) ? 1 : 0;
}

int rbac_check_permission_multi(RbacEngine *eng, const char *username,
                                 uint64_t *required_perms, size_t count,
                                 int require_all) {
    uint64_t effective = rbac_get_effective_permissions(eng, username);
    size_t i;

    if (count == 0) return 1;

    if (require_all) {
        for (i = 0; i < count; i++) {
            if ((effective & required_perms[i]) != required_perms[i]) return 0;
        }
        return 1;
    } else {
        for (i = 0; i < count; i++) {
            if ((effective & required_perms[i]) == required_perms[i]) return 1;
        }
        return 0;
    }
}

int rbac_get_user_roles(const RbacEngine *eng, const char *username,
                         char roles_out[][RBAC_MAX_ROLENAME_LEN], size_t *count) {
    size_t i;
    const RbacUser *user = rbac_find_user((RbacEngine *)eng, username);
    if (!user) return -1;

    *count = (size_t)user->role_count;
    for (i = 0; i < (size_t)user->role_count && i < *count; i++) {
        strncpy(roles_out[i], user->role_names[i], RBAC_MAX_ROLENAME_LEN - 1);
    }
    return 0;
}

void rbac_setup_default_roles(RbacEngine *eng) {
    rbac_register_permission(eng, "READ:users",    "users",    "READ",   PERM_READ_USERS);
    rbac_register_permission(eng, "WRITE:users",   "users",    "WRITE",  PERM_WRITE_USERS);
    rbac_register_permission(eng, "DELETE:users",  "users",    "DELETE", PERM_DELETE_USERS);
    rbac_register_permission(eng, "READ:posts",    "posts",    "READ",   PERM_READ_POSTS);
    rbac_register_permission(eng, "WRITE:posts",   "posts",    "WRITE",  PERM_WRITE_POSTS);
    rbac_register_permission(eng, "DELETE:posts",  "posts",    "DELETE", PERM_DELETE_POSTS);
    rbac_register_permission(eng, "READ:comments", "comments", "READ",   PERM_READ_COMMENTS);
    rbac_register_permission(eng, "WRITE:comments","comments", "WRITE",  PERM_WRITE_COMMENTS);
    rbac_register_permission(eng, "DELETE:comments","comments","DELETE", PERM_DELETE_COMMENTS);
    rbac_register_permission(eng, "MANAGE:roles",  "roles",    "MANAGE", PERM_MANAGE_ROLES);
    rbac_register_permission(eng, "MANAGE:system", "system",   "MANAGE", PERM_MANAGE_SYSTEM);
    rbac_register_permission(eng, "READ:metrics",  "metrics",  "READ",   PERM_READ_METRICS);
    rbac_register_permission(eng, "MANAGE:users",  "users",    "MANAGE", PERM_MANAGE_USERS);

    rbac_register_role(eng, "VIEWER", ROLE_VIEWER, 1);
    rbac_register_role(eng, "EDITOR", ROLE_EDITOR, 2);
    rbac_register_role(eng, "MODERATOR", ROLE_MODERATOR, 3);
    rbac_register_role(eng, "ADMIN", ROLE_ADMIN, 10);

    rbac_role_grant_permission(eng, "VIEWER",
        PERM_READ_USERS | PERM_READ_POSTS | PERM_READ_COMMENTS | PERM_READ_METRICS);
    rbac_role_grant_permission(eng, "EDITOR",
        PERM_READ_USERS | PERM_READ_POSTS | PERM_WRITE_POSTS |
        PERM_READ_COMMENTS | PERM_WRITE_COMMENTS | PERM_READ_METRICS);
    rbac_role_grant_permission(eng, "MODERATOR",
        PERM_READ_USERS | PERM_READ_POSTS | PERM_WRITE_POSTS | PERM_DELETE_POSTS |
        PERM_READ_COMMENTS | PERM_WRITE_COMMENTS | PERM_DELETE_COMMENTS |
        PERM_READ_METRICS);
    rbac_role_grant_permission(eng, "ADMIN",
        PERM_READ_USERS | PERM_WRITE_USERS | PERM_DELETE_USERS |
        PERM_READ_POSTS | PERM_WRITE_POSTS | PERM_DELETE_POSTS |
        PERM_READ_COMMENTS | PERM_WRITE_COMMENTS | PERM_DELETE_COMMENTS |
        PERM_MANAGE_ROLES | PERM_MANAGE_SYSTEM | PERM_READ_METRICS |
        PERM_MANAGE_USERS);

    rbac_role_add_parent(eng, "EDITOR", "VIEWER");
    rbac_role_add_parent(eng, "MODERATOR", "EDITOR");
    rbac_role_add_parent(eng, "ADMIN", "MODERATOR");
}

void rbac_dump_user(const RbacEngine *eng, const char *username) {
    size_t i;
    char roles_buf[RBAC_MAX_ROLES][RBAC_MAX_ROLENAME_LEN];
    size_t role_count;
    uint64_t perms;

    printf("=== RBAC User: %s ===\n", username);
    if (rbac_get_user_roles(eng, username, roles_buf, &role_count) == 0) {
        printf("Roles (%zu):\n", role_count);
        for (i = 0; i < role_count; i++) {
            printf("  - %s\n", roles_buf[i]);
        }
    }
    perms = rbac_get_effective_permissions((RbacEngine *)eng, username);
    printf("Effective permissions mask: 0x%llx\n", (unsigned long long)perms);
    printf("  READ:users    = %s\n", (perms & PERM_READ_USERS)    ? "YES" : "NO");
    printf("  WRITE:users   = %s\n", (perms & PERM_WRITE_USERS)   ? "YES" : "NO");
    printf("  DELETE:users  = %s\n", (perms & PERM_DELETE_USERS)  ? "YES" : "NO");
    printf("  READ:posts    = %s\n", (perms & PERM_READ_POSTS)    ? "YES" : "NO");
    printf("  WRITE:posts   = %s\n", (perms & PERM_WRITE_POSTS)   ? "YES" : "NO");
    printf("  DELETE:posts  = %s\n", (perms & PERM_DELETE_POSTS)  ? "YES" : "NO");
    printf("  MANAGE:roles  = %s\n", (perms & PERM_MANAGE_ROLES)  ? "YES" : "NO");
}
