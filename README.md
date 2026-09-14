# serialbench-c

The C harness of the cross-language serialbench family. Scaffold stage:
benchmarks canonical-fixture XML parsing with libxml2 (and leptris when
`LEPTRIS_DIR`/`-DLEPTRIS_LIB` points at `libleptris`), writing the shared
data schema (`runs/{date}/{platform}-c-{compiler}.xml.yaml`).

Roadmap: generation + xpath + streaming ops; the yeptris/teptris cores;
competitor field (expat, pugixml, simdjson via wrappers); a compiler
matrix (gcc/clang at -O2, MSVC on Windows).

```sh
cmake -B build && cmake --build build
git clone --depth 1 https://github.com/serialbench/fixtures.git fixtures
./build/serialbench-c fixtures results/xml/results.yaml
```
