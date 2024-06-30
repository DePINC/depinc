#include "mortgage_calculator.h"

#include <chain.h>
#include <validation.h>
#include <subsidy_utils.h>

#include <util/moneystr.h>

CMortgageCalculator::CMortgageCalculator(CBlockIndex const* pindexTip, Consensus::Params params)
        : m_pindexTip(pindexTip), m_params(std::move(params)) {}

int CMortgageCalculator::CalcNumOfDistributions(CBlockIndex const* pindexFullMortgage) const {
    assert(IsFullMortgageBlock(pindexFullMortgage, m_params));

    int nNumOfFullMortgageBlocks{0};
    int nLowestHeight = std::max(
            m_params.BHDIP009Height,
            pindexFullMortgage->nHeight - m_params.BHDIP011NumHeightsToCalcDistributionPercentageOfFullMortgage);
    for (auto pindex = pindexFullMortgage->pprev; pindex->nHeight >= nLowestHeight; pindex = pindex->pprev) {
        if (IsFullMortgageBlock(pindex, m_params)) {
            ++nNumOfFullMortgageBlocks;
        }
    }
    return nNumOfFullMortgageBlocks;
}

int CMortgageCalculator::CalcNumOfDistributed(CBlockIndex const* pindexFullMortgage,
                                              CBlockIndex const* pindexPrev) const {
    int nNumOfDistributed{1};
    for (auto pindex = pindexPrev; pindex != pindexFullMortgage; pindex = pindex->pprev) {
        if (IsFullMortgageBlock(pindex, m_params)) {
            ++nNumOfDistributed;
        }
    }
    return nNumOfDistributed;
}

CAmount CMortgageCalculator::CalcDistributeAmount(CBlockIndex const* pindexFullMortgage, int nTargetHeight) const {
    int nDistributions = CalcNumOfDistributions(pindexFullMortgage);
    int nDistributed = CalcNumOfDistributed(pindexFullMortgage, m_pindexTip);

    if (nDistributed >= nDistributions) {
        return 0;
    }

    CAmount nOriginalAccumulatedAmount = GetBlockAccumulateSubsidy(pindexFullMortgage->pprev, m_params);
    return nOriginalAccumulatedAmount / nDistributions;
}

CAmount CMortgageCalculator::CalcAccumulatedAmount(int nTargetHeight) const {
    CAmount nTotalAmount{0};
    for (auto pindex = m_pindexTip; pindex->nHeight >= m_params.BHDIP009Height; pindex = pindex->pprev) {
        if (IsFullMortgageBlock(pindex, m_params)) {
            nTotalAmount += CalcDistributeAmount(pindex, nTargetHeight);
        }
    }
    return nTotalAmount;
}

bool CMortgageCalculator::IsFullMortgageBlock(CBlockIndex const* pindex, Consensus::Params const& params) {
    if (pindex->nHeight < params.BHDIP009Height) {
        return false;
    }
    return (pindex->nStatus & BLOCK_UNCONDITIONAL) == 0;
}
