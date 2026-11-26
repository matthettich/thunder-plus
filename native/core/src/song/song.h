// THE SONG AS THE CORE READS IT (U06). One immutable snapshot, built on the control thread by Song::load() and swapped in by
// SnapSlot; the audio thread only reads it, and only the typed tables below (never the JSON tree).
//
// Two shapes are accepted: a saved .thunder file {app:'thunder-project', v, build, song:{...}, kit:{...}}, and a bare song S
// (what op:'load' sends), alone or inside a message {"t":"song.load","rev":N,"S":{...}}.
//
// The typed tables hold what index.html's normSong makes of the fields (the same clamps, defaults and "empty" cases: -1 in a
// song cell or phrase value, a null phrase or chain), so a raw saved file and an S that has already been through normSong give
// the same tables. The whole document stays in `doc`: unknown fields anywhere are carried untouched, and json_write(root)
// gives them back. The parts later units parse (tfx, mfx, chainPatch, mst, perf, parts, kit...) are handles into the doc.
#ifndef THUNDER_SONG_SONG_H
#define THUNDER_SONG_SONG_H
#include <cstdint>
#include <vector>

#include "rt/json.h"

namespace thunder {

constexpr int kSongNT = 32;          // tracks the format holds (NT in index.html); the master column is index kSongNT
constexpr int kSongMT = kSongNT;
constexpr int kSongRows = 256;       // arrangement rows
constexpr int kSongCols = kSongNT + 1;
constexpr int kNPH = 4096;           // phrases and chains
constexpr int kPhraseRows = 32;      // PROWS: rows stored per phrase
constexpr int kRW = 12;              // ints per phrase row: 4 note columns, volume, instrument, 3 x (command, value)
constexpr int kFX0 = 6;              // first command column
constexpr int kChainRows = 16;       // entries per chain: [phrase, transpose]
constexpr int kNI = 64;              // instruments
constexpr int kFxcCount = 90;        // FXC.length: a command index at or past it is dropped
constexpr int kKnownBuild = 684;     // BUILD of the page this core was ported from

struct SongMix { int vol = 224, trim = 128, pan = 128; bool mute = false, solo = false; int col = 0; };

struct SongSx { int sx = 0, ou = 0, orp = 0, ox = 0, opt = 0; double og = 12; };
double song_js_plus(Json v);   // the JS's unary +x of a JSON value (a numeric string is its number; NaN if not numeric)
SongSx song_sx_load(Json o, int sync);   // index.html sxLoad (:23642): the instrument object `o`, its (already clamped) sync

struct SongInst {
  int pad = 0, tsp = 0, vol = 255, pan = 128, rev = 0, ofs = 0, slc = 0, smode = 0, sync = 0, beats = 4, grain = 1;
  // WEB-FOLLOW (20 Nov): SYNC TO LINES `lines` (0 = absent; web #257) and the stretch algorithm `sx` with OLDSCHOOL's own settings (web #259-#263: index.html sxLoad). Carried, never used to sound.
  int lines = 0, sx = 0, ou = 0, orp = 0, ox = 0, opt = 0; double og = 12;
  int mo = 0, mch = 0, mpt = 0, pbr = 2;
  int cc[4] = {74, 71, 1, 7};
  int lcc = -1, lsh = 0; double lrb = 4; int ldp = 64, lct = 64, lrg = 0;
  int kf = 0, hut = 0, huv = 0, mlen = 0, eco = 0; double ecr = 0.5; int ecf = 30, ecp = 0;
  int mrng = 64, gtype = 0, gcen = 0;
};


struct Song {
  JsonDoc doc;                  // the whole message or file
  Json root;                    // the document's root
  Json S;                       // the song object (root, root.S, or file.song)
  Json kit;                     // the kit object of a file (Missing when there is none)
  bool isFile = false;          // the .thunder shape

  int64_t rev = 0;              // the message's "rev" (0 when absent)
  int build = 0;                // the file's / message's build (0 when absent)
  bool newerBuild = false;      // build > kKnownBuild: loaded anyway; the app only shows a toast
  int v = 1;                    // the song's format version (S.v)
  bool legacyFx = false;        // v < 2: the old command list (FXC_V1) is NOT remapped here; those phrases keep their raw numbers

  // header
  int bpm = 120, swing = 50, swingB = 2, lpb = 4, ticks = 6, dlen = 16, ntr = 16, ni = 16;
  int master = 255, mout = 255, renderFrom = 0, scale = 0, sroot = 0;
  int nmono = 0;                // 0: the half rule (the mono half gets the extra track); else the song's own count
  int hmono = -1;               // -1: absent (a song from before the field)
  bool limiter = true, mstPump = false;

  // tables
  std::vector<int16_t> song;                 // kSongRows x kSongCols: chain index per track (and the master column), -1 empty
  std::vector<int32_t> chainOff;             // kNPH: offset into chainData, -1 = null chain
  std::vector<int16_t> chainData;            // per non-null chain kChainRows x {phrase (-1..4095), transpose (-128..127)}
  std::vector<int32_t> phraseOff;            // kNPH: offset into phraseData, -1 = null phrase
  std::vector<int16_t> phraseData;           // per non-null phrase kPhraseRows x kRW ints (-1..255; -1 empty)
  std::vector<int16_t> plen, plpb;           // kNPH each
  std::vector<int32_t> smute;                // kSongRows mute words
  double tune[12] = {0};
  bool chainOn[kSongNT] = {false};
  SongMix mix[kSongNT];
  SongInst insts[kNI];
  bool instMissing[kNI] = {false};   // pageS only: insts[i] is not an object (an edit made it; the page's S has all 64): the sequencer plays instrument 0 (makeSeq: S.insts[ii] || S.insts[0])

  // handles into the doc (Missing when the song has none); parsed by the units that use them
  Json tfx, mfx, chainPatch, mst, perf, parts, presets, irs;

  // Parses text (a file or a message). false: *err says where and why; nothing was built.
  // pageS: the text is the page's own S as it stands after an edit (the 'song' / 'patch' ops of a running sequencer), not a file or a first load. normSong's
  // load-time rules are then not applied again where an in-place edit can undo them (SEQ-LIVE-DESIGN Part B: the running JS sequencer reads S as edited):
  // an empty phrase keeps the length it was given, and an instrument that is not an object stays missing (the sequencer plays instrument 0, D9).
  bool load(const char* text, size_t len, JsonError* err, bool pageS = false);

  const int16_t* chain(int id) const { return id >= 0 && id < kNPH && chainOff[(size_t)id] >= 0 ? chainData.data() + chainOff[(size_t)id] : nullptr; }
  const int16_t* phrase(int id) const { return id >= 0 && id < kNPH && phraseOff[(size_t)id] >= 0 ? phraseData.data() + phraseOff[(size_t)id] : nullptr; }
  int cell(int row, int t) const { return song[(size_t)row * kSongCols + (size_t)t]; }
  int phrasesPresent() const;                // phrases that are not null
  int phrasesWithContent() const;            // phrases with at least one value >= 0
};

}  // namespace thunder
#endif
