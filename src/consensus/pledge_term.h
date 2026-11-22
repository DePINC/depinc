#ifndef PLEDGE_TERM_H
#define PLEDGE_TERM_H

#include <amount.h>

namespace Consensus {
struct Params;
} // namespace Consensus

struct PledgeTerm {
    int nLockHeight;
    int nWeightPercent;
};

struct RetargetFee {
    int nMinThousandths;
    int nMaxThousandths;
};

struct RetargetInfo {
    int nTermIndex;
    CAmount nPointAmount;
    int nPointHeight;
};

CAmount CalculateTxFeeForPointRetarget(RetargetInfo const& retargetInfo, int nHeight, Consensus::Params const& params);

/**
 * Calculate the actual amount of a chia point pledge at a given chain height.
 *
 * The result is the term weight applied to the pledged amount; when the point
 * has been locked for longer than the term it falls back to the first term.
 * Returns 0 when chainHeight is 0 (unknown height).
 */
CAmount CalcActualAmount(CAmount pledgeAmount, int pledgeOnHeight, PledgeTerm const& term, PledgeTerm const& fallbackTerm, int chainHeight) noexcept;

#endif
