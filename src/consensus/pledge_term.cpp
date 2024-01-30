#include "pledge_term.h"

#include <consensus/params.h>

CAmount CalculateTxFeeForPointRetarget(RetargetInfo const& retargetInfo, int nHeight, Consensus::Params const& params) {
    auto const& term = params.BHDIP009PledgeTerms.at(retargetInfo.nTermIndex);
    auto const& feeRange = params.BHDIP010RetargetFees.at(retargetInfo.nTermIndex);
    int nLockedHeight = nHeight - retargetInfo.nPointHeight;
    int nTxFeeThousandths = (feeRange.nMaxThousandths - feeRange.nMinThousandths) * nLockedHeight / term.nLockHeight + feeRange.nMinThousandths;
    CAmount nTxFee = nTxFeeThousandths * retargetInfo.nPointAmount / 1000;
    return nTxFee;
}
