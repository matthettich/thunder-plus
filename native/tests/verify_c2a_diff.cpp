// verify-c2a: reads JSONL lines {"p": text|null, "n": text|null}; prints one line per case: KIND\t id name value ...
#include <cstdio>
#include <iostream>
#include <string>
#include "chains/cdiff.h"
#include "rt/json.h"
using namespace thunder;
int main() {
  std::string line;
  while (std::getline(std::cin, line)) {
    JsonDoc d; if (!d.parse(line)) { std::printf("BADLINE\n"); continue; }
    Json r = d.root(); JsonDoc pd, nd; Json p, n;
    bool pok = true, nok = true;
    if (r.get("p").isStr()) { pok = pd.parse(r.get("p").asStr()); if (pok) p = pd.root(); }
    if (r.get("n").isStr()) { nok = nd.parse(r.get("n").asStr()); if (nok) n = nd.root(); }
    if (!pok || !nok) { std::printf("PARSEFAIL\n"); continue; }
    ChainsDiff c = chains_diff(p, n);
    std::printf("%s", c.kind == ChainsDiffKind::Same ? "Same" : c.kind == ChainsDiffKind::Params ? "Params" : "Structural");
    for (auto& o : c.params) std::printf("\t%s\x01%s\x01%.17g", o.id.c_str(), o.name.c_str(), o.v);
    std::printf("\n");
  }
}
