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
#include <leptris/descriptor.h>
#endif
#if HAVE_YEPTRIS
#include <yeptris.h>
#include <yeptris/json.h>
#include <yeptris/parse.h>
#include <yeptris/plan.h>
#define YEPTRIS_WITH_CBOR
#include <yeptris/cbor.h>
#endif
#if HAVE_TEPTRIS
#include <teptris/teptris.h>
#include <teptris/schema.h>
#include <teptris/plan.h>
#endif
#if HAVE_LIBXML2
#include <libxml/parser.h>
#include <libxml/HTMLparser.h>
#include <libxml/HTMLtree.h>
#include <libxml/tree.h>
#include <libxml/c14n.h>
#include <libxml/xmlschemastypes.h>
#endif
#if HAVE_LIBXSLT
#include <libxslt/xslt.h>
#include <libxslt/transform.h>
#include <libxslt/xsltutils.h>
#endif
#if HAVE_JANSSON
#include <jansson.h>
#endif
#if HAVE_YYJSON
#include <yyjson.h>
#endif
#if HAVE_CJSON
#include <cJSON.h>
#endif
#if HAVE_EXPAT
#include <expat.h>
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
/* schema fixtures are named by document shape, not by size:
   small=config, medium=users, large=dataset */
static const char *schema_stem(const char *size) {
  if (!strcmp(size, "small")) return "config";
  if (!strcmp(size, "medium")) return "users";
  return "dataset";
}
int g_check_failed = 0;

#if HAVE_EXPAT
/* expat is SAX-only: count start elements as the parse work. The count
 * equals the DOM element count (expat fires StartElement for empty
 * elements too), so it participates in the xml count gate. */
static void XMLCALL expat_start_count(void *user, const XML_Char *name, const XML_Char **atts) {
  (void)name; (void)atts;
  (*(long *)user)++;
}
#endif

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
#if HAVE_EXPAT
        if (!strcmp(fmt, "xml")) {
          XML_Parser p = XML_ParserCreate(NULL);
          long n = -1;
          if (p) {
            long count = 0;
            XML_SetUserData(p, &count);
            XML_SetElementHandler(p, expat_start_count, NULL);
            n = XML_Parse(p, data, (int)len, 1) == XML_STATUS_OK ? count : -1;
            XML_ParserFree(p);
          }
          check_line(fmt, "expat", sizes[s], n > 0, n);
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
#if HAVE_YYJSON
        {
          yyjson_doc *doc = yyjson_read(data, len, 0);
          check_line(fmt, "yyjson", sizes[s], doc != NULL, -1);
          if (doc) yyjson_doc_free(doc);
        }
#endif
#if HAVE_CJSON
        {
          cJSON *c = cJSON_ParseWithLength(data, len);
          check_line(fmt, "cjson", sizes[s], c != NULL, -1);
          if (c) cJSON_Delete(c);
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
#if HAVE_EXPAT
      BENCH("expat", "xml", sizes[s], iters[s],, {
        XML_Parser p = XML_ParserCreate(NULL);
        if (p) {
          long count = 0;
          XML_SetUserData(p, &count);
          XML_SetElementHandler(p, expat_start_count, NULL);
          XML_Parse(p, data, (int)len, 1);
          XML_ParserFree(p);
        }
      },);
#endif

      /* validation: schemas are per-size (config/users/dataset.* match
         the canonical document shapes); compile is amortized outside
         the loop, parse+validate are the per-iteration work */
      g_cur_op = "validation";
#if HAVE_LEPTRIS
      {
        char spath[512];
        snprintf(spath, sizeof spath, "%s/%s.xsd", dir, schema_stem(sizes[s]));
        size_t slen; char *sdata = read_file(spath, &slen);
        if (sdata) {
          LeptrisStatus st = LEPTRIS_OK;
          LeptrisXsdSchema schema = leptris_xsd_compile(sdata, slen, &st);
          if (schema && st == LEPTRIS_OK) {
            BENCH("leptris-xsd", "xml", sizes[s], iters[s],, {
              LeptrisStatus vst = LEPTRIS_OK;
              LeptrisDocument d = leptris_parse_string(data, len, &vst);
              if (vst == LEPTRIS_OK) {
                if (leptris_xsd_validate(schema, d) != 1) g_check_failed = 1;
                leptris_document_free(d);
              }
            },);
          }
          if (schema) leptris_xsd_free(schema);
          free(sdata);
        }
        snprintf(spath, sizeof spath, "%s/%s.sch", dir, schema_stem(sizes[s]));
        sdata = read_file(spath, &slen);
        if (sdata) {
          LeptrisStatus st = LEPTRIS_OK;
          LeptrisSchematron sch = leptris_schematron_parse(sdata, slen, &st);
          if (sch && st == LEPTRIS_OK) {
            BENCH("leptris-schematron", "xml", sizes[s], iters[s],, {
              LeptrisStatus vst = LEPTRIS_OK;
              LeptrisDocument d = leptris_parse_string(data, len, &vst);
              if (vst == LEPTRIS_OK) {
                LeptrisDocument svrl = leptris_schematron_validate(sch, d);
                if (svrl) leptris_document_free(svrl);
                leptris_document_free(d);
              }
            },);
          }
          if (sch) leptris_schematron_free(sch);
          free(sdata);
        }
      }
#endif
#if HAVE_LIBXML2
      {
        char spath[512];
        snprintf(spath, sizeof spath, "%s/%s.xsd", dir, schema_stem(sizes[s]));
        xmlSchemaParserCtxtPtr pctx = xmlSchemaNewParserCtxt(spath);
        xmlSchemaPtr schema = pctx ? xmlSchemaParse(pctx) : NULL;
        if (schema) {
          BENCH("libxml2-xsd", "xml", sizes[s], iters[s],, {
            xmlDocPtr d = xmlReadMemory(data, (int)len, NULL, NULL, 0);
            if (d) {
              xmlSchemaValidCtxtPtr vctx = xmlSchemaNewValidCtxt(schema);
              if (vctx) {
                if (xmlSchemaValidateDoc(vctx, d) != 0) g_check_failed = 1;
                xmlSchemaFreeValidCtxt(vctx);
              }
              xmlFreeDoc(d);
            }
          },);
        }
        if (pctx) xmlSchemaFreeParserCtxt(pctx);
        if (schema) xmlSchemaFree(schema);
      }
#endif

      /* c14n: canonical XML serialization (leptris vs libxml2 parity) */
      g_cur_op = "c14n";
#if HAVE_LEPTRIS
      BENCH("leptris-c14n", "xml", sizes[s], iters[s],, {
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisDocument d = leptris_parse_string(data, len, &st);
        if (st == LEPTRIS_OK) {
          char *out = leptris_c14n_canonicalize(d, 0, 0);
          if (out) leptris_free_string(out);
          leptris_document_free(d);
        }
      },);
#endif
#if HAVE_LIBXML2
      BENCH("libxml2-c14n", "xml", sizes[s], iters[s],, {
        xmlDocPtr d = xmlReadMemory(data, (int)len, NULL, NULL, 0);
        if (d) {
          xmlChar *out = NULL;
          xmlC14NDocDumpMemory(d, NULL, 0, 0, 0, &out);
          if (out) xmlFree(out);
          xmlFreeDoc(d);
        }
      },);
#endif

      /* diff: tree-vs-tree structural diff. Both sides parse the same
         document — the op measures the full traversal+compare walk;
         no sibling competitor ships a DOM diff. */
      g_cur_op = "diff";
#if HAVE_LEPTRIS
      BENCH("leptris-diff", "xml", sizes[s], iters[s],, {
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisDocument a = leptris_parse_string(data, len, &st);
        LeptrisDocument b = st == LEPTRIS_OK ? leptris_parse_string(data, len, &st) : NULL;
        if (st == LEPTRIS_OK) {
          LeptrisDiff diff = leptris_diff(a, b, 0, &st);
          if (diff) leptris_diff_free(diff);
        }
        leptris_document_free(a);
        leptris_document_free(b);
      },);
#endif

      /* plan: descriptor materialization (leptris's XML query face —
         the tree-shaped plan compiles once outside the loop;
         parse+walk is the per-iteration work, mirroring the
         ruby/py faces. Dialect: a NESTED row applies its child
         plan to each match, so a wrapper element (records) needs
         its own plan whose rows bind the repeated children). */
      g_cur_op = "plan";
#if HAVE_LEPTRIS
      {
        /* small: config -> {database, cache} */
        static const leptris_child_plan cfg_root_children[] = {
            {.wire_name = "database", .kind = LEPTRIS_PLAN_KIND_NESTED, .child_plan_index = 1},
            {.wire_name = "cache", .kind = LEPTRIS_PLAN_KIND_NESTED, .child_plan_index = 2},
        };
        static const leptris_child_plan cfg_db_children[] = {
            {.wire_name = "host", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
            {.wire_name = "port", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
            {.wire_name = "name", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
            {.wire_name = "user", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
            {.wire_name = "password", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
        };
        static const leptris_child_plan cfg_cache_children[] = {
            {.wire_name = "enabled", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
            {.wire_name = "ttl", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
        };
        static const leptris_element_plan cfg_plans[] = {
            {.element_name = "config", .child_count = 2, .child_plans = cfg_root_children},
            {.element_name = "database", .child_count = 5, .child_plans = cfg_db_children},
            {.element_name = "cache", .child_count = 2, .child_plans = cfg_cache_children},
        };
        static const leptris_plan_spec cfg_spec = {
            LEPTRIS_PLAN_ABI_VERSION, 3, cfg_plans};

        /* medium: users -> user(id attr) -> profile -> preferences */
        static const leptris_child_plan usr_root_children[] = {
            {.wire_name = "user", .kind = LEPTRIS_PLAN_KIND_NESTED, .child_plan_index = 1},
        };
        static const leptris_attr_plan usr_user_attrs[] = {
            {.wire_name = "id", .kind = LEPTRIS_PLAN_KIND_SCALAR},
        };
        static const leptris_child_plan usr_user_children[] = {
            {.wire_name = "name", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
            {.wire_name = "email", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
            {.wire_name = "created_at", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
            {.wire_name = "profile", .kind = LEPTRIS_PLAN_KIND_NESTED, .child_plan_index = 2},
        };
        static const leptris_child_plan usr_profile_children[] = {
            {.wire_name = "age", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
            {.wire_name = "city", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
            {.wire_name = "preferences", .kind = LEPTRIS_PLAN_KIND_NESTED, .child_plan_index = 3},
        };
        static const leptris_child_plan usr_prefs_children[] = {
            {.wire_name = "theme", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
            {.wire_name = "notifications", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
        };
        static const leptris_element_plan usr_plans[] = {
            {.element_name = "users", .child_count = 1, .child_plans = usr_root_children},
            {.element_name = "user", .attribute_count = 1, .attribute_plans = usr_user_attrs,
             .child_count = 4, .child_plans = usr_user_children},
            {.element_name = "profile", .child_count = 3, .child_plans = usr_profile_children},
            {.element_name = "preferences", .child_count = 2, .child_plans = usr_prefs_children},
        };
        static const leptris_plan_spec usr_spec = {
            LEPTRIS_PLAN_ABI_VERSION, 4, usr_plans};

        /* large: dataset -> {header, records -> record(id attr)
           -> {timestamp, data, metadata}} */
        static const leptris_child_plan ds_root_children[] = {
            {.wire_name = "header", .kind = LEPTRIS_PLAN_KIND_NESTED, .child_plan_index = 1},
            {.wire_name = "records", .kind = LEPTRIS_PLAN_KIND_NESTED, .child_plan_index = 2},
        };
        static const leptris_child_plan ds_header_children[] = {
            {.wire_name = "created", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
            {.wire_name = "count", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
            {.wire_name = "format", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
        };
        static const leptris_child_plan ds_records_children[] = {
            {.wire_name = "record", .kind = LEPTRIS_PLAN_KIND_NESTED, .child_plan_index = 3},
        };
        static const leptris_attr_plan ds_record_attrs[] = {
            {.wire_name = "id", .kind = LEPTRIS_PLAN_KIND_SCALAR},
        };
        static const leptris_child_plan ds_record_children[] = {
            {.wire_name = "timestamp", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
            {.wire_name = "data", .kind = LEPTRIS_PLAN_KIND_NESTED, .child_plan_index = 4},
            {.wire_name = "metadata", .kind = LEPTRIS_PLAN_KIND_NESTED, .child_plan_index = 5},
        };
        static const leptris_child_plan ds_data_children[] = {
            {.wire_name = "field1", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
            {.wire_name = "field2", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
            {.wire_name = "field3", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
        };
        static const leptris_child_plan ds_meta_children[] = {
            {.wire_name = "source", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
            {.wire_name = "version", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
            {.wire_name = "checksum", .kind = LEPTRIS_PLAN_KIND_SCALAR, .child_plan_index = -1},
        };
        static const leptris_element_plan ds_plans[] = {
            {.element_name = "dataset", .child_count = 2, .child_plans = ds_root_children},
            {.element_name = "header", .child_count = 3, .child_plans = ds_header_children},
            {.element_name = "records", .child_count = 1, .child_plans = ds_records_children},
            {.element_name = "record", .attribute_count = 1, .attribute_plans = ds_record_attrs,
             .child_count = 3, .child_plans = ds_record_children},
            {.element_name = "data", .child_count = 3, .child_plans = ds_data_children},
            {.element_name = "metadata", .child_count = 3, .child_plans = ds_meta_children},
        };
        static const leptris_plan_spec ds_spec = {
            LEPTRIS_PLAN_ABI_VERSION, 6, ds_plans};

        const leptris_plan_spec *spec = !strcmp(sizes[s], "small") ? &cfg_spec
                                      : !strcmp(sizes[s], "medium") ? &usr_spec
                                                                    : &ds_spec;
        LeptrisStatus pst = LEPTRIS_OK;
        LeptrisPlan plan = leptris_plan_build(spec, &pst);
        if (plan && pst == LEPTRIS_OK) {
          BENCH("leptris-plan", "xml", sizes[s], iters[s],, {
            LeptrisStatus st = LEPTRIS_OK;
            LeptrisDocument d = leptris_parse_string(data, len, &st);
            if (st == LEPTRIS_OK) {
              LeptrisPlanResult r =
                  leptris_plan_walk(d, leptris_document_root(d), plan, &st);
              if (r) leptris_plan_result_free(r);
            }
            leptris_document_free(d);
          },);
        }
        if (plan) leptris_plan_free(plan);
      }
#endif
      g_cur_op = "parsing";
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
#if HAVE_YYJSON
      BENCH("yyjson", "json", sizes[s], iters[s],, {
        yyjson_doc *doc = yyjson_read(data, len, 0);
        if (doc) yyjson_doc_free(doc);
      },);
      g_cur_op = "generation";
      BENCH("yyjson", "json", sizes[s], iters[s],, {
        yyjson_doc *doc = yyjson_read(data, len, 0);
        if (doc) {
          size_t wn = 0;
          char *wout = yyjson_val_write(yyjson_doc_get_root(doc), 0, &wn);
          if (wout) free(wout);
          yyjson_doc_free(doc);
        }
      },);
      g_cur_op = "parsing";
#endif
#if HAVE_CJSON
      BENCH("cjson", "json", sizes[s], iters[s],, {
        cJSON *c = cJSON_ParseWithLength(data, len);
        if (c) cJSON_Delete(c);
      },);
      g_cur_op = "generation";
      BENCH("cjson", "json", sizes[s], iters[s],, {
        cJSON *c = cJSON_ParseWithLength(data, len);
        if (c) {
          char *cout = cJSON_PrintUnformatted(c);
          if (cout) free(cout);
          cJSON_Delete(c);
        }
      },);
      g_cur_op = "parsing";
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

#if HAVE_YEPTRIS
      g_cur_op = "validation";
      {
        char spath[512];
        snprintf(spath, sizeof spath, "%s/%s.schema.json", dir, schema_stem(sizes[s]));
        size_t slen; char *sdata = read_file(spath, &slen);
        if (sdata) {
          yeptris_schema *schema = yeptris_schema_compile(sdata, slen);
          if (schema) {
            BENCH("yeptris-schema", "json", sizes[s], iters[s],, {
              yeptris_validate_report *rep = NULL;
              if (yeptris_schema_validate(schema, data, len, &rep) == YEPTRIS_OK) {
                /* 0 diagnostics = valid; a fixture that stops
                   validating is a schema/fixture contract break */
                if (rep && yeptris_validate_report_count(rep) > 0) g_check_failed = 1;
                yeptris_validate_report_free(rep);
              } else {
                g_check_failed = 1;
              }
            },);
          }
          if (schema) yeptris_schema_free(schema);
          free(sdata);
        }
      }

      /* plan: compiled query plan (yeptris's query face — leaves only,
         segmented path; spec grammar per yeptris/plan.h. compile once
         outside the loop, parse+walk is the per-iteration work) */
      g_cur_op = "plan";
      {
        static const char *small_spec =
          "{\"kind\":\"map\",\"path\":[\"config\",\"database\"],\"children\":["
          "{\"name\":\"host\",\"kind\":\"str\"},{\"name\":\"port\",\"kind\":\"int\"},"
          "{\"name\":\"name\",\"kind\":\"str\"},{\"name\":\"user\",\"kind\":\"str\"},"
          "{\"name\":\"password\",\"kind\":\"str\"}]}";
        static const char *medium_spec =
          "{\"kind\":\"seq\",\"path\":[\"users\"],\"children\":["
          "{\"name\":\"id\",\"kind\":\"int\"},{\"name\":\"name\",\"kind\":\"str\"},"
          "{\"name\":\"email\",\"kind\":\"str\"},{\"name\":\"created_at\",\"kind\":\"str\"}]}";
        static const char *large_spec =
          "{\"kind\":\"map\",\"path\":[\"dataset\",\"records\"],\"children\":["
          "{\"name\":\"id\",\"kind\":\"int\"},{\"name\":\"timestamp\",\"kind\":\"str\"}]}";
        const char *spec = !strcmp(sizes[s], "small") ? small_spec
                         : !strcmp(sizes[s], "medium") ? medium_spec : large_spec;
        size_t spec_len = strlen(spec);
        YeptrisStatus st = 0;
        yeptris_plan *plan = yeptris_plan_compile(spec, spec_len, &st);
        if (plan && st == 0) {
          BENCH("yeptris-plan", "json", sizes[s], iters[s],, {
            YeptrisStatus vst = 0;
            YeptrisDocument d = yeptris_parse_json(data, len, &vst);
            if (vst == 0) {
              yeptris_plan_result *r = yeptris_document_plan_walk(d, plan, &vst);
              if (r) yeptris_plan_result_free(r);
              yeptris_document_free(d);
            }
          },);
        }
        if (plan) yeptris_plan_free(plan);
      }
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

      /* plan: compiled query plan over the yaml fixtures (same core
         walk the json face uses; the canonical yaml keys are symbol-
         style, so wire names and path segments carry the leading
         colon — mirroring the js/py faces). compile once outside the
         loop, parse+walk is the per-iteration work. */
      g_cur_op = "plan";
      {
        static const char *small_spec =
          "{\"kind\":\"map\",\"path\":[\":config\",\":database\"],\"children\":["
          "{\"name\":\":host\",\"kind\":\"str\"},{\"name\":\":port\",\"kind\":\"int\"},"
          "{\"name\":\":name\",\"kind\":\"str\"},{\"name\":\":user\",\"kind\":\"str\"},"
          "{\"name\":\":password\",\"kind\":\"str\"}]}";
        static const char *medium_spec =
          "{\"kind\":\"seq\",\"path\":[\":users\"],\"children\":["
          "{\"name\":\":id\",\"kind\":\"int\"},{\"name\":\":name\",\"kind\":\"str\"},"
          "{\"name\":\":email\",\"kind\":\"str\"},{\"name\":\":created_at\",\"kind\":\"str\"}]}";
        static const char *large_spec =
          "{\"kind\":\"map\",\"path\":[\":dataset\",\":records\"],\"children\":["
          "{\"name\":\":id\",\"kind\":\"int\"},{\"name\":\":timestamp\",\"kind\":\"str\"}]}";
        const char *spec = !strcmp(sizes[s], "small") ? small_spec
                         : !strcmp(sizes[s], "medium") ? medium_spec : large_spec;
        size_t spec_len = strlen(spec);
        YeptrisStatus st = 0;
        yeptris_plan *plan = yeptris_plan_compile(spec, spec_len, &st);
        if (plan && st == 0) {
          BENCH("yeptris-plan", "yaml", sizes[s], iters[s],, {
            YeptrisStatus vst = 0;
            YeptrisDocument d = yeptris_parse(data, len, &vst);
            if (vst == 0) {
              yeptris_plan_result *r = yeptris_document_plan_walk(d, plan, &vst);
              if (r) yeptris_plan_result_free(r);
              yeptris_document_free(d);
            }
          },);
        }
        if (plan) yeptris_plan_free(plan);
      }
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

#if HAVE_TEPTRIS
      g_cur_op = "validation";
      {
        char spath[512];
        snprintf(spath, sizeof spath, "%s/%s.schema.json", dir, schema_stem(sizes[s]));
        size_t slen; char *sdata = read_file(spath, &slen);
        if (sdata) {
          teptris_schema *schema = NULL; char *diag = NULL; size_t diag_len = 0;
          if (teptris_schema_compile(sdata, slen, &schema, &diag, &diag_len) == TEPTRIS_OK && schema) {
            BENCH("teptris-schema", "toml", sizes[s], iters[s],, {
              teptris_document *d = NULL;
              if (teptris_parse(data, len, NULL, &d) == TEPTRIS_OK && d) {
                char *errs = NULL; size_t errs_len = 0;
                if (teptris_schema_validate_doc(schema, d, &errs, &errs_len) == TEPTRIS_OK) {
                  /* "[]" = valid; anything collected is a contract break */
                  if (errs && errs_len > 2) g_check_failed = 1;
                  free(errs);
                } else {
                  g_check_failed = 1;
                }
              }
              teptris_document_free(d);
            },);
          }
          if (diag) teptris_diag_free(diag);
          if (schema) teptris_schema_free(schema);
          free(sdata);
        }
      }

      /* plan: compiled query plan over the document (teptris's query
         face; flattened plan tree per plan.h — build once outside the
         loop, parse+walk is the per-iteration work) */
      g_cur_op = "plan";
      {
        static const teptris_plan_row cfg_rows[] = {
            {"config", TEPTRIS_PLAN_NESTED, 1},
            {"database", TEPTRIS_PLAN_NESTED, 2}, {"cache", TEPTRIS_PLAN_NESTED, 3},
            {"host", TEPTRIS_PLAN_SCALAR, 0}, {"port", TEPTRIS_PLAN_SCALAR, 0},
            {"name", TEPTRIS_PLAN_SCALAR, 0}, {"user", TEPTRIS_PLAN_SCALAR, 0},
            {"password", TEPTRIS_PLAN_SCALAR, 0},
            {"enabled", TEPTRIS_PLAN_SCALAR, 0}, {"ttl", TEPTRIS_PLAN_SCALAR, 0}};
        static const uint32_t cfg_first[] = {0, 1, 3, 8, 10};
        static const teptris_plan_row usr_rows[] = {
            {"users", TEPTRIS_PLAN_COLLECTION, 1},
            {"id", TEPTRIS_PLAN_SCALAR, 0}, {"name", TEPTRIS_PLAN_SCALAR, 0},
            {"email", TEPTRIS_PLAN_SCALAR, 0}, {"created_at", TEPTRIS_PLAN_SCALAR, 0},
            {"profile", TEPTRIS_PLAN_NESTED, 2},
            {"age", TEPTRIS_PLAN_SCALAR, 0}, {"city", TEPTRIS_PLAN_SCALAR, 0},
            {"preferences", TEPTRIS_PLAN_NESTED, 3},
            {"theme", TEPTRIS_PLAN_SCALAR, 0}, {"notifications", TEPTRIS_PLAN_SCALAR, 0}};
        static const uint32_t usr_first[] = {0, 1, 6, 9, 11};
        static const teptris_plan_row ds_rows[] = {
            {"dataset", TEPTRIS_PLAN_NESTED, 1},
            {"header", TEPTRIS_PLAN_NESTED, 2}, {"records", TEPTRIS_PLAN_COLLECTION, 3},
            {"created", TEPTRIS_PLAN_SCALAR, 0}, {"count", TEPTRIS_PLAN_SCALAR, 0},
            {"format", TEPTRIS_PLAN_SCALAR, 0},
            {"id", TEPTRIS_PLAN_SCALAR, 0}, {"timestamp", TEPTRIS_PLAN_SCALAR, 0},
            {"data", TEPTRIS_PLAN_NESTED, 4}, {"metadata", TEPTRIS_PLAN_NESTED, 5},
            {"field1", TEPTRIS_PLAN_SCALAR, 0}, {"field2", TEPTRIS_PLAN_SCALAR, 0},
            {"field3", TEPTRIS_PLAN_SCALAR, 0}, {"nested", TEPTRIS_PLAN_RAW, 0},
            {"source", TEPTRIS_PLAN_SCALAR, 0}, {"version", TEPTRIS_PLAN_SCALAR, 0},
            {"checksum", TEPTRIS_PLAN_SCALAR, 0}};
        static const uint32_t ds_first[] = {0, 1, 3, 6, 10, 14, 17};
        teptris_plan_spec spec;
        if (!strcmp(sizes[s], "small")) {
          spec = (teptris_plan_spec){TEPTRIS_PLAN_ABI_VERSION, 4, cfg_rows, cfg_first};
        } else if (!strcmp(sizes[s], "medium")) {
          spec = (teptris_plan_spec){TEPTRIS_PLAN_ABI_VERSION, 4, usr_rows, usr_first};
        } else {
          spec = (teptris_plan_spec){TEPTRIS_PLAN_ABI_VERSION, 6, ds_rows, ds_first};
        }
        teptris_status pst = TEPTRIS_OK;
        teptris_plan *plan = teptris_plan_build(&spec, &pst);
        if (plan && pst == TEPTRIS_OK) {
          BENCH("teptris-plan", "toml", sizes[s], iters[s],, {
            teptris_document *d = NULL;
            if (teptris_parse(data, len, NULL, &d) == TEPTRIS_OK && d) {
              teptris_status wst;
              teptris_plan_result *r = teptris_plan_walk(plan, teptris_document_root(d), &wst);
              if (r) teptris_plan_result_free(r);
            }
            teptris_document_free(d);
          },);
        }
        if (plan) teptris_plan_free(plan);
      }
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
  fprintf(out, ", generation, xpath, validation, c14n, diff, plan, xslt]\n");
  fprintf(out, "serializers:\n");
#if HAVE_LEPTRIS
  fprintf(out, "    - {name: leptris, format: xml, version: '%s', features: {xpath: true, html5: true, xslt: true}}\n", leptris_version());
#if HAVE_YYJSON
  /* yyjson_version() returns a packed uint32 (major<<16|minor<<8|patch),
     not a string — %s on it is a wild-pointer dereference */
  unsigned yyjson_v = yyjson_version();
  fprintf(out, "    - {name: yyjson, format: json, version: '%u.%u.%u', features: {}}\n",
          yyjson_v >> 16, (yyjson_v >> 8) & 0xffu, yyjson_v & 0xffu);
#endif
#if HAVE_CJSON
  fprintf(out, "    - {name: cjson, format: json, version: '%s', features: {}}\n", cJSON_Version());
#endif
#if HAVE_EXPAT
  fprintf(out, "    - {name: expat, format: xml, version: '%s', features: {}}\n", XML_ExpatVersion());
#endif
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
  static const char *sections[] = {"parsing", "generation", "xpath", "validation", "c14n", "diff", "plan"};
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
