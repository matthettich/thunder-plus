# Verification of PR #254 (build 651, bus into bus) - independent check

Status: in progress (items 1 and 2 done; 3-9 follow). Head checked: afa5eb9. Harness: own scripts (not the PR's test), plan/derived code cut from `origin/beta` and PR `index.html`, run in vm.

## 1. Old songs sound the same - CONFIRMED, with two notes
Compared `busPlan` (feeds, targets, pure, pre, loop), isBusT, isSendT, busGroups, topological order, pdcPlan (random latencies), CHN labels (old MISS read as MS), audible (random mute/solo, plans of depth 1), stems (keepT, inBus) and the render closure, old vs new, plus the page copy vs the core copy of each version.
- Both shipped songs (ELECTRO, BETA-TESTING-ELECTRO) and all 20 fixture cases: identical, except the fixture cases that are the new shape (expected).
- 30022 cases (3 pools: tracks 1-4, 1-8, 1-32; random Track In / Chain Out / mixer modules and cables, random CHN, mute, solo). Excluded shape (a Track In of b that does not reach b's own Chain Out, and b is another track's target): 6362 precise / 9518 literal. Remaining ~23.6k cases: every field identical, 0 differences, including 217 loop cases (same kept-edge count, loop still reported, loopEdges filled, new plan acyclic, loopEdges independent of module order).
- Page copy == core copy on both versions (plan, loopEdges, srcAll, busAll): 0 differences.
- NOTE A (should fix or accept, minor): Map insertion order of `feeds` changes (old: module order; new: source-track order). Only visible reader: `busGroups` (`for (const [b, src] of P.feeds)`), where a source that feeds two buses takes the FIRST bus's colour. Smallest patch: Track In 3 -> Chain Out 6; Track In 1 -> Chain Outs 5 and 6 (CHN on 1,3,5,6; modules in that order). beta: track 1 takes bus 6's colour; PR: bus 5's. ~461 of 30000 random cases. Cosmetic (Arrange / Mixer colour of a send), no sound change. Not mentioned in the PR's changelog.
- NOTE B: loops - the PR is deliberate that the dropped cable can differ (ruling R3).

## 2. The owner's patch - CONFIRMED
(Track In 1,2,3,4,5,6,9,13 -> mixer -> Chain Out 8; Track In 8 -> Chain Out 14; CHN on those + 8, 14.)
- feeds {8:[1,2,3,4,5,6,9,13], 14:[8]}; targets 1-6,9,13 -> 8 and 8 -> 14; pure = the eight sources and 8; loop false.
- labels: BS on 01-06, 09, 13; ON on 08 and 14; every other `--`. Status lines read correctly (08 "bus of tracks ... feeds the bus on track 14"; 14 "bus of track 08").
- order: Kahn order from feeds puts 8 before 14 (the real host order is item 5).
- stems: keepT = sources + 8 + 14, the only stem is 14.
- latency plan (source 1 = 100 ms, 2 = 50 ms, bus 8 = 200 ms, bus 14 = 25 ms): arrival at 8 = 300, at 14 = 325 = T; f[2]=50, f[3..]=100 (sources aligned to the longest path into 8), f[8]=0; pure sources d=0; c[14]=300.
- three deep (-> 14 -> Chain Out 20): feeds 8, 14:[8], 20:[14]; labels ON on 8, 14, 20; stems: only 20; srcAll[20] = 1-6, 8, 9, 13, 14.
- diamond (1 -> buses 8 and 9, 2 -> 9, both -> 14): feeds 8:[1], 9:[1,2], 14:[8,9]; stems only 14; f[9]=200 so the early arm lines up at 14.
