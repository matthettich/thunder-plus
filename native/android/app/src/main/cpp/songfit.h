// N2 H1b: SongFit, the fallback decision. "A song the new engine cannot play goes to the old engine (the page's own Web Audio in the WebView), with a short message" (PICKUP.md 21 Nov 18:18,
// the owner's rule; the lead's ruling of 24 Nov: an unported pad effect is a SONG-level reason too). Pure: no Android header, no allocation on any audio path; a Linux test runs it.
//
// THE REASONS IT CAN RETURN (each a short English line the shell shows; `code` is the stable key the page can switch on):
//   no-effects      caps lack THUNDER_LIVE_CAP_EFFECTS: this build has no effects library (live.h CAP_EFFECTS).
//   no-chains       the song has a chain on (song.chainOn) and the chain patch has modules, and caps lack THUNDER_LIVE_CAP_CHAINS (the host was built or created without F_CHAINS).
//   sidechain       the chain patch has a Sidechain In / Out module (type scin / scout): "silent in this version" (live.h CHAINS paragraph: sidechain taps and sends are C3).
//   stretch         an instrument with sync 2 or 3, or with sx set (live.h NOT IN THIS API: the stretch, D10).
//   font            a voice whose engine is `sf` (live.h CAP_SF is not set: font_add / pad_sf are E_UNSUPPORTED).
//   engine          a voice whose engine or layer type the core's pad renderer does not render (EngRender bound 0 / not ported: the wasm-backed engines, `wavesl`, the pump synths until U33's render is wired).
//   pad-effect      a voice with a pad effect the core does not host (engine.h EngFx::Kind::Other, switched on): the pad would sound dry. One reason per pad, at most 8 listed.
//   no-lxr          a voice of the optional drum engines (lxdrum, lxsnare, lxcym, lxhat) while the module is not registered (a closed build): the pad would be empty.
//   too-big         the song text is over the host's limit (opts.max_song_bytes, 16 MiB by default).
//   resample        the chain patch has a Resample module (type "resample": live.h NOT IN THIS API, C3 / C4).
//   pump-in-chain   the chain patch has a pump module (ptube psnap pgate pecho pspring pverb pgrain pvox ptune pcrush pswirl pstutter ptwang): a pump module in a live chain has no spare instance until C4 (kPumpInChainServiced).
//   module          the chain patch has a module type this build does not know (chains::cdef is null).
//   lxp             the page has a live transpose on (FitContext::lxpActive) and the song has a KEY FOLLOW instrument (kf): live.h has no `lxp` call (G11).
//   clock-follow    the page follows an external MIDI clock (FitContext::clockIn: midi.cfg): the live host has no MIDI clock in (live.h NOT IN THIS API; N2-PAGE-CLIENT 4.6).
//   no-kit          NOT a verdict: no kit.load has reached the shell yet, so the voices cannot be judged. `pending` is true, `ok` false; the verdict follows in the `fit` event after kit.load (verify-n2 F6).
// WHAT IT DOES NOT CHECK (GUESS, listed so nobody believes it does): pump slots in a track's effects (FX1 hosts them), MIDI-only features (the page keeps them), tracker commands the sequencer ignores (the
// sequencer is the golden-tested port of the page's), the page-only conditions of N2-PAGE-CLIENT 4.6 other than lxp and clock follow (recording, count-in, Link: the page's own can()). A song that passes can still be
// refused by thunder_live_song_publish / thunder_live_play (E_SONG, E_LIMIT, E_MEMORY, E_UNSUPPORTED): the shell then falls back with the error text, as it does for these reasons.
#ifndef TPA_SONGFIT_H
#define TPA_SONGFIT_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tpa {

struct FitReason { std::string code; std::string text; };
// What only the page / shell knows (not in the song text): passed by the bridge from the messages it has seen.
struct FitContext { bool lxpActive = false; bool clockIn = false; };
// A pump module in a live chain is serviced only when C4 (the pump service() driver) is built: false today (live.h NOT IN THIS API).
constexpr bool kPumpInChainServiced = false;

struct FitResult {
  bool ok = true;
  bool pending = false;             // no kit yet: not a verdict (ok is false); the next `fit` event after kit.load is
  std::vector<FitReason> reasons;
  std::vector<std::string> warns;   // non-fatal facts the page shows ("3 pads play their rendered twin": a live-engine pad (bass, vox, acid, mach) stays on the sampler route, live.h CAP_ENGINES / ruling D8)
  std::string json(int64_t rev) const;   // {"t":"fit","rev":N,"ok":..,"reasons":[{"code","text"}],"reasonTexts":[..],"warn":[..]}: the bridge's `fit` event (reasonTexts and warn are the flat lists of the song.load reply, CORE-BRIDGE 7 G14)
};

// songJson: the song object (a .thunder file's `song`, or the page's S). kitJson: the kit object (`kit`; empty: voices are not checked, the page will `put` its own pads).
// caps: thunder_live_caps() of this build (THUNDER_LIVE_CAP_*) OR'ed with the host's own: pass THUNDER_LIVE_CAP_CHAINS only when the host was created with F_CHAINS.
// maxSongBytes: opts.max_song_bytes (0: 16 MiB). lxrRegistered: thunder_lxr_api() != null.
// kitJson EMPTY: the song-level checks run, but there is NO verdict (pending, ok false): the voices of an unseen kit cannot be judged, and "ok" for them would be a guess.
FitResult songFit(std::string_view songJson, std::string_view kitJson, uint32_t caps, uint64_t maxSongBytes, bool lxrRegistered, const FitContext& ctx = FitContext());

}  // namespace tpa
#endif
