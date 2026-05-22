#include "di_container.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char connection_string[128];
} Database;

typedef struct {
    Database *db;
} UserRepository;

typedef struct {
    UserRepository *repo;
} UserService;

typedef struct {
    UserService *svc;
} UserController;

static void *db_factory(void *container, void **deps, int dep_count) {
    (void)container;
    (void)deps;
    (void)dep_count;
    Database *db = (Database *)malloc(sizeof(Database));
    strcpy(db->connection_string, "postgresql://localhost:5432/mydb");
    printf("  [Factory] Database created\n");
    return db;
}

static void *repo_factory(void *container, void **deps, int dep_count) {
    (void)container;
    (void)dep_count;
    UserRepository *repo = (UserRepository *)malloc(sizeof(UserRepository));
    repo->db = (Database *)deps[0];
    printf("  [Factory] UserRepository created (dep: Database)\n");
    return repo;
}

static void *svc_factory(void *container, void **deps, int dep_count) {
    (void)container;
    (void)dep_count;
    UserService *svc = (UserService *)malloc(sizeof(UserService));
    svc->repo = (UserRepository *)deps[0];
    printf("  [Factory] UserService created (dep: UserRepository)\n");
    return svc;
}

static void *ctrl_factory(void *container, void **deps, int dep_count) {
    (void)container;
    (void)dep_count;
    UserController *ctrl = (UserController *)malloc(sizeof(UserController));
    ctrl->svc = (UserService *)deps[0];
    printf("  [Factory] UserController created (dep: UserService)\n");
    return ctrl;
}

int main(void) {
    DIContainer container;
    UserController *ctrl1, *ctrl2;
    UserService *svc1, *svc2;

    printf("=== Dependency Injection Container Demo ===\n\n");

    di_init(&container);
    printf("[1] Container initialized\n\n");

    printf("[2] Registering services:\n");
    {
        const char *db_deps[]  = {NULL};
        di_register(&container, "database", "Database",
                    DI_SCOPE_SINGLETON, db_factory, NULL, 0);
        printf("    Registered 'database' as Singleton\n");
    }
    {
        const char *repo_deps[] = {"database"};
        di_register(&container, "user_repo", "UserRepository",
                    DI_SCOPE_TRANSIENT, repo_factory, repo_deps, 1);
        printf("    Registered 'user_repo' as Transient\n");
    }
    {
        const char *svc_deps[] = {"user_repo"};
        di_register(&container, "user_svc", "UserService",
                    DI_SCOPE_SINGLETON, svc_factory, svc_deps, 1);
        printf("    Registered 'user_svc' as Singleton\n");
    }
    {
        const char *ctrl_deps[] = {"user_svc"};
        di_register(&container, "user_ctrl", "UserController",
                    DI_SCOPE_TRANSIENT, ctrl_factory, ctrl_deps, 1);
        printf("    Registered 'user_ctrl' as Transient\n");
    }
    printf("\n");

    printf("[3] Resolving dependencies recursively:\n");
    ctrl1 = (UserController *)di_resolve(&container, "user_ctrl");
    printf("    Resolved UserController -> %p\n", (void *)ctrl1);
    printf("      -> UserService: %p\n", (void *)ctrl1->svc);
    printf("      -> UserRepository: %p\n", (void *)ctrl1->svc->repo);
    printf("      -> Database: %p (conn: %s)\n",
           (void *)ctrl1->svc->repo->db,
           ctrl1->svc->repo->db->connection_string);
    printf("\n");

    printf("[4] Singleton behavior:\n");
    svc1 = (UserService *)di_resolve(&container, "user_svc");
    svc2 = (UserService *)di_resolve(&container, "user_svc");
    printf("    First  resolve: %p\n", (void *)svc1);
    printf("    Second resolve: %p\n", (void *)svc2);
    printf("    Same instance:  %s\n\n", (svc1 == svc2) ? "YES (Singleton)" : "NO");

    printf("[5] Transient behavior:\n");
    ctrl2 = (UserController *)di_resolve(&container, "user_ctrl");
    printf("    First  resolve: %p\n", (void *)ctrl1);
    printf("    Second resolve: %p\n", (void *)ctrl2);
    printf("    Same instance:  %s\n\n", (ctrl1 == ctrl2) ? "YES" : "NO (Transient)");

    printf("[6] Type-based resolution:\n");
    Database *db_by_type = (Database *)di_resolve_type(&container, "Database");
    printf("    Resolved by type 'Database': %p (conn: %s)\n\n",
           (void *)db_by_type, db_by_type->connection_string);

    printf("[7] Request scope demonstration:\n");
    di_begin_request(&container);
    UserController *req_ctrl = (UserController *)di_resolve(&container, "user_ctrl");
    printf("    Request 1 UserController: %p\n", (void *)req_ctrl);
    di_end_request(&container);

    di_begin_request(&container);
    req_ctrl = (UserController *)di_resolve(&container, "user_ctrl");
    printf("    Request 2 UserController: %p\n", (void *)req_ctrl);
    di_end_request(&container);
    printf("\n");

    printf("[8] Cleanup:\n");
    di_destroy(&container);
    printf("    All singleton instances freed\n\n");

    printf("=== DI Container Demo Complete ===\n");
    return 0;
}
