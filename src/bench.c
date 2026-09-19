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
#define YEPTRIS_WITH_CBOR
#include <yeptris/cbor.h>
#endif
#if HAVE_TEPTRIS
#include <teptris/teptris.h>
#endif
#if HAVE_LIBXML2
#include <libxml/parser.h>
#include <libxml/HTMLparser.h>
#include <libxml/HTMLtree.h>
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
#if HAVE_LIBCBOR
#include <cbor.h>
#endif

typedef struct { const char *name; const char *fmt; const char *size; const char *op; double tpi; double ips; } row_t;
static row_t rows[256];
static int nrows = 0;
const char *g_cur_op = "parsing";
static const char *g_format = NULL;
static const char *dir_cache = NULL;
static void add_row(const char *name, const char *fmt, const char *size, int iters, double elapsed);
void serializer_ext(const char *name, const char *fmt, const char *version);
void add_row_ext(const char *name, const char *fmt, const char *size, int iters, double elapsed);
static void add_row(const char *name, const char *fmt, const char *size, int iters, double elapsed) {
  if (nrows >= 256) return;
  rows[nrows].name = name; rows[nrows].fmt = fmt; rows[nrows].size = size;
  rows[nrows].op = g_cur_op;
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

static const char *XPATH_QUERIES[] = {"//user | //record", "//user[@id='101']", "//preferences/theme"};
int g_check_failed = 0;

static void check_line(const char *fmt, const char *adapter, const char *size, int ok, long count) {
  if (ok) printf("CHECK %s %s %s count=%ld OK\n", fmt, adapter, size, count);
  else { printf("CHECK %s %s %s FAIL\n", fmt, adapter, size); g_check_failed = 1; }
}

void check_all(const char *dir) {
  static const char *sizes[] = {"small", "medium", "large"};
  static const char *fmts[] = {"xml", "html", "json", "yaml", "toml", "cbor"};
  char path[512];
  for (unsigned f = 0; f < sizeof(fmts)/sizeof(fmts[0]); f++) {
    const char *fmt = fmts[f];
    for (int s = 0; s < 3; s++) {
      snprintf(path, sizeof path, "%s/%s.%s", dir, sizes[s], fmt);
      size_t len; char *data = read_file(path, &len);
      if (!data) { fprintf(stderr, "missing %s\n", path); g_check_failed = 1; continue; }

      if (!strcmp(fmt, "xml") || !strcmp(fmt, "html")) {
#if HAVE_LEPTRIS
        {
          LeptrisStatus st = LEPTRIS_OK;
          LeptrisDocument d = !strcmp(fmt, "xml")
            ? leptris_parse_string(data, len, &st)
            : leptris_parse_html4_string(data, len, &st);
          long n = -1;
          if (st == LEPTRIS_OK) {
            LeptrisXPathResult r = leptris_xpath_eval(d, NULL, "//*");
            n = (long)leptris_xpath_result_count(r);
            leptris_xpath_result_free(r);
          }
          check_line(fmt, "leptris", sizes[s], st == LEPTRIS_OK && n > 0, n);
          leptris_document_free(d);
        }
#endif
#if HAVE_LIBXML2
        {
          xmlDocPtr d = !strcmp(fmt, "xml")
            ? xmlReadMemory(data, (int)len, NULL, NULL, XML_PARSE_NOBLANKS)
            : htmlReadMemory(data, (int)len, NULL, NULL, HTML_PARSE_NOBLANKS | HTML_PARSE_RECOVER);
          long n = -1;
          if (d) {
            xmlXPathContextPtr ctx = xmlXPathNewContext(d);
            xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar *)"//*", ctx);
            n = o && o->nodesetval ? o->nodesetval->nodeNr : 0;
            if (o) xmlXPathFreeObject(o);
            xmlXPathFreeContext(ctx);
          }
          check_line(fmt, "libxml2", sizes[s], d && n > 0, n);
          if (d) xmlFreeDoc(d);
        }
#endif
      }

      if (!strcmp(fmt, "json")) {
#if HAVE_YEPTRIS
        {
          YeptrisStatus st = 0;
          YeptrisDocument d = yeptris_parse_json(data, len, &st);
          check_line(fmt, "yeptris", sizes[s], st == 0, st == 0 ? (long)yeptris_document_count(d) : -1);
          yeptris_document_free(d);
        }
#endif
#if HAVE_JANSSON
        {
          json_error_t err;
          json_t *r = json_loadb(data, len, 0, &err);
          /* no recursive count: jansson and json-c both export
           * json_object_iter_next and the linker misbinds the
           * reference — iterators are off-limits with both loaded */
          check_line(fmt, "jansson", sizes[s], r != NULL, -1);
          if (r) json_decref(r);
        }
#endif
      }

      if (!strcmp(fmt, "yaml")) {
#if HAVE_YEPTRIS
        {
          YeptrisStatus st = 0;
          YeptrisDocument d = yeptris_parse(data, len, &st);
          check_line(fmt, "yeptris", sizes[s], st == 0, st == 0 ? (long)yeptris_document_count(d) : -1);
          yeptris_document_free(d);
        }
#endif
#if HAVE_LIBYAML
        {
          yaml_parser_t parser;
          long events = 0;
          int ok = 0;
          if (yaml_parser_initialize(&parser)) {
            yaml_event_t event;
            yaml_parser_set_input_string(&parser, (unsigned char *)data, len);
            while (yaml_parser_parse(&parser, &event)) {
              events++;
              int done = (event.type == YAML_STREAM_END_EVENT);
              yaml_event_delete(&event);
              if (done) { ok = 1; break; }
            }
            yaml_parser_delete(&parser);
          }
          check_line(fmt, "libyaml", sizes[s], ok, events);
        }
#endif
      }

      if (!strcmp(fmt, "toml")) {
#if HAVE_TEPTRIS
        {
          teptris_document *d = NULL;
          teptris_status st = teptris_parse(data, len, NULL, &d);
          check_line(fmt, "teptris", sizes[s], st == TEPTRIS_OK, -1);
          teptris_document_free(d);
        }
#endif
#if HAVE_TOMLC17
        {
          toml_result_t r = toml_parse(data, (int)len);
          check_line(fmt, "tomlc17", sizes[s], r.ok != 0, -1);
          toml_free(r);
        }
#endif
      }

      if (!strcmp(fmt, "cbor")) {
#if HAVE_YEPTRIS
        {
          YeptrisStatus st = 0;
          YeptrisDocument d = yeptris_cbor_decode(data, len, 0, &st);
          check_line(fmt, "yeptris", sizes[s], st == 0, st == 0 ? (long)yeptris_document_count(d) : -1);
          yeptris_document_free(d);
        }
#endif
#if HAVE_LIBCBOR
        {
          struct cbor_load_result res;
          cbor_item_t *item = cbor_load((const unsigned char *)data, len, &res);
          check_line(fmt, "libcbor", sizes[s], item != NULL, -1);
          if (item) cbor_decref(&item);
        }
#endif
      }
      free(data);
    }
  }
  {
    extern void bench_jsonc_check(const char *);
    extern void bench_tinycbor_check(const char *);
    extern void bench_cpp_check(const char *);
    bench_jsonc_check(dir);
    bench_tinycbor_check(dir);
    bench_cpp_check(dir);
  }
  printf("CHECK %s\n", g_check_failed ? "FAILED" : "ALL OK");
}

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

#if HAVE_LEPTRIS
      g_cur_op = "generation";
      BENCH("leptris", "xml", sizes[s], iters[s],, {
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisDocument d = leptris_parse_string(data, len, &st);
        if (st == LEPTRIS_OK) {
          char *out = leptris_document_serialize(d, NULL);
          if (out) leptris_free_string(out);
        }
        leptris_document_free(d);
      },);
      g_cur_op = "xpath";
      BENCH("leptris", "xml", sizes[s], iters[s],, {
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisDocument d = leptris_parse_string(data, len, &st);
        if (st == LEPTRIS_OK) {
          for (unsigned q = 0; q < 3; q++) {
            LeptrisXPathResult r = leptris_xpath_eval(d, NULL, XPATH_QUERIES[q]);
            leptris_xpath_result_free(r);
          }
        }
        leptris_document_free(d);
      },);
      g_cur_op = "parsing";
#endif

#if HAVE_LIBXML2
      g_cur_op = "generation";
      BENCH("libxml2", "xml", sizes[s], iters[s],, {
        xmlDocPtr d = xmlReadMemory(data, (int)len, NULL, NULL, XML_PARSE_NOBLANKS);
        if (d) {
          xmlChar *out; int size;
          xmlDocDumpMemory(d, &out, &size);
          if (out) xmlFree(out);
        }
        xmlFreeDoc(d);
      },);
      g_cur_op = "xpath";
      BENCH("libxml2", "xml", sizes[s], iters[s],, {
        xmlDocPtr d = xmlReadMemory(data, (int)len, NULL, NULL, XML_PARSE_NOBLANKS);
        if (d) {
          xmlXPathContextPtr ctx = xmlXPathNewContext(d);
          if (ctx) {
            for (unsigned q = 0; q < 3; q++) {
              xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar *)XPATH_QUERIES[q], ctx);
              if (o) xmlXPathFreeObject(o);
            }
            xmlXPathFreeContext(ctx);
          }
        }
        xmlFreeDoc(d);
      },);
      g_cur_op = "parsing";
#endif
    }

    if (!strcmp(fmt, "html")) {
#if HAVE_LEPTRIS
      /* html4 = libxml2-parity engine (what libxml2 benchmarks against);
       * the WHATWG-conformant engine is priced separately */
      BENCH("leptris", "html", sizes[s], iters[s],, {
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisDocument d = leptris_parse_html4_string(data, len, &st);
        if (st == LEPTRIS_OK) leptris_document_free(d);
      },);
      BENCH("leptris-whatwg", "html", sizes[s], iters[s],, {
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisDocument d = leptris_parse_html_string(data, len, &st);
        if (st == LEPTRIS_OK) leptris_document_free(d);
      },);
      g_cur_op = "generation";
      BENCH("leptris", "html", sizes[s], iters[s],, {
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisDocument d = leptris_parse_html4_string(data, len, &st);
        if (st == LEPTRIS_OK) {
          char *out = leptris_document_serialize(d, NULL);
          if (out) leptris_free_string(out);
        }
        leptris_document_free(d);
      },);
      g_cur_op = "parsing";
#endif
#if HAVE_LIBXML2
      BENCH("libxml2", "html", sizes[s], iters[s],, {
        htmlDocPtr d = htmlReadMemory(data, (int)len, NULL, NULL, HTML_PARSE_NOBLANKS | HTML_PARSE_RECOVER);
        if (d) xmlFreeDoc(d);
      },);
      g_cur_op = "generation";
      BENCH("libxml2", "html", sizes[s], iters[s],, {
        htmlDocPtr d = htmlReadMemory(data, (int)len, NULL, NULL, HTML_PARSE_NOBLANKS | HTML_PARSE_RECOVER);
        if (d) {
          xmlChar *out; int size;
          htmlDocDumpMemory(d, &out, &size);
          if (out) xmlFree(out);
        }
        xmlFreeDoc(d);
      },);
      g_cur_op = "parsing";
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

#if HAVE_YEPTRIS
      g_cur_op = "generation";
      BENCH("yeptris", "json", sizes[s], iters[s],, {
        YeptrisStatus st = 0;
        YeptrisDocument d = yeptris_parse_json(data, len, &st);
        if (st == 0) {
          size_t out_len = 0;
          char *out = yeptris_serialize_json(d, &out_len);
          if (out) yeptris_free(out);
        }
        yeptris_document_free(d);
      },);
      g_cur_op = "parsing";
#endif

#if HAVE_JANSSON
      g_cur_op = "generation";
      BENCH("jansson", "json", sizes[s], iters[s],, {
        json_error_t err;
        json_t *r = json_loadb(data, len, 0, &err);
        if (r) {
          char *out = json_dumps(r, JSON_COMPACT);
          if (out) free(out);
        }
        json_decref(r);
      },);
      g_cur_op = "parsing";
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

#if HAVE_YEPTRIS
      g_cur_op = "generation";
      BENCH("yeptris", "yaml", sizes[s], iters[s],, {
        YeptrisStatus st = 0;
        YeptrisDocument d = yeptris_parse(data, len, &st);
        if (st == 0) {
          size_t out_len = 0;
          char *out = yeptris_serialize(d, &out_len);
          if (out) yeptris_free(out);
        }
        yeptris_document_free(d);
      },);
      g_cur_op = "parsing";
#endif

#if HAVE_LIBYAML
      g_cur_op = "generation";
      BENCH("libyaml", "yaml", sizes[s], iters[s],, {
        yaml_parser_t parser;
        yaml_emitter_t emitter;
        if (yaml_parser_initialize(&parser) && yaml_emitter_initialize(&emitter)) {
          size_t cap = len * 8;
          unsigned char *buf = malloc(cap);
          size_t written = 0;
          yaml_parser_set_input_string(&parser, (unsigned char *)data, len);
          yaml_emitter_set_output_string(&emitter, buf, cap, &written);
          yaml_event_t event;
          int done = 0;
          while (!done && yaml_parser_parse(&parser, &event)) {
            done = (event.type == YAML_STREAM_END_EVENT);
            if (!yaml_emitter_emit(&emitter, &event)) break;
          }
          yaml_emitter_delete(&emitter);
          yaml_parser_delete(&parser);
          free(buf);
        }
      },);
      g_cur_op = "parsing";
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

#if HAVE_TEPTRIS
      g_cur_op = "generation";
      BENCH("teptris", "toml", sizes[s], iters[s],, {
        teptris_document *d = NULL;
        if (teptris_parse(data, len, NULL, &d) == TEPTRIS_OK) {
          char *buf = NULL; size_t out_len = 0;
          if (teptris_document_emit(d, &buf, &out_len) == TEPTRIS_OK && buf) free(buf);
        }
        teptris_document_free(d);
      },);
      g_cur_op = "parsing";
#endif
    }

    if (!strcmp(fmt, "cbor")) {
#if HAVE_YEPTRIS
      BENCH("yeptris", "cbor", sizes[s], iters[s],, {
        YeptrisStatus st = 0;
        YeptrisDocument d = yeptris_cbor_decode(data, len, 0, &st);
        if (st == 0) yeptris_document_free(d);
      },);
#endif
#if HAVE_LIBCBOR
      BENCH("libcbor", "cbor", sizes[s], iters[s],, {
        struct cbor_load_result res;
        cbor_item_t *item = cbor_load((const unsigned char *)data, len, &res);
        if (item) cbor_decref(&item);
      },);
#endif

#if HAVE_YEPTRIS
      g_cur_op = "generation";
      BENCH("yeptris", "cbor", sizes[s], iters[s],, {
        YeptrisStatus st = 0;
        YeptrisDocument d = yeptris_cbor_decode(data, len, 0, &st);
        if (st == 0) {
          size_t out_len = 0;
          char *out = yeptris_cbor_encode(d, 0, &out_len);
          if (out) yeptris_free(out);
        }
        yeptris_document_free(d);
      },);
      g_cur_op = "parsing";
#endif

#if HAVE_LIBCBOR
      g_cur_op = "generation";
      BENCH("libcbor", "cbor", sizes[s], iters[s],, {
        struct cbor_load_result res;
        cbor_item_t *item = cbor_load((const unsigned char *)data, len, &res);
        if (item) {
          unsigned char *buf = NULL; size_t out_len = 0;
          cbor_serialize_alloc(item, &buf, &out_len);
          if (buf) free(buf);
          cbor_decref(&item);
        }
      },);
      g_cur_op = "parsing";
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

void check_all(const char *dir);

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: serialbench-c <fixtures> <out-yaml> [format|check]\n"); return 2; }
  if (argc > 3 && !strcmp(argv[3], "check")) {
    check_all(argv[1]);
    return g_check_failed ? 1 : 0;
  }
#if HAVE_LIBXML2
  LIBXML_TEST_VERSION
#endif
  const char *all[] = {"xml", "html", "json", "yaml", "toml", "cbor", "xslt"};
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
#if HAVE_LIBCBOR
  fprintf(out, "    - {name: libcbor, format: cbor, version: '0.14', features: {}}\n");
#endif
#if HAVE_YEPTRIS
  fprintf(out, "    - {name: yeptris, format: cbor, version: '%s', features: {}}\n", yeptris_version());
#endif
  fputs(ser_buf, out);
  static const char *sections[] = {"parsing", "generation", "xpath"};
  fprintf(out, "benchmark_result:\n");
  for (unsigned sec = 0; sec < sizeof(sections)/sizeof(sections[0]); sec++) {
    fprintf(out, "  %s:\n", sections[sec]);
    for (int i = 0; i < nrows; i++) {
      if (strcmp(rows[i].fmt, "xslt") == 0) continue;
      if (strcmp(rows[i].op, sections[sec])) continue;
      fprintf(out, "    - {adapter: %s, format: %s, data_size: %s, time_per_iteration: %.9f, iterations_per_second: %.6f}\n",
              rows[i].name, rows[i].fmt, rows[i].size, rows[i].tpi, rows[i].ips);
    }
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
