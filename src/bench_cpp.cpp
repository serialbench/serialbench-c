#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>

#if HAVE_NLOHMANN
#include <nlohmann/json.hpp>
#endif
#if HAVE_RYML
#include <ryml.hpp>
#endif

static char *read_file_cxx(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) return nullptr;
  fseek(f, 0, SEEK_END); *len = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
  char *b = (char *)malloc(*len + 1);
  if (fread(b, 1, *len, f) != *len) { free(b); fclose(f); return nullptr; }
  b[*len] = 0; fclose(f); return b;
}
static double now_cxx() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

extern "C" void bench_cpp(const char *dir, void (*emit)(const char *, const char *, const char *, int, double)) {
  static const char *sizes[] = {"small", "medium", "large"};
  static const int iters[] = {10, 3, 1};
  char path[600];

#if HAVE_NLOHMANN
  for (int s = 0; s < 3; s++) {
    snprintf(path, sizeof path, "%s/%s.json", dir, sizes[s]);
    size_t len; char *data = read_file_cxx(path, &len);
    if (!data) continue;
    for (int i = 0; i < 3; i++) { auto j = nlohmann::json::parse(data, data + len); (void)j; }
    double t0 = now_cxx();
    for (int i = 0; i < iters[s]; i++) { auto j = nlohmann::json::parse(data, data + len); (void)j; }
    emit("nlohmann-json", "json", sizes[s], iters[s], now_cxx() - t0);
    free(data);
  }
#endif

#if HAVE_RYML
  for (int s = 0; s < 3; s++) {
    snprintf(path, sizeof path, "%s/%s.yaml", dir, sizes[s]);
    size_t len; char *data = read_file_cxx(path, &len);
    if (!data) continue;
    char *buf = (char *)malloc(len + 1);
    for (int i = 0; i < 3; i++) {
      memcpy(buf, data, len + 1);
      ryml::Tree tree = ryml::parse_in_place(ryml::substr(buf, len));
      (void)tree;
    }
    double t0 = now_cxx();
    for (int i = 0; i < iters[s]; i++) {
      memcpy(buf, data, len + 1);
      ryml::Tree tree = ryml::parse_in_place(ryml::substr(buf, len));
      (void)tree;
    }
    emit("rapidyaml", "yaml", sizes[s], iters[s], now_cxx() - t0);
    free(buf); free(data);
  }
#endif
}
