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

void bench_jsonc(const char *dir, void (*emit)(const char *, const char *, const char *, int, double)) {
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
void bench_jsonc(const char *dir, void (*emit)(const char *, const char *, const char *, int, double)) { (void)dir; (void)emit; }
#endif

extern void bench_cpp(const char *dir, void (*emit)(const char *, const char *, const char *, int, double));

void bench_extra(const char *dir, void (*emit)(const char *, const char *, const char *, int, double),
                 void (*ser)(const char *, const char *, const char *)) {
  bench_jsonc(dir, emit);
  bench_cpp(dir, emit);
#if HAVE_JSON_C
  ser("json-c", "json", json_c_version());
#endif
#if HAVE_NLOHMANN
  ser("nlohmann-json", "json", "3.x");
#endif
#if HAVE_RYML
  ser("rapidyaml", "yaml", "0.7");
#endif
}
