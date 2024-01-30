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

#endif
