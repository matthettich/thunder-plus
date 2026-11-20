# Verification of PR #259 (build 658, envelope times in lines) — IN PROGRESS

Head 4f37c68 vs origin/beta. Independent harness (/tmp/v259, loads the ENGINE block of each index.html in a vm like tools/engine-golden.js).

## 1. Old sounds bit-identical: voiceMod skip
Code read: `voiceMod` is stateless (hashRnd is a pure hash of (n, k); no RNG draw, LFO phase, S&H or follower is carried between calls), so skipping it cannot change later state. The skip tests `V.r1amt||..||V.r7amt` (truthiness); voiceMod uses `V['r'+i+'amt'] || 0`: the same test, so 0, -0, NaN, '', null, undefined, false are all "no amount" in both. Strings such as '0' or 'abc' are truthy, so voiceMod still runs in both.

Results (random voices, 1-4 layers of every data-free layer type, ~12% engine voices, pad amp on/off, mod routes with amounts 0/-0/1e-9/-1e-9/NaN/undefined/null/''/false/'0'/'abc'/tiny/real, random LFO/env/seq fields):
- 3000 voices, all-finite LFO/env/seq fields: 2999 identical; 1 hash differs (B|154) but both outputs are 100 percent NaN (routes with amount 'abc', voiceMod runs on both sides): NaN bit pattern only, not a real difference. 0 real differences.
- 3000 voices with corrupt LFO/seq fields (Sync outside 0..8, Infinity/NaN rate or depth or seqRate/seqDepth, non-array seq) plus zero amounts: 20 differ. In 19 the OLD build outputs NaN (all samples or most) because `s * dep * 0` is NaN when s or dep is non-finite (zero amount does not protect); the PR renders clean audio. In 1 (B|1064) the old output is finite but built from a NaN pitch term; the PR is the sane one. These voices cannot come from the UI (sync is a 0..8 selector, rates are clamped sliders) and were already broken; the PR does not change any voice that rendered correctly. Note, not a regression.
- Factory / presets / shipped voices: see below (running).
