#include "pledge_term.h"

#include <consensus/params.h>

#include <algorithm>

CAmount CalculateTxFeeForPointRetarget(RetargetInfo const& retargetInfo, int nHeight, Consensus::Params const& params) {
    auto const& term = params.BHDIP009PledgeTerms.at(retargetInfo.nTermIndex);
    auto const& feeRange = params.BHDIP010RetargetFees.at(retargetInfo.nTermIndex);
    int nLockedHeight = nHeight - retargetInfo.nPointHeight;
    int nTxFeeThousandths = (feeRange.nMaxThousandths - feeRange.nMinThousandths) * nLockedHeight / term.nLockHeight + feeRange.nMinThousandths;
    CAmount nTxFee = nTxFeeThousandths * retargetInfo.nPointAmount / 1000;
    return nTxFee;
}

CAmount CalcActualAmount(CAmount pledgeAmount, int pledgeOnHeight, PledgeTerm const& term, PledgeTerm const& fallbackTerm, int chainHeight) noexcept {
    if (chainHeight == 0) {
        return 0;
    }
    int pledgedHeights = std::max(chainHeight - pledgeOnHeight, 0);
    if (pledgedHeights > term.nLockHeight) {
        // should use the fallback term
        return fallbackTerm.nWeightPercent * pledgeAmount / 100;
    }
    return term.nWeightPercent * pledgeAmount / 100;
}
