// WEB-FOLLOW item 2 (docs/port/WEB-FOLLOW-20NOV-NOTES.md): envelope times in lines (web #259). eng_env_lines_fit / eng_layer_rel_ms / eng_voice_grel_ms against the web's OWN envLinesFit / envMs
// (tests/golden/envlines-web.json, made by tools/envlines-dump.mjs from the pinned beta over 160 generated voices at eleven tempo / LPB pairs, including bpm 0 / NaN and a fractional LPB): every double equal.
// Then the audio side: a voice in lines renders to exactly the voice in ms that the JS's numbers describe (same length, same samples), and an old voice (no unit field) is untouched.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "engine/engine_json.h"
#include "rt/json.h"

using namespace thunder;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)
static bool readFile(const std::string& p, std::string* o) { std::ifstream f(p, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *o = ss.str(); return true; }
static bool same(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0 || (a != a && b != b) || (a == 0 && b == 0); }

static std::vector<float> render(const EngVoice& V, double hold, double bpm) {
  EngRenderOpts o; o.sr = 48000; o.hold = hold; o.bpm = bpm;
  const size_t bound = eng_render_bound(V, o); std::vector<float> buf(bound + 8, 0.f); size_t n = 0;
  const int st = eng_render_voice(V, o, buf.data(), bound, &n); if (st != kEngOk) return {};
  buf.resize(n); return buf;
}

int main(int argc, char** argv) {
  const std::string root = argc > 1 ? argv[1] : "."; std::string txt;
  if (!readFile(root + "/native/tests/golden/envlines-web.json", &txt)) { std::printf("cannot read the golden\n"); return 1; }
  JsonDoc doc; if (!doc.parse(txt)) { std::printf("bad json\n"); return 1; }
  const Json cases = doc.root().arr("cases"); int nFit = 0, nRender = 0;
  for (size_t ci = 0; ci < cases.size(); ci++) {
    const Json c = cases.at(ci), out = c.obj("out"); const Json bj = c.get("bpm");
    const double bpm = bj.isNull() ? (double)NAN : bj.asNum(), lpb = c.num("lpb"); double hold = c.num("hold");
    EngVoice V; const Json vj = c.obj("voice"); const Json none;
    if (!eng_voice_from_json(V, vj, vj.arr("layers"), none)) { CHECK(false, "case %zu: voice refused", ci); continue; }
    const EngVoice V0 = V;
    // the notes' release (relOf): before the fit, from the saved values
    const Json rels = out.arr("rels");
    for (size_t i = 0; i < rels.size(); i++) CHECK(same(eng_layer_rel_ms(V0.layers[i], bpm, lpb), rels.at(i).asNum()), "case %zu layer %zu: rel %.17g, JS %.17g", ci, i, eng_layer_rel_ms(V0.layers[i], bpm, lpb), rels.at(i).asNum());
    CHECK(same(eng_voice_grel_ms(V0, bpm, lpb), out.num("grel")), "case %zu: gRel %.17g, JS %.17g", ci, eng_voice_grel_ms(V0, bpm, lpb), out.num("grel"));
    const bool fit = eng_env_lines_fit(V, &hold, bpm, lpb);
    CHECK(fit == out.boolean("fit"), "case %zu: fit %d, JS %d", ci, (int)fit, (int)out.boolean("fit"));
    if (!fit) { bool layersSame = true;  for (size_t i = 0; i < 8; i++) { const EngLayer &a = V.layers[i], &b = V0.layers[i]; if (!(same(a.atk, b.atk) && same(a.hold, b.hold) && same(a.dec, b.dec) && same(a.rel, b.rel) && same(a.fAtk, b.fAtk) && same(a.fDec, b.fDec))) layersSame = false; } 
      CHECK(same(V.gAtk, V0.gAtk) && same(V.gHold, V0.gHold) && same(V.gDec, V0.gDec) && same(V.gRel, V0.gRel) && layersSame && same(V.len, V0.len) && same(hold, c.num("hold")), "case %zu: a voice without lines was changed", ci); continue; }
    nFit++;
    const Json L = out.arr("layers");
    for (size_t i = 0; i < L.size(); i++) { const Json q = L.at(i); const EngLayer& e = V.layers[i];
      CHECK(same(e.atk, q.num("atk")) && same(e.hold, q.num("hold")) && same(e.dec, q.num("dec")) && same(e.rel, q.num("rel")) && same(e.fAtk, q.num("fAtk")) && same(e.fDec, q.num("fDec")),
            "case %zu layer %zu (bpm %g lpb %g): atk %.17g/%.17g hold %.17g/%.17g dec %.17g/%.17g rel %.17g/%.17g fAtk %.17g/%.17g fDec %.17g/%.17g", ci, i, bpm, lpb, e.atk, q.num("atk"), e.hold, q.num("hold"), e.dec, q.num("dec"), e.rel, q.num("rel"), e.fAtk, q.num("fAtk"), e.fDec, q.num("fDec")); }
    if (V0.gAmpOn && V0.gLines) CHECK(same(V.gAtk, out.num("gAtk")) && same(V.gHold, out.num("gHold")) && same(V.gDec, out.num("gDec")) && same(V.gRel, out.num("gRel")), "case %zu: the pad amp", ci);
    else CHECK(same(V.gAtk, V0.gAtk) && same(V.gRel, V0.gRel), "case %zu: a pad amp not in lines changed", ci);
    CHECK(same(V.len, out.num("len")) && same(hold, out.num("hold")), "case %zu: len %.17g (JS %.17g), hold %.17g (JS %.17g)", ci, V.len, out.num("len"), hold, out.num("hold"));
    // the audio: a voice written in ms with the JS's numbers renders as this one does (a few cases: rendering is the slow part)
    if (nRender < 12 && c.get("lines").asBool()) {
      Json ls = vj.arr("layers"); bool renderable = true; for (size_t i = 0; i < ls.size(); i++) { const std::string_view t = ls.at(i).str("type"); if (t != "white") renderable = false; }
      if (!renderable || V.len > 20) continue;
      EngVoice M = V0; for (size_t i = 0; i < L.size(); i++) { EngLayer& e = M.layers[i]; const Json q = L.at(i); e.aLines = e.fLines = false; e.atk = q.num("atk"); e.hold = q.num("hold"); e.dec = q.num("dec"); e.rel = q.num("rel"); e.fAtk = q.num("fAtk"); e.fDec = q.num("fDec"); }
      if (V0.gAmpOn && V0.gLines) { M.gLines = false; M.gAtk = out.num("gAtk"); M.gHold = out.num("gHold"); M.gDec = out.num("gDec"); M.gRel = out.num("gRel"); }
      M.len = out.num("len");
      const std::vector<float> a = render(V, hold, bpm == bpm && bpm != 0 ? bpm : 120), b = render(M, out.num("hold"), bpm == bpm && bpm != 0 ? bpm : 120);
      CHECK(!a.empty() && a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * 4) == 0, "case %zu: the lines voice and its ms equivalent render differently (%zu vs %zu samples)", ci, a.size(), b.size());
      nRender++; }
  }
  CHECK(nFit > 100 && nRender >= 3, "the cases prove too little: %d fits, %d renders", nFit, nRender);
  // an old voice (no unit field at all) is not touched: no fit, the same render
  { const char* js = "{\"gate\":200,\"len\":4,\"layers\":[{\"on\":true,\"type\":\"white\",\"level\":0.8,\"mode\":\"AHD\",\"atk\":5,\"hold\":0,\"dec\":300}]}"; JsonDoc d; if (!d.parse(js)) return 1;
    EngVoice V; const Json none; eng_voice_from_json(V, d.root(), d.root().arr("layers"), none); double hold = 6; const EngVoice V0 = V;
    CHECK(!eng_env_lines_fit(V, &hold, 120, 8) && hold == 6 && V.len == V0.len && V.layers[0].atk == V0.layers[0].atk, "a voice without a unit field is changed");
    CHECK(render(V, 0, 120) == render(V0, 0, 120), "an old voice renders differently"); }
  std::printf("envlines: %zu cases, %d with lines, %d rendered, %d failed\n", cases.size(), nFit, nRender, fails);
  return fails ? 1 : 0;
}
