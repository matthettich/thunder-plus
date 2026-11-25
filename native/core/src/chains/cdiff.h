// C2a: the control side's reading of the page's `chnDiff` (index.html, `function chnDiff`): what changed between the chains patch a node has
// and the one the song has now, in the only two classes the live host acts on.
//   Same        chnDiff gives [] (nothing the engine would be told).
//   Params      every op chnDiff gives is a `param` op whose value is a finite number and whose module id is a string: `params` lists them in the
//               order chnDiff gives them (modules in next's Map order, each module's params in JS for-in order). They can go to the audio thread
//               as setParam calls without touching the graph.
//   Structural  anything else: any other op (patch, add, remove, text, connect, disconnect), a param value that is not a finite number (the page
//               compares with `!==`: a string / boolean / null / array / object / out-of-range number that differs is not a number the engine can
//               set), a param op whose module id is not a string, or a document on which chnDiff would THROW (modules / cables that are not arrays,
//               a null module or cable, a cable without from / to). The page's caller sends the whole patch then; here the host swaps the plan.
// Control thread only: it allocates. It throws nothing but std::bad_alloc and never reads outside the documents, whatever they hold.
// Where the page's JS has a quirk it is followed, and the quirks are listed in cdiff.cpp (duplicate ids, for-in order, UTF-16 slices, the cable key's string conversions).
#ifndef THUNDER_CHAINS_CDIFF_H
#define THUNDER_CHAINS_CDIFF_H
#include <string>
#include <vector>

#include "mix/mix.h"
#include "rt/json.h"

namespace thunder {

enum class ChainsDiffKind { Same, Params, Structural };
struct ChainsParamOp { std::string id, name; double v = 0; };
struct ChainsDiff { ChainsDiffKind kind = ChainsDiffKind::Same; std::vector<ChainsParamOp> params; };

// chnDiff(prev, next), classified. A Missing Json is the page's `undefined` / `null`.
ChainsDiff chains_diff(Json prev, Json next);

// Field-for-field equality of two bus plans (the first nFeeds entries of each feed list; every mask; pure, pre, loop). Used to decide whether a chains
// publish can keep the running patch.
bool bus_plan_equal(const BusPlan& a, const BusPlan& b);

}  // namespace thunder
#endif
