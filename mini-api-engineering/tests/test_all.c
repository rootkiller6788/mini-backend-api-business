#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include "../include/rest_design.h"
#include "../include/graphql_engine.h"
#include "../include/grpc_sim.h"
#include "../include/openapi_builder.h"
#include "../include/api_version.h"

static int tests_pass = 0;
static int tests_fail = 0;

#define TEST(name) do { printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_pass++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_fail++; } while(0)
#define CHECK(cond, msg) do { if (cond) PASS(); else FAIL(msg); } while(0)

static void test_rest_methods(void) {
    printf("\n--- REST Design Tests ---\n");
    TEST("method_string");
    CHECK(strcmp(rest_method_string(REST_GET), "GET") == 0, "GET mismatch");
    TEST("status_string");
    CHECK(strcmp(rest_status_string(REST_200_OK), "200 OK") == 0, "200 mismatch");
    TEST("status_reason");
    CHECK(strcmp(rest_status_reason(REST_404_NOT_FOUND), "Not Found") == 0, "404 mismatch");
    TEST("resource_init");
    rest_resource_t r;
    rest_resource_init(&r, "test", "/api/test", REST_GET);
    CHECK(strcmp(r.name, "test") == 0 && r.method == REST_GET, "init failed");
    TEST("resource_set_body");
    rest_resource_set_body(&r, "{\"ok\":true}");
    CHECK(r.response_len > 0 && strstr(r.response_body, "ok"), "body not set");
    TEST("resource_add_link");
    rest_resource_add_link(&r, "self", "/api/test", "GET");
    CHECK(r.link_count == 1, "link not added");
    TEST("resource_add_header");
    rest_resource_add_header(&r, "X-Custom", "value1");
    CHECK(r.header_count == 1, "header not added");
    TEST("resource_add_path_param");
    rest_resource_add_path_param(&r, "id", "42");
    CHECK(r.param_count == 1, "param not added");
    TEST("pagination_init");
    rest_pagination_t p;
    rest_pagination_init(&p, 2, 10, 42);
    CHECK(p.page == 2 && p.total_pages == 5 && p.has_next && p.has_prev, "pagination wrong");
    TEST("is_safe_method");
    CHECK(rest_is_safe_method(REST_GET) && !rest_is_safe_method(REST_POST), "safe method wrong");
    TEST("is_idempotent");
    CHECK(rest_is_idempotent_method(REST_PUT) && !rest_is_idempotent_method(REST_POST), "idempotent wrong");
    TEST("is_status_success");
    CHECK(rest_is_status_success(REST_200_OK) && !rest_is_status_success(REST_404_NOT_FOUND), "success wrong");
    TEST("null_resource_init"); rest_resource_init(NULL, "x", "y", REST_GET); PASS();
    TEST("null_set_body"); rest_resource_set_body(NULL, "x"); PASS();
}

static void test_rest_router(void) {
    printf("\n--- REST Router Tests ---\n");
    TEST("router_init");
    rest_router_t router;
    rest_router_init(&router, "/api", 1);
    CHECK(strcmp(router.base_path, "/api") == 0, "router init failed");
    TEST("router_register");
    rest_resource_t r1, r2;
    rest_resource_init(&r1, "users", "/api/v1/users", REST_GET);
    rest_resource_init(&r2, "user", "/api/v1/users/{id}", REST_GET);
    CHECK(rest_router_register(&router, &r1), "register r1 failed");
    CHECK(rest_router_register(&router, &r2), "register r2 failed");
    CHECK(router.route_count == 2, "route count wrong");
    TEST("router_resolve_exact");
    rest_resource_t* found = rest_router_resolve(&router, "/api/v1/users", REST_GET);
    CHECK(found != NULL && strcmp(found->name, "users") == 0, "exact resolve failed");
    TEST("router_resolve_param");
    found = rest_router_resolve(&router, "/api/v1/users/42", REST_GET);
    CHECK(found != NULL && strcmp(found->name, "user") == 0, "param resolve failed");
    TEST("router_resolve_wrong_method");
    found = rest_router_resolve(&router, "/api/v1/users", REST_POST);
    CHECK(found == NULL, "should not match POST");
    TEST("router_register_null");
    CHECK(!rest_router_register(NULL, &r1), "null router");
}

static void test_rest_etag(void) {
    printf("\n--- REST ETag Tests ---\n");
    TEST("etag_strong");
    rest_etag_t etag = rest_etag_strong("hello", 5);
    CHECK(etag.value[0] == '"' && !etag.is_weak, "strong etag format");
    TEST("etag_weak");
    rest_etag_t wetag = rest_etag_weak("hello", 5);
    CHECK(wetag.is_weak && strncmp(wetag.value, "W/", 2) == 0, "weak etag");
    TEST("etag_match_self");
    CHECK(rest_etag_match(etag, etag.value), "should match itself");
    TEST("etag_match_wildcard");
    CHECK(rest_etag_match(etag, "*"), "should match wildcard");
    TEST("etag_compare");
    rest_etag_t etag2 = rest_etag_strong("hello", 5);
    CHECK(rest_etag_compare(etag, etag2) == 0, "same body same etag");
    TEST("etag_empty_body");
    rest_etag_t empty_etag = rest_etag_strong(NULL, 0);
    CHECK(empty_etag.value[0] == '\0', "empty body empty etag");
}

static void test_rest_cors_rate(void) {
    printf("\n--- REST CORS & Rate Limit Tests ---\n");
    TEST("cors_policy");
    rest_router_t router;
    rest_router_init(&router, "/api", 1);
    rest_router_set_cors(&router, "https://example.com", true, "GET,POST", "X-API-Key", 3600);
    CHECK(strcmp(router.cors.origin, "https://example.com") == 0, "origin not set");
    TEST("build_cors_headers");
    char buf[1024];
    rest_build_cors_headers(&router.cors, NULL, buf, sizeof(buf));
    CHECK(strstr(buf, "Access-Control-Allow-Origin") != NULL, "CORS headers missing");
    TEST("build_cache_headers");
    rest_etag_t etag = rest_etag_strong("data", 4);
    rest_build_cache_headers(etag, 300, REST_CACHE_PUBLIC, buf, sizeof(buf));
    CHECK(strstr(buf, "Cache-Control: public") != NULL, "cache control missing");
    TEST("rate_limit_allow");
    rest_router_set_rate_limit(&router, 1.0, 3.0);
    CHECK(rest_router_check_rate_limit(&router), "first request");
    CHECK(rest_router_check_rate_limit(&router), "second request");
    CHECK(rest_router_check_rate_limit(&router), "third request");
    TEST("rate_limit_exceed");
    CHECK(!rest_router_check_rate_limit(&router), "should be rate limited");
}

static void test_rest_negotiation(void) {
    printf("\n--- REST Content Negotiation Tests ---\n");
    TEST("media_type_to_mime");
    CHECK(strcmp(rest_media_type_to_mime(REST_MEDIA_JSON), "application/json") == 0, "json mime");
    TEST("negotiate_json_preferred");
    rest_media_type_t supported[] = {REST_MEDIA_JSON, REST_MEDIA_XML, REST_MEDIA_TEXT_PLAIN};
    rest_media_type_t r = rest_negotiate_content_type("application/json, application/xml;q=0.9", supported, 3);
    CHECK(r == REST_MEDIA_JSON, "should pick JSON");
    TEST("negotiate_star_slash_star");
    r = rest_negotiate_content_type("*/*", supported, 3);
    CHECK(r == REST_MEDIA_JSON, "*/* picks first");
    TEST("parse_media_range");
    rest_media_range_t mr = rest_parse_media_range("text/html;q=0.5;level=3");
    CHECK(mr.quality == 0.5 && mr.level == 3, "q/level parse");
    TEST("negotiate_null");
    rest_media_type_t s2[] = {REST_MEDIA_JSON};
    r = rest_negotiate_content_type(NULL, s2, 1);
    CHECK(r == REST_MEDIA_JSON, "null header fallback");
}


static void test_graphql_core(void) {
    printf("\n--- GraphQL Core Tests ---\n");
    TEST("scalar_name");
    CHECK(strcmp(gql_scalar_name(GQL_TYPE_STRING), "String") == 0, "scalar name");
    TEST("operation_name");
    CHECK(strcmp(gql_operation_name(GQL_OP_QUERY), "query") == 0, "op name");
    TEST("engine_init");
    gql_engine_t e;
    gql_engine_init(&e);
    CHECK(e.schema.type_count == 0, "empty at init");
    TEST("add_type");
    gql_schema_add_type(&e.schema, "User");
    CHECK(e.schema.type_count == 1, "type not added");
    TEST("add_field");
    gql_schema_add_field(&e.schema, "User", "name", GQL_TYPE_STRING, "String", false, true);
    const gql_type_t* t = gql_find_type(&e, "User");
    CHECK(t != NULL && t->field_count == 1, "field not added");
    TEST("add_enum");
    gql_schema_add_type(&e.schema, "Role");
    gql_schema_add_enum_value(&e.schema, "Role", "ADMIN");
    const gql_type_t* rt = gql_find_type(&e, "Role");
    CHECK(rt != NULL && rt->is_enum && rt->enum_count == 1, "enum not added");
    TEST("find_type_nulls");
    CHECK(gql_find_type(NULL, "X") == NULL, "null engine");
    CHECK(gql_find_type(&e, "NonExistent") == NULL, "nonexistent");
}

static void test_graphql_parse(void) {
    printf("\n--- GraphQL Parse Tests ---\n");
    TEST("parse_simple");
    gql_engine_t e;
    gql_engine_init(&e);
    CHECK(gql_parse_query(&e, "{ hello }"), "simple query");
    CHECK(e.parsed.selection_count == 1, "1 selection");
    TEST("parse_nested");
    gql_engine_t e2;
    gql_engine_init(&e2);
    CHECK(gql_parse_query(&e2, "{ user { name email } }"), "nested query");
    TEST("parse_args");
    gql_engine_t e3;
    gql_engine_init(&e3);
    CHECK(gql_parse_query(&e3, "{ user(id: 42) { name } }"), "args query");
    TEST("parse_mutation");
    gql_engine_t e4;
    gql_engine_init(&e4);
    CHECK(gql_parse_query(&e4, "mutation { createUser(name: \"Eve\") { id } }"), "mutation");
    CHECK(e4.parsed.operation == GQL_OP_MUTATION, "should be mutation");
    TEST("parse_invalid");
    CHECK(!gql_parse_query(NULL, "{ x }"), "null engine");
    CHECK(!gql_parse_query(&e, NULL), "null query");
    TEST("sdl_parse");
    gql_engine_t e5;
    gql_engine_init(&e5);
    CHECK(gql_parse_schema_sdl(&e5, "type Query { hello: String }"), "SDL parse");
    CHECK(e5.schema.type_count >= 1, "SDL created type");
    TEST("sdl_enum");
    gql_engine_t e6;
    gql_engine_init(&e6);
    CHECK(gql_parse_schema_sdl(&e6, "enum Color { RED GREEN BLUE }"), "SDL enum");
}

static void test_graphql_complexity(void) {
    printf("\n--- GraphQL Complexity Tests ---\n");
    TEST("calculate_cost");
    gql_engine_t e;
    gql_engine_init(&e);
    gql_parse_query(&e, "{ hello world }");
    int32_t cost = gql_calculate_query_cost(&e);
    CHECK(cost == 3, "simple query cost = 3");
    TEST("calculate_depth");
    int32_t depth = gql_calculate_query_depth(&e);
    CHECK(depth == 1, "simple depth = 1");
    TEST("calculate_depth_nested");
    gql_engine_t e2;
    gql_engine_init(&e2);
    gql_parse_query(&e2, "{ user { name email posts { title } } }");
    CHECK(gql_calculate_query_depth(&e2) >= 2, "nested depth >= 2");
    TEST("limit_pass");
    CHECK(gql_limit_query_complexity(&e, 100, 10), "within limits");
    TEST("limit_fail_cost");
    CHECK(!gql_limit_query_complexity(&e, 1, 10), "cost limit breached");
}

static void test_graphql_validation(void) {
    printf("\n--- GraphQL Validation Tests ---\n");
    TEST("validate_query");
    gql_engine_t e;
    gql_engine_init(&e);
    gql_schema_add_type(&e.schema, "Query");
    gql_schema_add_field(&e.schema, "Query", "hello", GQL_TYPE_STRING, "String", false, false);
    gql_schema_set_query_type(&e.schema, "Query");
    gql_parse_query(&e, "{ hello }");
    CHECK(gql_validate_query(&e), "valid query");
    TEST("validate_schema_empty");
    gql_engine_t e2;
    gql_engine_init(&e2);
    CHECK(!gql_validate_schema(&e2), "empty schema fails");
    TEST("validate_schema_ok");
    CHECK(gql_validate_schema(&e), "schema with types passes");
    TEST("has_field");
    CHECK(gql_has_field(&e, "Query", "hello"), "has hello");
    CHECK(!gql_has_field(&e, "Query", "nonexistent"), "no nonexistent");
    TEST("type_field_count");
    CHECK(gql_type_field_count(&e, "Query") == 1, "Query has 1 field");
    CHECK(gql_type_field_count(NULL, "X") == 0, "null = 0");
    TEST("sdl_from_schema");
    char buf[4096];
    char* result = gql_sdl_from_schema(&e, buf, sizeof(buf));
    CHECK(result != NULL && strstr(buf, "type Query") != NULL, "SDL export");
    CHECK(strstr(buf, "hello: String") != NULL, "hello field in SDL");
}


static void test_grpc_core(void) {
    printf("\n--- gRPC Core Tests ---\n");
    TEST("rpc_type_string");
    CHECK(strcmp(grpc_rpc_type_string(GRPC_UNARY), "UNARY") == 0, "unary string");
    TEST("status_string");
    CHECK(strcmp(grpc_status_string(GRPC_OK), "OK") == 0, "status string");
    TEST("proto_type_string");
    CHECK(strcmp(grpc_proto_type_string(GRPC_PROTO_STRING), "string") == 0, "proto type");
    TEST("sim_init");
    grpc_sim_t sim;
    grpc_sim_init(&sim);
    CHECK(sim.proto.service_count == 0 && sim.next_stream_id == 1, "init");
    TEST("add_service");
    grpc_service_def_t* svc = grpc_sim_add_service(&sim, "Greeter", "greet");
    CHECK(svc != NULL, "service added");
    TEST("add_method");
    grpc_method_def_t* m = grpc_sim_add_method(svc, "SayHello", GRPC_UNARY, "HelloReq", "HelloResp");
    CHECK(m != NULL && m->rpc_type == GRPC_UNARY, "method added");
    TEST("add_message_field");
    grpc_message_def_t* msg = grpc_sim_add_message(&sim, "HelloReq");
    grpc_message_add_field(msg, "name", GRPC_PROTO_STRING, 1, false, false);
    CHECK(msg->field_count == 1, "field added");
    TEST("export_proto");
    char proto_buf[4096];
    CHECK(grpc_sim_export_proto(&sim, proto_buf, sizeof(proto_buf)) != NULL, "export");
    CHECK(strstr(proto_buf, "service Greeter") != NULL, "service in proto");
    CHECK(strstr(proto_buf, "rpc SayHello") != NULL, "rpc in proto");
    TEST("export_null");
    CHECK(grpc_sim_export_proto(NULL, proto_buf, 100) == NULL, "null export");
}

static void test_grpc_streams(void) {
    printf("\n--- gRPC Stream Tests ---\n");
    TEST("open_stream");
    grpc_sim_t sim;
    grpc_sim_init(&sim);
    uint32_t sid = grpc_sim_open_stream(&sim, GRPC_UNARY);
    CHECK(sid == 1, "first stream id = 1");
    CHECK(sim.stream_count == 1, "stream count 1");
    TEST("write_stream");
    uint8_t data[] = "Hello gRPC";
    CHECK(grpc_sim_write_stream(&sim, sid, data, 11), "write");
    TEST("read_stream");
    uint8_t rbuf[256];
    int32_t rlen = (int32_t)sizeof(rbuf);
    bool done = true;
    CHECK(grpc_sim_read_stream(&sim, sid, rbuf, &rlen, &done), "read");
    CHECK(rlen == 11, "read length");
    TEST("close_stream");
    CHECK(grpc_sim_close_stream(&sim, sid, GRPC_OK), "close");
    CHECK(sim.stream_count == 0, "removed");
    TEST("open_stream_null");
    CHECK(grpc_sim_open_stream(NULL, GRPC_UNARY) == 0, "null sim");
}

static void test_grpc_varint_zigzag(void) {
    printf("\n--- gRPC Varint/ZigZag Tests ---\n");
    TEST("varint_encode_1");
    uint8_t buf[10];
    uint32_t len = grpc_varint_encode(1, buf);
    CHECK(len == 1 && buf[0] == 1, "varint 1");
    TEST("varint_encode_300");
    len = grpc_varint_encode(300, buf);
    CHECK(len == 2, "varint 300 = 2 bytes");
    TEST("varint_decode");
    uint64_t val;
    int32_t dlen = grpc_varint_decode(buf, 10, &val);
    CHECK(dlen == 2 && val == 300, "varint roundtrip");
    TEST("zigzag32_neg1");
    uint32_t z = grpc_zigzag32_encode(-1);
    CHECK(z == 1 && grpc_zigzag32_decode(z) == -1, "zigzag -1");
    TEST("zigzag32_1");
    z = grpc_zigzag32_encode(1);
    CHECK(z == 2 && grpc_zigzag32_decode(z) == 1, "zigzag 1");
    TEST("zigzag64_pos");
    int64_t z64v = 1000000;
    uint64_t z64e = grpc_zigzag64_encode(z64v);
    CHECK(grpc_zigzag64_decode(z64e) == z64v, "zigzag64 roundtrip");
}

static void test_grpc_wire_format(void) {
    printf("\n--- gRPC Wire Format Tests ---\n");
    TEST("wire_encode_varint");
    uint8_t buf[64];
    uint64_t val = 150;
    int32_t written = grpc_wire_encode_field(buf, 1, GRPC_PROTO_UINT64, &val, sizeof(val));
    CHECK(written > 0, "wire encode");
    TEST("wire_decode_varint");
    uint64_t dval;
    int32_t dvlen = (int32_t)sizeof(dval);
    int32_t dfn = 0;
    grpc_proto_type_t dpt;
    int32_t consumed = grpc_wire_decode_field(buf, written, &dfn, &dpt, &dval, &dvlen);
    CHECK(consumed > 0 && dfn == 1 && dval == 150, "wire decode roundtrip");
    TEST("wire_encode_string");
    const char* str = "test";
    written = grpc_wire_encode_field(buf, 2, GRPC_PROTO_STRING, str, 4);
    CHECK(written > 4, "string encode");
    TEST("wire_decode_string");
    char strbuf[32];
    dvlen = (int32_t)sizeof(strbuf);
    consumed = grpc_wire_decode_field(buf, written, &dfn, &dpt, strbuf, &dvlen);
    CHECK(consumed > 0 && dfn == 2 && dvlen == 4, "string decode");
    CHECK(memcmp(strbuf, "test", 4) == 0, "string content");
}

static void test_grpc_h2_frames(void) {
    printf("\n--- gRPC HTTP/2 Frame Tests ---\n");
    TEST("h2_build");
    grpc_h2_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.type = GRPC_FRAME_HEADERS;
    frame.flags = 0x04;
    frame.stream_id = 1;
    uint8_t out[32];
    int32_t out_len;
    CHECK(grpc_sim_build_h2_frame(&frame, out, &out_len, true), "build");
    CHECK(out_len == 9, "empty = 9 bytes");
    TEST("h2_parse");
    grpc_h2_frame_t parsed;
    CHECK(grpc_sim_parse_grpc_frame(&parsed, out, out_len), "parse");
    CHECK(parsed.type == GRPC_FRAME_HEADERS && parsed.stream_id == 1, "parsed ok");
    TEST("h2_parse_short");
    uint8_t short_data[5] = {0};
    CHECK(!grpc_sim_parse_grpc_frame(&parsed, short_data, 5), "too short fails");
    TEST("grpc_msg_frame");
    grpc_stream_t stream;
    memset(&stream, 0, sizeof(stream));
    uint8_t msg_frame[32] = {0,0,0,0,11,'H','e','l','l','o',' ','W','o','r','l','d'};
    CHECK(grpc_sim_grpc_frame(&stream, msg_frame, 16), "msg frame");
    CHECK(stream.message_len == 11, "msg length");
    CHECK(!stream.is_compressed, "not compressed");
}

static void test_grpc_health(void) {
    printf("\n--- gRPC Health Check Tests ---\n");
    grpc_sim_t sim;
    grpc_sim_init(&sim);
    grpc_sim_add_service(&sim, "Health", "grpc.health");
    grpc_status_code_t status;
    TEST("health_check_found");
    CHECK(grpc_sim_health_check(&sim, "Health", &status), "health check");
    CHECK(status == GRPC_OK, "should be OK");
    TEST("health_check_unknown");
    CHECK(grpc_sim_health_check(&sim, "Unknown", &status), "unknown check");
    CHECK(status == GRPC_NOT_FOUND, "should be NOT_FOUND");
    TEST("health_check_null_svc");
    CHECK(grpc_sim_health_check(&sim, NULL, &status), "null svc");
    CHECK(status == GRPC_OK, "null = OK");
}


static void test_openapi_core(void) {
    printf("\n--- OpenAPI Core Tests ---\n");
    TEST("type_string");
    CHECK(strcmp(oa_type_string(OA_STRING), "string") == 0, "type string");
    TEST("method_string");
    CHECK(strcmp(oa_method_string(OA_GET), "get") == 0, "method string");
    TEST("spec_init");
    oa_spec_t spec;
    oa_spec_init(&spec, "Test API", "1.0.0", "A test");
    CHECK(strcmp(spec.title, "Test API") == 0, "title");
    CHECK(strcmp(spec.version, "1.0.0") == 0, "version");
    TEST("add_server");
    oa_spec_add_server(&spec, "http://localhost:8080", "Local");
    CHECK(spec.server_count == 1, "server added");
    TEST("add_schema_prop");
    oa_schema_t* s = oa_spec_add_schema(&spec, "User", OA_OBJECT, "User object");
    oa_property_t* p = oa_schema_add_prop(s, "name", OA_STRING, NULL, true, "User name");
    CHECK(s != NULL && p != NULL && p->is_required, "schema+prop");
    TEST("add_operation");
    oa_operation_t* op = oa_spec_add_operation(&spec, "/users", OA_GET, "listUsers", "List users");
    CHECK(op != NULL, "op added");
    TEST("operation_add_response");
    oa_operation_add_response(op, "200", "OK", "application/json", "#/components/schemas/User");
    CHECK(op->response_count == 1, "response added");
    TEST("operation_add_param");
    oa_operation_add_param(op, "limit", OA_IN_QUERY, OA_INTEGER, false, "Max results");
    CHECK(op->param_count == 1, "param added");
    TEST("operation_add_body");
    oa_operation_add_body(op, "#/components/schemas/User", OA_OBJECT);
    CHECK(op->has_request_body, "body added");
    TEST("operation_deprecated");
    oa_operation_set_deprecated(op, true);
    CHECK(op->deprecated, "deprecated");
    TEST("add_security");
    oa_security_scheme_t* sec = oa_spec_add_security(&spec, "Bearer", OA_SEC_HTTP, "JWT auth");
    CHECK(sec != NULL, "security added");
    oa_security_set_http_bearer(sec, "bearer", "JWT");
    CHECK(strcmp(sec->scheme, "bearer") == 0, "bearer");
    TEST("enum_and_ref");
    oa_schema_add_enum_value(s, "active");
    CHECK(s->enum_count == 1, "enum value");
    oa_schema_set_ref(s, "#/components/schemas/User");
    CHECK(strlen(s->ref) > 0, "ref set");
}

static void test_openapi_json(void) {
    printf("\n--- OpenAPI JSON Generation Tests ---\n");
    TEST("spec_to_json");
    oa_spec_t spec;
    oa_spec_init(&spec, "Mini API", "1.0.0", "A small API");
    oa_spec_add_server(&spec, "http://localhost:3000", "Local");
    char buf[4096];
    char* json = oa_spec_to_json(&spec, buf, sizeof(buf));
    CHECK(json != NULL, "JSON generated");
    CHECK(strstr(buf, "\"openapi\":\"3.0.3\"") != NULL, "openapi version");
    CHECK(strstr(buf, "\"title\":\"Mini API\"") != NULL, "title");
    CHECK(strstr(buf, "localhost:3000") != NULL, "server URL");
    TEST("spec_to_json_with_ops");
    oa_operation_t* op = oa_spec_add_operation(&spec, "/hello", OA_GET, "getHello", "Say hello");
    oa_operation_add_response(op, "200", "Success", "application/json", "#/components/schemas/Greeting");
    json = oa_spec_to_json(&spec, buf, sizeof(buf));
    CHECK(strstr(buf, "getHello") != NULL, "op id in JSON");
    TEST("spec_to_json_null");
    CHECK(oa_spec_to_json(NULL, buf, 100) == NULL, "null spec");
}

static void test_openapi_validation(void) {
    printf("\n--- OpenAPI Validation Tests ---\n");
    TEST("validate_valid");
    oa_spec_t spec;
    oa_spec_init(&spec, "Valid", "1.0.0", "OK");
    CHECK(oa_spec_validate(&spec), "valid passes");
    TEST("validate_no_title");
    oa_spec_t empty;
    memset(&empty, 0, sizeof(empty));
    CHECK(!oa_spec_validate(&empty), "no title fails");
    TEST("validate_schema");
    oa_schema_t s;
    memset(&s, 0, sizeof(s));
    CHECK(!oa_validate_schema(&s), "no name fails");
    strcpy(s.name, "Test");
    s.type = OA_STRING;
    CHECK(oa_validate_schema(&s), "valid schema passes");
    TEST("validate_operation");
    oa_operation_t op;
    memset(&op, 0, sizeof(op));
    CHECK(!oa_validate_operation(&op), "no op id fails");
    strcpy(op.operation_id, "testOp");
    strcpy(op.path, "/test");
    op.method = OA_GET;
    CHECK(oa_validate_operation(&op), "valid op passes");
    TEST("has_path_param");
    oa_operation_add_param(&op, "id", OA_IN_PATH, OA_STRING, true, "ID");
    CHECK(oa_operation_has_path_param(&op, "id"), "finds param");
    CHECK(!oa_operation_has_path_param(&op, "nonexistent"), "no fake param");
    TEST("path_count");
    oa_spec_t spec2;
    oa_spec_init(&spec2, "T", "1", "t");
    oa_spec_add_operation(&spec2, "/a", OA_GET, "op1", "a1");
    oa_spec_add_operation(&spec2, "/a", OA_POST, "op2", "a2");
    oa_spec_add_operation(&spec2, "/b", OA_GET, "op3", "b");
    CHECK(oa_spec_path_count(&spec2) == 2, "2 unique paths");
}

static void test_openapi_merge(void) {
    printf("\n--- OpenAPI Merge Tests ---\n");
    TEST("spec_merge");
    oa_spec_t s1, s2;
    oa_spec_init(&s1, "Main", "1.0.0", "Main spec");
    oa_spec_init(&s2, "Aux", "1.0.0", "Aux spec");
    oa_spec_add_operation(&s1, "/users", OA_GET, "listUsers", "List");
    oa_spec_add_operation(&s2, "/pets", OA_GET, "listPets", "Pets");
    oa_spec_merge(&s1, &s2);
    CHECK(s1.op_count == 2, "merged op count");
    TEST("find_operation");
    CHECK(oa_spec_find_operation(&s1, "listUsers") != NULL, "find op");
    CHECK(oa_spec_find_operation(&s1, "noop") == NULL, "no fake op");
    TEST("find_schema");
    oa_spec_add_schema(&s1, "Pet", OA_OBJECT, "Pet");
    CHECK(oa_spec_find_schema(&s1, "Pet") != NULL, "find schema");
    CHECK(oa_spec_find_schema(&s1, "Unknown") == NULL, "no fake schema");
}

static void test_openapi_yaml(void) {
    printf("\n--- OpenAPI YAML Export Tests ---\n");
    TEST("export_yaml");
    oa_spec_t spec;
    oa_spec_init(&spec, "YAML API", "2.0.0", "A YAML API");
    oa_spec_add_server(&spec, "https://api.example.com", "Prod");
    oa_operation_t* op = oa_spec_add_operation(&spec, "/items", OA_GET, "listItems", "List");
    oa_operation_add_response(op, "200", "OK", "application/json", "#/components/schemas/Item");
    char buf[4096];
    CHECK(oa_spec_export_yaml(&spec, buf, sizeof(buf)), "yaml export");
    CHECK(strstr(buf, "openapi: \"3.0.3\"") != NULL, "openapi version");
    CHECK(strstr(buf, "title: YAML API") != NULL, "title");
    TEST("export_yaml_schema");
    oa_schema_t s;
    memset(&s, 0, sizeof(s));
    strcpy(s.name, "Item");
    s.type = OA_OBJECT;
    oa_schema_add_prop(&s, "id", OA_INTEGER, "int64", true, "ID");
    oa_schema_add_prop(&s, "name", OA_STRING, NULL, true, "Name");
    char sbuf[2048];
    CHECK(oa_spec_export_yaml_schema(&s, sbuf, sizeof(sbuf)), "schema yaml");
    CHECK(strstr(sbuf, "type: object") != NULL, "type field");
}


static void test_api_version_core(void) {
    printf("\n--- API Version Core Tests ---\n");
    TEST("strategy_string");
    CHECK(strcmp(av_strategy_string(AV_URI_PATH), "URI_PATH") == 0, "strategy string");
    TEST("deprecation_string");
    CHECK(strcmp(av_deprecation_level_string(AV_SUNSET), "SUNSET") == 0, "deprec string");
    TEST("parse_simple");
    av_version_t v;
    av_version_parse(&v, "1.2.3");
    CHECK(v.major == 1 && v.minor == 2 && v.patch == 3, "parse");
    TEST("parse_with_v");
    av_version_parse(&v, "v2.0.0");
    CHECK(v.major == 2, "v prefix");
    TEST("parse_prerelease");
    av_version_parse(&v, "1.0.0-alpha");
    CHECK(v.is_prerelease && strcmp(v.prerelease_label, "alpha") == 0, "prerelease");
    TEST("compare_equal");
    av_version_t a, b;
    av_version_parse(&a, "1.2.3");
    av_version_parse(&b, "1.2.3");
    CHECK(av_version_compare(a, b) == 0, "equal");
    TEST("compare_major");
    av_version_parse(&b, "2.0.0");
    CHECK(av_version_compare(a, b) < 0, "a < b");
    TEST("compare_prerelease");
    av_version_parse(&a, "1.0.0-alpha");
    av_version_parse(&b, "1.0.0");
    CHECK(av_version_compare(a, b) < 0, "pre-release < release");
    TEST("is_supported");
    av_version_t minv, maxv, testv;
    av_version_parse(&minv, "1.0.0");
    av_version_parse(&maxv, "3.0.0");
    av_version_parse(&testv, "2.0.0");
    CHECK(av_version_is_supported(testv, minv, maxv), "2.0.0 supported");
    av_version_parse(&testv, "0.9.0");
    CHECK(!av_version_is_supported(testv, minv, maxv), "0.9.0 too old");
    TEST("version_format");
    char buf[32];
    av_version_parse(&a, "1.0.0-alpha");
    av_version_format(a, buf, sizeof(buf));
    CHECK(strcmp(buf, "1.0.0-alpha") == 0, "format");
}

static void test_api_version_bump(void) {
    printf("\n--- API Version Bump Tests ---\n");
    TEST("bump_major");
    av_version_t v;
    av_version_parse(&v, "1.2.3-beta");
    av_version_bump_major(&v);
    CHECK(v.major == 2 && v.minor == 0 && v.patch == 0 && !v.is_prerelease, "major bump");
    TEST("bump_minor");
    av_version_parse(&v, "1.2.3");
    av_version_bump_minor(&v);
    CHECK(v.major == 1 && v.minor == 3 && v.patch == 0, "minor bump");
    TEST("bump_patch");
    av_version_parse(&v, "1.2.3");
    av_version_bump_patch(&v);
    CHECK(v.major == 1 && v.minor == 2 && v.patch == 4, "patch bump");
    TEST("bump_null");
    av_version_bump_major(NULL); av_version_bump_minor(NULL); av_version_bump_patch(NULL);
    PASS();
}

static void test_api_version_router(void) {
    printf("\n--- API Version Router Tests ---\n");
    TEST("router_init");
    av_router_t r;
    av_router_init(&r, "/api", AV_URI_PATH);
    CHECK(r.default_strategy == AV_URI_PATH, "strategy");
    TEST("register_version");
    av_router_register_version(&r, "1.0.0");
    av_router_register_version(&r, "2.0.0");
    CHECK(r.version_count == 2, "2 versions");
    TEST("set_current");
    av_router_set_current(&r, "2.0.0");
    CHECK(r.current_version.major == 2, "current set");
    TEST("parse_uri");
    av_request_t req;
    CHECK(av_router_parse_request(&r, &req, "/api/v2/users", NULL, NULL), "parse uri");
    CHECK(req.version.major == 2, "v2 parsed");
    TEST("router_match");
    CHECK(av_router_match(&r, &req), "v2 matches");
    TEST("router_no_match");
    av_request_t req3;
    av_router_parse_request(&r, &req3, "/api/v99/unknown", NULL, NULL);
    CHECK(!av_router_match(&r, &req3), "v99 no match");
    TEST("set_deprecation");
    av_deprecation_t* dep = av_router_set_deprecation(&r, "1.0.0", AV_MINOR, "2025-01-01", "Use v2");
    CHECK(dep != NULL && dep->level == AV_MINOR, "deprecation set");
    TEST("set_sunset");
    av_router_set_sunset(&r, "1.0.0", "2025-12-31", "https://docs.example.com");
    CHECK(r.deprecation_count == 2, "sunset adds deprecation");
    TEST("check_deprecation");
    av_request_t req1;
    av_router_parse_request(&r, &req1, "/api/v1/users", NULL, NULL);
    const av_deprecation_t* cd = av_router_check_deprecation(&r, &req1);
    CHECK(cd != NULL, "v1 deprecated");
    TEST("is_sunset");
    CHECK(av_router_is_sunset(&r, &req1), "v1 sunset");
}

static void test_api_version_headers(void) {
    printf("\n--- API Version Header Tests ---\n");
    TEST("build_sunset_header");
    av_deprecation_t dep;
    memset(&dep, 0, sizeof(dep));
    strcpy(dep.sunset_date, "2025-12-31T23:59:59Z");
    char buf[256];
    char* hdr = av_build_sunset_header(&dep, buf, sizeof(buf));
    CHECK(hdr != NULL && strstr(buf, "Sunset:") != NULL, "sunset header");
    TEST("build_deprecation_header");
    hdr = av_build_deprecation_header(&dep, buf, sizeof(buf));
    CHECK(hdr != NULL && strstr(buf, "Deprecation: true") != NULL, "deprec header");
    TEST("build_version_uri_path");
    av_router_t r;
    av_router_init(&r, "/api", AV_URI_PATH);
    hdr = av_build_version_uri(&r, "2", "users", buf, sizeof(buf));
    CHECK(strcmp(buf, "/api/v2/users") == 0, "URI path");
    TEST("build_version_uri_query");
    av_router_init(&r, "/api", AV_QUERY_PARAM);
    hdr = av_build_version_uri(&r, "3", "users", buf, sizeof(buf));
    CHECK(strcmp(buf, "/api/users?v=3") == 0, "query param");
}

static void test_api_version_semver(void) {
    printf("\n--- Semver Range Tests ---\n");
    TEST("range_parse_caret");
    av_version_range_t range;
    CHECK(av_range_parse("^1.2.3", &range), "caret parse");
    CHECK(range.type == AV_RANGE_CARET && range.has_upper, "caret");
    TEST("range_parse_tilde");
    CHECK(av_range_parse("~1.2.3", &range), "tilde parse");
    CHECK(range.type == AV_RANGE_TILDE, "tilde");
    TEST("range_parse_gte");
    CHECK(av_range_parse(">=2.0.0", &range), "gte parse");
    CHECK(!range.has_upper, "gte no upper");
    TEST("range_satisfies_caret");
    av_range_parse("^1.2.3", &range);
    av_version_t v;
    av_version_parse(&v, "1.3.0");
    CHECK(av_range_satisfies(&range, v), "1.3.0 in ^1.2.3");
    av_version_parse(&v, "2.0.0");
    CHECK(!av_range_satisfies(&range, v), "2.0.0 not in ^1.2.3");
    TEST("range_satisfies_tilde");
    av_range_parse("~1.2.3", &range);
    av_version_parse(&v, "1.2.5");
    CHECK(av_range_satisfies(&range, v), "1.2.5 in ~1.2.3");
    av_version_parse(&v, "1.3.0");
    CHECK(!av_range_satisfies(&range, v), "1.3.0 not in ~1.2.3");
}

static void test_api_version_compat(void) {
    printf("\n--- API Version Compatibility Tests ---\n");
    TEST("is_prerelease");
    av_version_t v;
    av_version_parse(&v, "1.0.0-beta");
    CHECK(av_version_is_prerelease(v), "beta prerelease");
    av_version_parse(&v, "1.0.0");
    CHECK(!av_version_is_prerelease(v), "release not prerelease");
    TEST("has_major_change");
    av_version_t a, b;
    av_version_parse(&a, "1.0.0");
    av_version_parse(&b, "2.0.0");
    CHECK(av_version_has_major_change(a, b), "major change");
    av_version_parse(&b, "1.1.0");
    CHECK(!av_version_has_major_change(a, b), "not major");
    TEST("has_minor_change");
    av_version_parse(&b, "1.1.0");
    CHECK(av_version_has_minor_change(a, b), "minor change");
    av_version_parse(&b, "1.0.1");
    CHECK(!av_version_has_minor_change(a, b), "not minor");
    TEST("is_backward_compatible");
    av_version_parse(&a, "1.0.0");
    av_version_parse(&b, "1.5.0");
    CHECK(av_version_is_backward_compatible(a, b), "1.5 compat with 1.0");
    av_version_parse(&b, "2.0.0");
    CHECK(!av_version_is_backward_compatible(a, b), "2.0 not compat");
    TEST("changelog_render");
    av_router_t r;
    av_router_init(&r, "/api", AV_URI_PATH);
    av_router_register_version(&r, "1.0.0");
    av_router_set_current(&r, "2.0.0");
    av_router_set_deprecation(&r, "1.0.0", AV_SUNSET, "2025-01-01", "v1 sunset");
    char buf[4096];
    char* cl = av_changelog_render(&r, buf, sizeof(buf));
    CHECK(cl != NULL && strstr(buf, "SUNSET") != NULL, "changelog SUNSET");
    CHECK(strstr(buf, "Current") != NULL, "changelog Current");
}

static void test_cross_module(void) {
    printf("\n--- Cross-Module Integration Tests ---\n");
    TEST("rest_to_openapi_status");
    rest_status_t rest_stats[] = {REST_200_OK, REST_201_CREATED, REST_404_NOT_FOUND, REST_500_INTERNAL_SERVER_ERROR};
    const char* codes[] = {"200", "201", "404", "500"};
    for (int i = 0; i < 4; i++) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", (int)rest_stats[i]);
        CHECK(strcmp(buf, codes[i]) == 0, "status code match");
    }
    TEST("openapi_path_count");
    oa_spec_t spec;
    oa_spec_init(&spec, "Test", "1.0.0", "Test");
    oa_spec_add_operation(&spec, "/users", OA_GET, "listUsers", "List");
    oa_spec_add_operation(&spec, "/users", OA_POST, "createUser", "Create");
    CHECK(oa_spec_path_count(&spec) == 1, "1 unique path");
    CHECK(spec.op_count == 2, "2 ops");
}

static void test_advanced_principles(void) {
    printf("\n--- Advanced API Design Principles ---\n");
    TEST("uniform_interface");
    rest_resource_t r;
    rest_resource_init(&r, "item", "/items/42", REST_GET);
    rest_resource_set_body(&r, "{\"id\":42}");
    rest_resource_add_link(&r, "self", "/items/42", "GET");
    CHECK(r.path[0] == '/' && r.response_len > 0 && r.link_count > 0, "uniform interface");
    TEST("stateless_constraint");
    rest_resource_t r2;
    rest_resource_init(&r2, "stateless", "/state", REST_GET);
    rest_resource_add_header(&r2, "Authorization", "Bearer token123");
    CHECK(r2.header_count > 0, "auth per-request");
    TEST("grpc_idempotency");
    grpc_sim_t sim;
    grpc_sim_init(&sim);
    grpc_service_def_t* svc = grpc_sim_add_service(&sim, "Idempotent", "test");
    grpc_sim_add_method(svc, "GetItem", GRPC_UNARY, "GetReq", "Item");
    grpc_sim_add_method(svc, "WatchItems", GRPC_SERVER_STREAM, "WatchReq", "Item");
    CHECK(svc->method_count == 2, "both methods registered");
}

int main(void) {
    printf("=== mini-api-engineering: Comprehensive Test Suite ===\n\n");
    test_rest_methods();
    test_rest_router();
    test_rest_etag();
    test_rest_cors_rate();
    test_rest_negotiation();
    test_graphql_core();
    test_graphql_parse();
    test_graphql_complexity();
    test_graphql_validation();
    test_grpc_core();
    test_grpc_streams();
    test_grpc_varint_zigzag();
    test_grpc_wire_format();
    test_grpc_h2_frames();
    test_grpc_health();
    test_openapi_core();
    test_openapi_json();
    test_openapi_validation();
    test_openapi_merge();
    test_openapi_yaml();
    test_api_version_core();
    test_api_version_bump();
    test_api_version_router();
    test_api_version_headers();
    test_api_version_semver();
    test_api_version_compat();
    test_cross_module();
    test_advanced_principles();
    printf("\n=== Results: %d passed, %d failed ===\n", tests_pass, tests_fail);
    return tests_fail > 0 ? 1 : 0;
}
