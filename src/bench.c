/* serialbench-c: cross-language benchmark harness, C runtime.
 * Measures the canonical fixtures with the native tris cores against the
 * packaged C field, writing the shared data-repo schema.
 * Usage: serialbench-c <fixtures-dir> <out-results-yaml> [format]
 * Formats: xml json yaml toml html xslt (default: all available).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if HAVE_LEPTRIS
#include <leptris.h>
#endif
#if HAVE_YEPTRIS
#include <yeptris.h>
#include <yeptris/json.h>
#include <yeptris/parse.h>
#endif
#if HAVE_TEPTRIS
#include <teptris/teptris.h>
#endif
#if HAVE_LIBXML2
#include <libxml/parser.h>
#include <libxml/HTMLparser.h>
#include <libxml/tree.h>
#endif
#if HAVE_LIBXSLT
#include <libxslt/xslt.h>
#include <libxslt/transform.h>
#include <libxslt/xsltutils.h>
#endif
#if HAVE_JANSSON
#include <jansson.h>
#endif
#if HAVE_TOMLC17
#include <tomlc17.h>
#endif
#if HAVE_LIBYAML
#include <yaml.h>
#endif

typedef struct { const char *name; const char *fmt; const char *size; double tpi; double ips; } row_t;
static row_t rows[256];
static int nrows = 0;
static const char *g_format = NULL;
static const char *dir_cache = NULL;
static void add_row(const char *name, const char *fmt, const char *size, int iters, double elapsed);
void serializer_ext(const char *name, const char *fmt, const char *version);
void add_row_ext(const char *name, const char *fmt, const char *size, int iters, double elapsed);
static void add_row(const char *name, const char *fmt, const char *size, int iters, double elapsed) {
  if (nrows >= 256) return;
  rows[nrows].name = name; rows[nrows].fmt = fmt; rows[nrows].size = size;
  rows[nrows].tpi = elapsed / iters; rows[nrows].ips = iters / elapsed;
  nrows++;
}

void serializer_ext(const char *name, const char *fmt, const char *version);
void add_row_ext(const char *name, const char *fmt, const char *size, int iters, double elapsed) {
  add_row(name, fmt, size, iters, elapsed);
}
static char ser_buf[2048];
static size_t ser_len = 0;
void serializer_ext(const char *name, const char *fmt, const char *version) {
  ser_len += (size_t)snprintf(ser_buf + ser_len, sizeof ser_buf - ser_len,
                              "    - {name: %s, format: %s, version: '%s', features: {}}\n", name, fmt, version);
}


static char *read_file(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END); *len = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
  char *b = malloc(*len + 1);
  if (fread(b, 1, *len, f) != *len) { free(b); fclose(f); return NULL; }
  b[*len] = 0; fclose(f); return b;
}
static double now_sec(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }

#define BENCH(label, fmt, size, iters, setup_expr, work_expr, free_expr) do { \
  for (int i = 0; i < 3; i++) { setup_expr; work_expr; free_expr; } \
  double t0 = now_sec(); \
  for (int i = 0; i < iters; i++) { setup_expr; work_expr; free_expr; } \
  add_row(label, fmt, size, iters, now_sec() - t0); \
} while (0)

static void run_format(const char *fmt, const char *dir, const char *fixtures_ext) {
  static const char *sizes[] = {"small", "medium", "large"};
  static const int iters[] = {10, 3, 1};
  char path[512];
  for (int s = 0; s < 3; s++) {
    snprintf(path, sizeof path, "%s/%s.%s", dir, sizes[s], fixtures_ext);
    size_t len; char *data = read_file(path, &len);
    if (!data) { fprintf(stderr, "missing %s\n", path); continue; }

    if (!strcmp(fmt, "xml")) {
#if HAVE_LEPTRIS
      BENCH("leptris", "xml", sizes[s], iters[s],, {
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisDocument d = leptris_parse_string(data, len, &st);
        if (st == LEPTRIS_OK) leptris_document_free(d);
      },);
#endif
#if HAVE_LIBXML2
      BENCH("libxml2", "xml", sizes[s], iters[s],, {
        xmlDocPtr d = xmlReadMemory(data, (int)len, NULL, NULL, XML_PARSE_NOBLANKS);
        if (d) xmlFreeDoc(d);
      },);
#endif
    }

    if (!strcmp(fmt, "html")) {
#if HAVE_LEPTRIS
      BENCH("leptris", "html", sizes[s], iters[s],, {
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisDocument d = leptris_parse_html_string(data, len, &st);
        if (st == LEPTRIS_OK) leptris_document_free(d);
      },);
#endif
#if HAVE_LIBXML2
      BENCH("libxml2", "html", sizes[s], iters[s],, {
        htmlDocPtr d = htmlReadMemory(data, (int)len, NULL, NULL, HTML_PARSE_NOBLANKS | HTML_PARSE_RECOVER);
        if (d) xmlFreeDoc(d);
      },);
#endif
    }

    if (!strcmp(fmt, "json")) {
#if HAVE_YEPTRIS
      BENCH("yeptris", "json", sizes[s], iters[s],, {
        YeptrisStatus st = 0;
        YeptrisDocument d = yeptris_parse_json(data, len, &st);
        if (st == 0) yeptris_document_free(d);
      },);
#endif
#if HAVE_JANSSON
      BENCH("jansson", "json", sizes[s], iters[s],, {
        json_error_t err;
        json_t *r = json_loadb(data, len, 0, &err);
        if (r) json_decref(r);
      },);
#endif
    }

    if (!strcmp(fmt, "yaml")) {
#if HAVE_YEPTRIS
      BENCH("yeptris", "yaml", sizes[s], iters[s],, {
        YeptrisStatus st = 0;
        YeptrisDocument d = yeptris_parse(data, len, &st);
        if (st == 0) yeptris_document_free(d);
      },);
#endif
#if HAVE_LIBYAML
      BENCH("libyaml", "yaml", sizes[s], iters[s],, {
        yaml_parser_t parser;
        if (yaml_parser_initialize(&parser)) {
          yaml_event_t event;
          yaml_parser_set_input_string(&parser, (unsigned char *)data, len);
          while (yaml_parser_parse(&parser, &event)) {
            int done = (event.type == YAML_STREAM_END_EVENT);
            yaml_event_delete(&event);
            if (done) break;
          }
          yaml_parser_delete(&parser);
        }
      },);
#endif
    }

    if (!strcmp(fmt, "toml")) {
#if HAVE_TEPTRIS
      BENCH("teptris", "toml", sizes[s], iters[s],, {
        teptris_document *d = NULL;
        if (teptris_parse(data, len, NULL, &d) == TEPTRIS_OK) teptris_document_free(d);
      },);
#endif
#if HAVE_TOMLC17
      BENCH("tomlc17", "toml", sizes[s], iters[s],, {
        toml_result_t r = toml_parse(data, (int)len);
        toml_free(r);
      },);
#endif
    }

    if (!strcmp(fmt, "xslt")) {
#if HAVE_LEPTRIS
      /* stylesheet compiled once per iteration = compile+apply, ruby parity */
      BENCH("leptris", "xslt", sizes[s], iters[s],, {
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisDocument src = leptris_parse_string(data, len, &st);
        if (st != LEPTRIS_OK) break;
        char xsl_path[512]; snprintf(xsl_path, sizeof xsl_path, "%s/transform.xsl", dir);
        size_t xl; char *xs = read_file(xsl_path, &xl);
        if (xs) {
          LeptrisXslt t = leptris_xslt_parse(xs, xl);
          LeptrisDocument out = leptris_xslt_apply(t, src);
          if (out) leptris_document_free(out);
          leptris_xslt_free(t);
          free(xs);
        }
        leptris_document_free(src);
      },);
#endif
#if HAVE_LIBXSLT && HAVE_LIBXML2
      BENCH("libxslt", "xslt", sizes[s], iters[s],, {
        xmlDocPtr src = xmlReadMemory(data, (int)len, NULL, NULL, XML_PARSE_NOBLANKS);
        if (!src) break;
        char xsl_path[512]; snprintf(xsl_path, sizeof xsl_path, "%s/transform.xsl", dir);
        size_t xl; char *xs = read_file(xsl_path, &xl);
        if (xs) {
          xmlDocPtr sx = xmlReadMemory(xs, (int)xl, NULL, NULL, XML_PARSE_NOBLANKS);
          if (sx) {
            xsltStylesheetPtr st = xsltParseStylesheetDoc(sx);
            if (st) {
              xmlDocPtr res = xsltApplyStylesheet(st, src, NULL);
              if (res) xmlFreeDoc(res);
              xsltFreeStylesheet(st);
            } else xmlFreeDoc(sx);
          }
          free(xs);
        }
        xmlFreeDoc(src);
      },);
#endif
    }
    free(data);
  }
}

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: serialbench-c <fixtures> <out-yaml> [format]\n"); return 2; }
#if HAVE_LIBXML2
  LIBXML_TEST_VERSION
#endif
  const char *all[] = {"xml", "html", "json", "yaml", "toml", "xslt"};
  dir_cache = argv[1];
  const char *want = argc > 3 ? argv[3] : NULL;
  for (unsigned i = 0; i < sizeof(all)/sizeof(all[0]); i++) {
    /* xml invocation also runs the xslt leg: its rows land in the xml file */
    if (want && strcmp(want, all[i]) && !(strcmp(want, "xml") == 0 && strcmp(all[i], "xslt") == 0)) continue;
    const char *ext = !strcmp(all[i], "xslt") ? "xml" : all[i];
    run_format(all[i], argv[1], ext);
  }

#if HAVE_EXTRA_BENCH
  {
    extern void bench_extra(const char *dir, const char *want,
                            void (*emit)(const char *, const char *, const char *, int, double),
                            void (*ser)(const char *, const char *, const char *));
    bench_extra(dir_cache, want, add_row_ext, serializer_ext);
  }
#endif

  FILE *out = fopen(argv[2], "w");
  if (!out) { perror("open out"); return 2; }
  fprintf(out, "platform:\n");
  fprintf(out, "  platform_string: %s-c-native\n", getenv("SERIALBENCH_PLATFORM") ? getenv("SERIALBENCH_PLATFORM") : "local");
  fprintf(out, "  kind: native\n  os: %s\n  arch: %s\n  runtime: c\n",
#ifdef __APPLE__
      "macos",
#elif defined(_WIN32)
      "windows",
#else
      "linux",
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
      "arm64"
#else
      "x86_64"
#endif
  );
  fprintf(out, "  runtime_version: %s\n", getenv("SERIALBENCH_C_VERSION") ? getenv("SERIALBENCH_C_VERSION") : "native");
  fprintf(out, "benchmark_config:\n  name: c-full\n  formats: [xml, html, json, yaml, toml]\n  operations: [parsing");
  fprintf(out, ", xslt]\n");
  fprintf(out, "serializers:\n");
#if HAVE_LEPTRIS
  fprintf(out, "    - {name: leptris, format: xml, version: '%s', features: {xpath: true, html5: true, xslt: true}}\n", leptris_version());
#endif
#if HAVE_LIBXML2
  fprintf(out, "    - {name: libxml2, format: xml, version: '%s', features: {xpath: true}}\n", LIBXML_DOTTED_VERSION);
#endif
#if HAVE_LIBXSLT
  fprintf(out, "    - {name: libxslt, format: xml, version: '%s', features: {xslt: true}}\n", LIBXSLT_DOTTED_VERSION);
#endif
#if HAVE_YEPTRIS
  fprintf(out, "    - {name: yeptris, format: json, version: '%s', features: {}}\n", yeptris_version());
  fprintf(out, "    - {name: yeptris, format: yaml, version: '%s', features: {}}\n", yeptris_version());
#endif
#if HAVE_JANSSON
  fprintf(out, "    - {name: jansson, format: json, version: '%s', features: {}}\n", JANSSON_VERSION);
#endif
#if HAVE_NLOHMANN
  fprintf(out, "    - {name: nlohmann-json, format: json, version: '3.x', features: {}}\n");
#endif
#if HAVE_RYML
  fprintf(out, "    - {name: rapidyaml, format: yaml, version: '0.7', features: {}}\n");
#endif
#if HAVE_TOMLC17
  fprintf(out, "    - {name: tomlc17, format: toml, version: '0.x', features: {}}\n");
#endif
#if HAVE_LIBYAML
  fprintf(out, "    - {name: libyaml, format: yaml, version: '%s', features: {}}\n", yaml_get_version_string());
#endif
#if HAVE_TEPTRIS
  fprintf(out, "    - {name: teptris, format: toml, version: '%s', features: {}}\n", teptris_version_string());
#endif
  fputs(ser_buf, out);
  fprintf(out, "benchmark_result:\n  parsing:\n");
  for (int i = 0; i < nrows; i++) {
    if (strcmp(rows[i].fmt, "xslt") == 0) continue;
    fprintf(out, "    - {adapter: %s, format: %s, data_size: %s, time_per_iteration: %.9f, iterations_per_second: %.6f}\n",
            rows[i].name, rows[i].fmt, rows[i].size, rows[i].tpi, rows[i].ips);
  }
  fprintf(out, "  xslt:\n");
  for (int i = 0; i < nrows; i++) {
    if (strcmp(rows[i].fmt, "xslt")) continue;
    fprintf(out, "    - {adapter: %s, format: xml, data_size: %s, time_per_iteration: %.9f, iterations_per_second: %.6f}\n",
            rows[i].name, rows[i].size, rows[i].tpi, rows[i].ips);
  }
  fclose(out);
  printf("rows: %d\n", nrows);
  return 0;
}
