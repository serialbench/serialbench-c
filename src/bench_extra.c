/* json-c lives here: its typedef json_object collides with jansson's
 * json_object() function, so the two cannot share a TU. */
#if HAVE_JSON_C
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <json-c/json.h>

static char *read_file_x(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END); *len = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
  char *b = malloc(*len + 1);
  if (fread(b, 1, *len, f) != *len) { free(b); fclose(f); return NULL; }
  b[*len] = 0; fclose(f); return b;
}
static double now_x(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }

void bench_jsonc(const char *dir, const char *want, void (*emit)(const char *, const char *, const char *, int, double)) {
  if (want && strcmp(want, "json")) return;
  static const char *sizes[] = {"small", "medium", "large"};
  static const int iters[] = {10, 3, 1};
  char path[512];
  for (int s = 0; s < 3; s++) {
    snprintf(path, sizeof path, "%s/%s.json", dir, sizes[s]);
    size_t len; char *data = read_file_x(path, &len);
    if (!data) continue;
    for (int i = 0; i < 3; i++) { struct json_object *o = json_tokener_parse(data); if (o) json_object_put(o); }
    double t0 = now_x();
    for (int i = 0; i < iters[s]; i++) { struct json_object *o = json_tokener_parse(data); if (o) json_object_put(o); }
    emit("json-c", "json", sizes[s], iters[s], now_x() - t0);
    free(data);
  }
}
#else
#include <stddef.h>
void bench_jsonc(const char *dir, const char *want, void (*emit)(const char *, const char *, const char *, int, double)) {
  if (want && strcmp(want, "json")) return; (void)dir; (void)emit; }
#endif

#if HAVE_TINYCBOR
/* tinycbor lives in its own TU: its cbor.h collides with libcbor's. The
 * exact path arrives via TINYCBOR_CBOR_H so no -I ordering games. */
#include TINYCBOR_CBOR_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char *read_file_tc(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END); *len = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
  char *b = malloc(*len + 1);
  if (fread(b, 1, *len, f) != *len) { free(b); fclose(f); return NULL; }
  b[*len] = 0; fclose(f); return b;
}
static double now_tc(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }

static void walk_tinycbor(CborValue *it) {
  while (!cbor_value_at_end(it)) {
    if (cbor_value_is_container(it)) {
      CborValue rec;
      cbor_value_enter_container(it, &rec);
      walk_tinycbor(&rec);
      cbor_value_leave_container(it, &rec);
    } else {
      cbor_value_advance(it);
    }
  }
}

void bench_tinycbor(const char *dir, const char *want, void (*emit)(const char *, const char *, const char *, int, double)) {
  if (want && strcmp(want, "cbor")) return;
  static const char *sizes[] = {"small", "medium", "large"};
  static const int iters[] = {10, 3, 1};
  char path[512];
  for (int s = 0; s < 3; s++) {
    snprintf(path, sizeof path, "%s/%s.cbor", dir, sizes[s]);
    size_t len; char *data = read_file_tc(path, &len);
    if (!data) continue;
    for (int i = 0; i < 3; i++) {
      CborParser parser; CborValue it;
      if (cbor_parser_init((const uint8_t *)data, len, 0, &parser, &it) == CborNoError) walk_tinycbor(&it);
    }
    double t0 = now_tc();
    for (int i = 0; i < iters[s]; i++) {
      CborParser parser; CborValue it;
      if (cbor_parser_init((const uint8_t *)data, len, 0, &parser, &it) == CborNoError) walk_tinycbor(&it);
    }
    emit("tinycbor", "cbor", sizes[s], iters[s], now_tc() - t0);
    free(data);
  }
}
#else
#include <string.h>
void bench_tinycbor(const char *dir, const char *want, void (*emit)(const char *, const char *, const char *, int, double)) {
  if (want && strcmp(want, "cbor")) return; (void)dir; (void)emit; }
#endif

extern void bench_cpp(const char *dir, const char *want, void (*emit)(const char *, const char *, const char *, int, double));
extern void bench_cpp_serializers(void (*ser)(const char *, const char *, const char *));

void bench_extra(const char *dir, const char *want, void (*emit)(const char *, const char *, const char *, int, double),
                 void (*ser)(const char *, const char *, const char *)) {
  bench_jsonc(dir, want, emit);
  bench_tinycbor(dir, want, emit);
  bench_cpp(dir, want, emit);
  bench_cpp_serializers(ser);
  int json_ok = !want || !strcmp(want, "json");
  int yaml_ok = !want || !strcmp(want, "yaml");
  int cbor_ok = !want || !strcmp(want, "cbor");
#if HAVE_JSON_C
  if (json_ok) ser("json-c", "json", json_c_version());
#endif
#if HAVE_NLOHMANN
  if (json_ok) ser("nlohmann-json", "json", "3.x");
#endif
#if HAVE_RYML
  if (yaml_ok) ser("rapidyaml", "yaml", "0.7");
#endif
#if HAVE_TINYCBOR
  if (cbor_ok) ser("tinycbor", "cbor", "0.6");
#endif
}
