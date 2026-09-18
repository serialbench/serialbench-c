/* serialbench-c: the C harness of the cross-language benchmark family.
 * Loads the canonical fixtures, times parse (+ free), and writes the shared
 * data-repo YAML schema. Same iteration discipline as the ruby/python
 * harnesses: warmup 3, then small 10 / medium 3 / large 1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#if HAVE_LEPTRIS
#include <leptris.h>
#endif

#if defined(__APPLE__)
#include <dlfcn.h>
#endif

typedef struct {
  const char *name;
  double time_per_iteration;
  double ips;
} row_t;

static char *read_file(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  *len = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc(*len + 1);
  if (fread(buf, 1, *len, f) != *len) { free(buf); fclose(f); return NULL; }
  buf[*len] = 0;
  fclose(f);
  return buf;
}

static double now_sec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

static row_t bench_libxml2(const char *data, size_t len, int iterations) {
  for (int i = 0; i < 3; i++) {
    xmlDocPtr doc = xmlReadMemory(data, (int)len, NULL, NULL, XML_PARSE_NOBLANKS);
    if (doc) xmlFreeDoc(doc);
  }
  double t0 = now_sec();
  for (int i = 0; i < iterations; i++) {
    xmlDocPtr doc = xmlReadMemory(data, (int)len, NULL, NULL, XML_PARSE_NOBLANKS);
    if (doc) xmlFreeDoc(doc);
  }
  double elapsed = now_sec() - t0;
  return (row_t){"libxml2", elapsed / iterations, iterations / elapsed};
}

#if HAVE_LEPTRIS
static row_t bench_leptris(const char *data, size_t len, int iterations) {
  LeptrisStatus st = LEPTRIS_OK;
  for (int i = 0; i < 3; i++) {
    LeptrisDocument doc = leptris_parse_string(data, len, &st);
    if (st == LEPTRIS_OK) leptris_document_free(doc);
  }
  double t0 = now_sec();
  for (int i = 0; i < iterations; i++) {
    LeptrisDocument doc = leptris_parse_string(data, len, &st);
    if (st == LEPTRIS_OK) leptris_document_free(doc);
  }
  double elapsed = now_sec() - t0;
  return (row_t){"leptris", elapsed / iterations, iterations / elapsed};
}
#endif

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: serialbench-c <fixtures-dir> <out-results-yaml>\n");
    return 2;
  }
  LIBXML_TEST_VERSION

  static const char *sizes[] = {"small", "medium", "large"};
  static const int iterations[] = {10, 3, 1};

  FILE *out = fopen(argv[2], "w");
  if (!out) { perror("open out"); return 2; }

  fprintf(out, "platform:\n");
  fprintf(out, "  platform_string: %s-c-native\n", getenv("SERIALBENCH_PLATFORM") ? getenv("SERIALBENCH_PLATFORM") : "local");
  fprintf(out, "  kind: native\n");
  fprintf(out, "  os: %s\n",
#ifdef __APPLE__
      "macos"
#elif defined(_WIN32)
      "windows"
#else
      "linux"
#endif
  );
  fprintf(out, "  arch: %s\n",
#if defined(__aarch64__) || defined(_M_ARM64)
      "arm64"
#else
      "x86_64"
#endif
  );
  fprintf(out, "  runtime: c\n");
  fprintf(out, "  runtime_version: %s\n", getenv("SERIALBENCH_C_VERSION") ? getenv("SERIALBENCH_C_VERSION") : __VERSION__);
  fprintf(out, "benchmark_config:\n  name: c-full-xml\n  formats: [xml]\n  operations: [parsing]\n");
  const char *leptris_ver =
#if HAVE_LEPTRIS
      "1.9.x";
#else
      "unavailable";
#endif
  fprintf(out, "  serializers:\n");
  fprintf(out, "    - name: libxml2\n      format: xml\n      version: %s\n      features: {xpath: true, namespaces: true}\n",
          LIBXML_DOTTED_VERSION);
#if HAVE_LEPTRIS
  fprintf(out, "    - name: leptris\n      format: xml\n      version: %s\n      features: {xpath: true, namespaces: true}\n", leptris_ver);
#endif
  fprintf(out, "benchmark_result:\n  parsing:\n");

  for (int s = 0; s < 3; s++) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s.xml", argv[1], sizes[s]);
    size_t len;
    char *data = read_file(path, &len);
    if (!data) { fprintf(stderr, "missing fixture %s\n", path); continue; }

    row_t r = bench_libxml2(data, len, iterations[s]);
    fprintf(out, "    - adapter: %s\n      format: xml\n      data_size: %s\n"
                 "      time_per_iterations: %.9f\n      time_per_iteration: %.9f\n"
                 "      iterations_per_second: %.6f\n      iterations_count: %d\n",
            r.name, sizes[s], r.time_per_iteration * iterations[s], r.time_per_iteration, r.ips, iterations[s]);
#if HAVE_LEPTRIS
    r = bench_leptris(data, len, iterations[s]);
    fprintf(out, "    - adapter: %s\n      format: xml\n      data_size: %s\n"
                 "      time_per_iterations: %.9f\n      time_per_iteration: %.9f\n"
                 "      iterations_per_second: %.6f\n      iterations_count: %d\n",
            r.name, sizes[s], r.time_per_iteration * iterations[s], r.time_per_iteration, r.ips, iterations[s]);
#endif
    free(data);
    printf("%s: done\n", sizes[s]);
  }
  fclose(out);
  xmlCleanupParser();
  return 0;
}
