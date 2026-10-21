# serialbench-c

The C/C++ harness of the cross-language serialbench family. Measures the
canonical fixtures (https://github.com/serialbench/fixtures) with the native
tris cores against the packaged C/C++ field, writing the shared data schema
(`runs/{date}/{platform}-c-{compiler}.{format}.yaml`).

## Field

| Format | Adapters |
|---|---|
| xml | leptris, libxml2 (+ xpath; generation) |
| html | leptris (html4), leptris-whatwg, libxml2 |
| xslt | leptris, libxslt (rows land in the xml file) |
| json | yeptris, jansson, json-c, nlohmann-json, simdjson |
| yaml | yeptris, libyaml, rapidyaml |
| toml | teptris, tomlc17 |
| cbor | yeptris, libcbor, tinycbor |

Dependencies are optional and compile out when absent (`HAVE_*` defines).
tris cores are located via `LEPTRIS_DIR`/`YEPTRIS_DIR`/`TEPTRIS_DIR` or
`-DTRIS_LIB_DIR`/`-DTRIS_INC_DIR`; tomlc17 via `TOMLC17_DIR`. json-c is
isolated in `src/bench_extra.c` (its `json_object` typedef collides with
jansson's); the C++ field (nlohmann-json, simdjson, pugixml, rapidyaml)
lives in `src/bench_cpp.cpp`.

## Build and run

```sh
cmake -B build && cmake --build build
git clone --depth 1 https://github.com/serialbench/fixtures.git fixtures
./build/serialbench-c fixtures results/xml/results.yaml [format]
```

Formats: `xml html json yaml toml cbor xslt`, or all available when
omitted. The xml invocation also runs the xslt leg.

## Correctness gate

`check` mode parses every fixture with every available adapter and compares
element counts within each size — adapters must agree or the run fails:

```sh
./build/serialbench-c fixtures /dev/null check
```

CI gates every platform on this before benchmarking.

## CI

`.github/workflows/benchmark.yml` runs weekly: gcc/clang on
ubuntu-24.04/-arm, ubuntu-22.04, macos-15, macos-26-intel, macos-26, plus a
windows-2022 UCRT64 leg that builds utf8proc and the tris cores from
source. Each format is an independent checkpoint — benchmark, artifact
upload, push to `serialbench/data` via the Contents API, then a
completeness check that fails the leg so autoheal fills the gaps.
