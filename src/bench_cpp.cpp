#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <functional>

extern "C" const char *g_cur_op;
extern "C" int g_check_failed;

#if HAVE_NLOHMANN
#include <nlohmann/json.hpp>
#endif
#if HAVE_RYML
#include <ryml.hpp>
#endif
#if HAVE_SIMDJSON
#include <simdjson.h>
#endif
#if HAVE_PUGIXML
#include <pugixml.hpp>
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

#if HAVE_PUGIXML
struct pugi_buf_writer : pugi::xml_writer {
  void write(const void *data, size_t size) override { n += size; (void)data; }
  size_t n = 0;
};
#endif

extern "C" void bench_cpp(const char *dir, const char *want, void (*emit)(const char *, const char *, const char *, int, double)) {
  (void)want;
  static const char *sizes[] = {"small", "medium", "large"};
  static const int iters[] = {10, 3, 1};
  char path[600];

#if HAVE_NLOHMANN
  if (!want || !strcmp(want, "json"))
  for (int s = 0; s < 3; s++) {
    snprintf(path, sizeof path, "%s/%s.json", dir, sizes[s]);
    size_t len; char *data = read_file_cxx(path, &len);
    if (!data) continue;
    for (int i = 0; i < 3; i++) { auto j = nlohmann::json::parse(data, data + len); (void)j; }
    double t0 = now_cxx();
    for (int i = 0; i < iters[s]; i++) { auto j = nlohmann::json::parse(data, data + len); (void)j; }
    emit("nlohmann-json", "json", sizes[s], iters[s], now_cxx() - t0);

    g_cur_op = "generation";
    for (int i = 0; i < 3; i++) { auto j = nlohmann::json::parse(data, data + len); auto s2 = j.dump(); }
    t0 = now_cxx();
    for (int i = 0; i < iters[s]; i++) { auto j = nlohmann::json::parse(data, data + len); auto s2 = j.dump(); }
    emit("nlohmann-json", "json", sizes[s], iters[s], now_cxx() - t0);
    g_cur_op = "parsing";
    free(data);
  }
#endif

#if HAVE_RYML
  if (!want || !strcmp(want, "yaml"))
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

    g_cur_op = "generation";
    for (int i = 0; i < 3; i++) {
      memcpy(buf, data, len + 1);
      ryml::Tree tree = ryml::parse_in_place(ryml::substr(buf, len));
      size_t cap = len * 4;
      char *ebuf = (char *)malloc(cap);
      ryml::emit_yaml(tree, ryml::substr(ebuf, cap));
      free(ebuf);
    }
    t0 = now_cxx();
    for (int i = 0; i < iters[s]; i++) {
      memcpy(buf, data, len + 1);
      ryml::Tree tree = ryml::parse_in_place(ryml::substr(buf, len));
      size_t cap = len * 4;
      char *ebuf = (char *)malloc(cap);
      ryml::emit_yaml(tree, ryml::substr(ebuf, cap));
      free(ebuf);
    }
    emit("rapidyaml", "yaml", sizes[s], iters[s], now_cxx() - t0);
    g_cur_op = "parsing";
    free(buf); free(data);
  }
#endif

#if HAVE_SIMDJSON
  if (!want || !strcmp(want, "json"))
  for (int s = 0; s < 3; s++) {
    snprintf(path, sizeof path, "%s/%s.json", dir, sizes[s]);
    size_t len; char *data = read_file_cxx(path, &len);
    if (!data) continue;
    simdjson::dom::parser parser;
    simdjson::padded_string pdoc(data, len);
    for (int i = 0; i < 3; i++) { auto e = parser.parse(pdoc).value(); (void)e; }
    double t0 = now_cxx();
    for (int i = 0; i < iters[s]; i++) { auto e = parser.parse(pdoc).value(); (void)e; }
    emit("simdjson", "json", sizes[s], iters[s], now_cxx() - t0);
    free(data);
  }
#endif

#if HAVE_PUGIXML
  if (!want || !strcmp(want, "xml"))
  for (int s = 0; s < 3; s++) {
    snprintf(path, sizeof path, "%s/%s.xml", dir, sizes[s]);
    size_t len; char *data = read_file_cxx(path, &len);
    if (!data) continue;
    for (int i = 0; i < 3; i++) { pugi::xml_document doc; doc.load_buffer(data, len); }
    double t0 = now_cxx();
    for (int i = 0; i < iters[s]; i++) { pugi::xml_document doc; doc.load_buffer(data, len); }
    emit("pugixml", "xml", sizes[s], iters[s], now_cxx() - t0);

    g_cur_op = "generation";
    for (int i = 0; i < 3; i++) { pugi::xml_document doc; doc.load_buffer(data, len); pugi_buf_writer w; doc.print(w); }
    t0 = now_cxx();
    for (int i = 0; i < iters[s]; i++) {
      pugi::xml_document doc; doc.load_buffer(data, len);
      pugi_buf_writer w;
      doc.print(w);
    }
    emit("pugixml", "xml", sizes[s], iters[s], now_cxx() - t0);

    g_cur_op = "xpath";
    static const char *qs[] = {"//user | //record", "//user[@id='101']", "//preferences/theme"};
    for (int i = 0; i < 3; i++) { pugi::xml_document doc; doc.load_buffer(data, len); for (const char *q : qs) doc.select_nodes(q); }
    t0 = now_cxx();
    for (int i = 0; i < iters[s]; i++) {
      pugi::xml_document doc; doc.load_buffer(data, len);
      for (const char *q : qs) doc.select_nodes(q);
    }
    emit("pugixml", "xml", sizes[s], iters[s], now_cxx() - t0);
    g_cur_op = "parsing";
    free(data);
  }
#endif
}

extern "C" void bench_cpp_check(const char *dir) {
  static const char *sizes[] = {"small", "medium", "large"};
  char path[600];
#if HAVE_PUGIXML
  for (int s = 0; s < 3; s++) {
    snprintf(path, sizeof path, "%s/%s.xml", dir, sizes[s]);
    size_t len; char *data = read_file_cxx(path, &len);
    if (!data) continue;
    pugi::xml_document doc;
    pugi::xml_parse_result r = doc.load_buffer(data, len);
    long n = r ? (long)doc.select_nodes("//*").size() : -1;
    printf("CHECK xml pugixml %s count=%ld %s\n", sizes[s], n, r && n > 0 ? "OK" : "FAIL");
    if (!r || n <= 0) g_check_failed = 1;
    free(data);
  }
#endif
#if HAVE_NLOHMANN
  for (int s = 0; s < 3; s++) {
    snprintf(path, sizeof path, "%s/%s.json", dir, sizes[s]);
    size_t len; char *data = read_file_cxx(path, &len);
    if (!data) continue;
    try {
      auto j = nlohmann::json::parse(data, data + len);
      std::function<long(const nlohmann::json&)> count = [&](const nlohmann::json &v) -> long {
        if (v.is_object() || v.is_array()) {
          long n = 1;
          for (auto it = v.begin(); it != v.end(); ++it) n += count(*it);
          return n;
        }
        return 1;
      };
      printf("CHECK json nlohmann-json %s count=%ld OK\n", sizes[s], count(j));
    } catch (...) {
      printf("CHECK json nlohmann-json %s FAIL\n", sizes[s]);
      g_check_failed = 1;
    }
    free(data);
  }
#endif
#if HAVE_SIMDJSON
  for (int s = 0; s < 3; s++) {
    snprintf(path, sizeof path, "%s/%s.json", dir, sizes[s]);
    size_t len; char *data = read_file_cxx(path, &len);
    if (!data) continue;
    simdjson::dom::parser parser;
    simdjson::padded_string pdoc(data, len);
    auto er = parser.parse(pdoc);
    if (er.error() == simdjson::SUCCESS) {
      printf("CHECK json simdjson %s count=-1 OK\n", sizes[s]);
    } else {
      printf("CHECK json simdjson %s FAIL\n", sizes[s]);
      g_check_failed = 1;
    }
    free(data);
  }
#endif
}

extern "C" void bench_cpp_serializers(void (*ser)(const char *, const char *, const char *)) {
#if HAVE_SIMDJSON
  ser("simdjson", "json", SIMDJSON_VERSION);
#endif
#if HAVE_PUGIXML
  char v[16];
  snprintf(v, sizeof v, "%d.%d", PUGIXML_VERSION / 1000, PUGIXML_VERSION % 1000 / 10);
  ser("pugixml", "xml", v);
#endif
}
