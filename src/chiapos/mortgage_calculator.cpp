#include "mortgage_calculator.h"

#include <chain.h>
#include <validation.h>
#include <subsidy_utils.h>

#include <util/moneystr.h>

CBlockIndex const* FindPrevIndex(int nHeight, CBlockIndex const* pindex) {
    while (pindex->nHeight >= nHeight) {
        pindex = pindex->pprev;
    }
    return pindex;
}

CMortgageCalculator::CMortgageCalculator(CBlockIndex const* pindexTip, Consensus::Params params)
        : m_pindexTip(pindexTip), m_params(std::move(params)) {}

int CMortgageCalculator::CalcNumOfDistributions(int nHeight) const {
    CBlockIndex const* pcurr = m_pindexTip;
    while (pcurr->nHeight >= nHeight) {
        pcurr = pcurr->pprev;
    }
    int nNumOfFullMortgageBlocks{0};
    int nLowestHeight = std::max(m_params.BHDIP009Height,
                                 nHeight - m_params.BHDIP011NumHeightsToCalcDistributionPercentageOfFullMortgage);
    for (auto pindex = pcurr; pindex->nHeight >= nLowestHeight; pindex = pindex->pprev) {
        if (IsFullMortgageBlock(pindex, m_params)) {
            ++nNumOfFullMortgageBlocks;
        }
    }
    return std::max(m_params.BHDIP011MinFullMortgageBlocksToDistribute, nNumOfFullMortgageBlocks);
}

int CMortgageCalculator::CalcNumOfDistributed(int nDistributeFromHeight, int nTargetHeight) const {
    int nNumOfDistributed{0};
    for (auto pindex = FindPrevIndex(nTargetHeight, m_pindexTip); pindex->nHeight >= nDistributeFromHeight;
         pindex = pindex->pprev) {
        if (IsFullMortgageBlock(pindex, m_params)) {
            ++nNumOfDistributed;
        }
    }
    return nNumOfDistributed;
}

CAmount CMortgageCalculator::CalcDistributeAmount(int nDistributeFromHeight, int nTargetHeight) const {
    int nDistributions = CalcNumOfDistributions(nDistributeFromHeight);
    int nDistributed = CalcNumOfDistributed(nDistributeFromHeight, nTargetHeight);

    if (nDistributed >= nDistributions) {
        return 0;
    }

    CAmount nOriginalAccumulatedAmount =
            GetBlockAccumulateSubsidy(FindPrevIndex(nDistributeFromHeight, m_pindexTip), m_params);
    return nOriginalAccumulatedAmount / nDistributions;
}

std::tuple<CAmount, CMortgageCalculator::FullMortgageAccumulatedInfoMap> CMortgageCalculator::CalcAccumulatedAmount(
        int nTargetHeight) const {
    CAmount nTotalAmount{0};
    FullMortgageAccumulatedInfoMap mapFullMortgageAccumulatedInfo;
    for (auto pindex = FindPrevIndex(nTargetHeight, m_pindexTip); pindex->nHeight >= m_params.BHDIP009Height;
         pindex = pindex->pprev) {
        if (IsFullMortgageBlock(pindex, m_params)) {
            CAmount nAccumulatedAmount = CalcDistributeAmount(pindex->nHeight, nTargetHeight);
            nTotalAmount += nAccumulatedAmount;
            FullMortgageAccumulatedInfo entry;
            entry.nHeight = pindex->nHeight;
            entry.nAccumulatedAmount = nAccumulatedAmount;
            mapFullMortgageAccumulatedInfo.insert_or_assign(pindex->nHeight, std::move(entry));
        }
    }
    // don't forget to calculate the accumulate amount from current block
    CAmount nOriginalAccumulated = GetBlockAccumulateSubsidy(FindPrevIndex(nTargetHeight, m_pindexTip), m_params);
    CAmount nDistributions = CalcNumOfDistributions(nTargetHeight);
    return std::make_tuple(nTotalAmount + nOriginalAccumulated / nDistributions, mapFullMortgageAccumulatedInfo);
}

bool CMortgageCalculator::IsFullMortgageBlock(CBlockIndex const* pindex, Consensus::Params const& params) {
    if (pindex->nHeight < params.BHDIP009Height) {
        return false;
    }
    return (pindex->nStatus & BLOCK_UNCONDITIONAL) == 0;
}
