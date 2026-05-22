#include "rbac_engine.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    RbacEngine eng;
    uint64_t required_perms[3];
    int rc;

    rbac_engine_init(&eng);
    rbac_setup_default_roles(&eng);

    printf("=== RBAC Engine Demo ===\n\n");

    rbac_create_user(&eng, "alice");
    rbac_create_user(&eng, "bob");
    rbac_create_user(&eng, "charlie");

    printf("--- Role Assignment ---\n");

    rc = rbac_assign_role(&eng, "alice", "ADMIN");
    printf("[1] Assign ADMIN to alice: %s\n", rc == 0 ? "OK" : "FAIL");

    rc = rbac_assign_role(&eng, "bob", "EDITOR");
    printf("[2] Assign EDITOR to bob: %s\n", rc == 0 ? "OK" : "FAIL");

    rc = rbac_assign_role(&eng, "charlie", "VIEWER");
    printf("[3] Assign VIEWER to charlie: %s\n", rc == 0 ? "OK" : "FAIL");

    printf("\n--- Role Hierarchy Inheritance ---\n");

    printf("[4] Hierarchy: ADMIN -> MODERATOR -> EDITOR -> VIEWER\n");

    printf("\n--- Permission Checks (alice = ADMIN) ---\n");

    rc = rbac_check_permission(&eng, "alice", PERM_READ_USERS);
    printf("[5] alice READ:users:    %s\n", rc ? "ALLOW" : "DENY");

    rc = rbac_check_permission(&eng, "alice", PERM_DELETE_POSTS);
    printf("[6] alice DELETE:posts:  %s\n", rc ? "ALLOW" : "DENY");

    rc = rbac_check_permission(&eng, "alice", PERM_MANAGE_SYSTEM);
    printf("[7] alice MANAGE:system: %s\n", rc ? "ALLOW" : "DENY");

    rc = rbac_check_permission(&eng, "alice", PERM_MANAGE_ROLES);
    printf("[8] alice MANAGE:roles:  %s\n", rc ? "ALLOW" : "DENY");

    printf("\n--- Permission Checks (bob = EDITOR, inherits VIEWER) ---\n");

    rc = rbac_check_permission(&eng, "bob", PERM_WRITE_POSTS);
    printf("[9]  bob WRITE:posts:   %s\n", rc ? "ALLOW" : "DENY");

    rc = rbac_check_permission(&eng, "bob", PERM_DELETE_POSTS);
    printf("[10] bob DELETE:posts:  %s\n", rc ? "ALLOW" : "DENY");

    rc = rbac_check_permission(&eng, "bob", PERM_MANAGE_SYSTEM);
    printf("[11] bob MANAGE:system: %s\n", rc ? "ALLOW" : "DENY");

    rc = rbac_check_permission(&eng, "bob", PERM_READ_USERS);
    printf("[12] bob READ:users:    %s (inherited from VIEWER)\n", rc ? "ALLOW" : "DENY");

    printf("\n--- Permission Checks (charlie = VIEWER) ---\n");

    rc = rbac_check_permission(&eng, "charlie", PERM_READ_POSTS);
    printf("[13] charlie READ:posts:   %s\n", rc ? "ALLOW" : "DENY");

    rc = rbac_check_permission(&eng, "charlie", PERM_WRITE_POSTS);
    printf("[14] charlie WRITE:posts:  %s\n", rc ? "ALLOW" : "DENY");

    rc = rbac_check_permission(&eng, "charlie", PERM_READ_METRICS);
    printf("[15] charlie READ:metrics:  %s\n", rc ? "ALLOW" : "DENY");

    printf("\n--- Multi-Permission Check ---\n");

    required_perms[0] = PERM_READ_USERS;
    required_perms[1] = PERM_READ_POSTS;
    required_perms[2] = PERM_DELETE_POSTS;

    rc = rbac_check_permission_multi(&eng, "alice",
                                      required_perms, 3, 1);
    printf("[16] alice (require ALL 3): %s\n", rc ? "ALLOW" : "DENY");

    rc = rbac_check_permission_multi(&eng, "bob",
                                      required_perms, 3, 1);
    printf("[17] bob   (require ALL 3): %s\n", rc ? "ALLOW" : "DENY");

    rc = rbac_check_permission_multi(&eng, "bob",
                                      required_perms, 3, 0);
    printf("[18] bob   (require ANY):   %s\n", rc ? "ALLOW" : "DENY");

    printf("\n--- Effective Permissions ---\n");

    printf("[19] alice effective: 0x%llx\n",
           (unsigned long long)rbac_get_effective_permissions(&eng, "alice"));
    printf("[20] bob effective:   0x%llx\n",
           (unsigned long long)rbac_get_effective_permissions(&eng, "bob"));
    printf("[21] charlie effective: 0x%llx\n",
           (unsigned long long)rbac_get_effective_permissions(&eng, "charlie"));

    printf("\n--- Role Removal ---\n");

    rc = rbac_assign_role(&eng, "bob", "MODERATOR");
    printf("[22] Add MODERATOR to bob: %s\n", rc == 0 ? "OK" : "FAIL");

    rc = rbac_check_permission(&eng, "bob", PERM_DELETE_POSTS);
    printf("[23] bob DELETE:posts (MODERATOR): %s\n", rc ? "ALLOW" : "DENY");

    rc = rbac_remove_role(&eng, "bob", "MODERATOR");
    printf("[24] Remove MODERATOR from bob: %s\n", rc == 0 ? "OK" : "FAIL");

    rc = rbac_check_permission(&eng, "bob", PERM_DELETE_POSTS);
    printf("[25] bob DELETE:posts (after remove): %s\n", rc ? "ALLOW" : "DENY");

    printf("\n--- User Dumps ---\n");
    rbac_dump_user(&eng, "alice");
    printf("\n");
    rbac_dump_user(&eng, "bob");
    printf("\n");
    rbac_dump_user(&eng, "charlie");

    printf("\n=== All RBAC tests completed ===\n");
    return 0;
}
