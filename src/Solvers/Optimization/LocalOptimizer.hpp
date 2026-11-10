#pragma once
#include "CandidateArchive.hpp"

namespace opusoptima::solvers {
// Eight deterministic trials per distinct snapshot representative, at most twenty-four gates.
// Default order: {8,1,2,4,5,6,7,3}. Variant 8 (compact transport/hardware) is
// queued by default and takes the first position; measured on all 422 published
// slots it improved 158 children (136 sole-best, 39 the only improver) and was
// never native-rejected, whereas v0 improved none and was rejected 298 times. Exact
// OPUSOPTIMA_COMPACT_TRANSPORT=off restores the legacy order {0,1,2,4,5,6,7,3} and
// refuses v8; unset and on both select the new default. With
// OPUSOPTIMA_SHARED_BONDER=on the order is {8,1,2,3,4,5,6,7} (legacy
// {0,1,2,3,4,5,6,7} when off).
// Variant 5 composes useful return compaction and empty-rotation prepositioning,
// otherwise retaining timing division and idle-arm removal. Each edit needs the gate.
// Variant 6 defaults to guarded shared-track envelope trimming; only unset or exact
// OPUSOPTIMA_TRACK_ENVELOPE=on enables it. Other values retain legacy head deletion.
// Exact OPUSOPTIMA_IDLE_TRACK_ENVELOPE=on composes v2 idle removal before the
// envelope only when both the original and combined envelope passes succeed.
// Variants 9-12 (whole-stream shift, global time-rank compaction, trailing no-op
// trimming and two-copy tape compression) were removed: v9/v10 were native-rejected
// on every parent and v11/v12 produced byte-identical no-op children. No switch
// re-enables them.
void OptimizeVerified(io::CandidateArchive& archive,
                      std::chrono::steady_clock::time_point deadline);
[[nodiscard]] io::SolutionData LocalTrial(const io::SolutionData& parent, int variant);
} // namespace opusoptima::solvers
