#include "mortgage_calculator.h"

#include <chain.h>
#include <validation.h>
#include <subsidy_utils.h>

#include <util/moneystr.h>

class IndexRangeControl {
public:
    static IndexRangeControl& GetInstance() {
        static IndexRangeControl instance;
        return instance;
    }

    int GetLowestHeight() const {
        if (m_mortgage_heights.empty()) {
            LogPrint(BCLog::BENCH, "%s: cached 0 heights\n", __func__);
            return 0;
        }
        int nLowestHeight = *m_mortgage_heights.begin();
        LogPrint(BCLog::BENCH, "%s: cached %d heights, lowest height %d\n", __func__, m_mortgage_heights.size(), nLowestHeight);
        return nLowestHeight;
    }

    void InsertHeight(int nHeight) { m_mortgage_heights.insert(nHeight); }

    void RemoveHeight(int nHeight) { m_mortgage_heights.erase(nHeight); }

private:
    std::set<int> m_mortgage_heights;
};

CBlockIndex const* FindPrevIndex(int nHeight, CBlockIndex const* pindex) {
    while (pindex->nHeight >= nHeight) {
        pindex = pindex->pprev;
    }
    return pindex;
}

CMortgageCalculator::CMortgageCalculator(CBlockIndex const* pindexTip, Consensus::Params params)
        : m_pindexTip(pindexTip), m_params(std::move(params)) {}

std::tuple<CAmount, CMortgageCalculator::FullMortgageAccumulatedInfoMap> CMortgageCalculator::CalcAccumulatedAmount(
        int nTargetHeight) const {
    CAmount nTotalAmount{0};
    int nLowestHeight = std::max(m_params.BHDIP011Height, IndexRangeControl::GetInstance().GetLowestHeight());
    FullMortgageAccumulatedInfoMap mapFullMortgageAccumulatedInfo;
    for (auto pindex = FindPrevIndex(nTargetHeight, m_pindexTip); pindex->nHeight >= nLowestHeight;
         pindex = pindex->pprev) {
        if (IsFullMortgageBlock(pindex, m_params)) {
            CAmount nAccumulatedAmount = CalcDistributeAmountToTargetHeight(pindex->nHeight, nTargetHeight);
            if (nAccumulatedAmount > 0) {
                IndexRangeControl::GetInstance().InsertHeight(pindex->nHeight);
                nTotalAmount += nAccumulatedAmount;
                FullMortgageAccumulatedInfo entry;
                entry.nHeight = pindex->nHeight;
                entry.nAccumulatedAmount = nAccumulatedAmount;
                mapFullMortgageAccumulatedInfo.insert_or_assign(pindex->nHeight, std::move(entry));
            } else {
                IndexRangeControl::GetInstance().RemoveHeight(pindex->nHeight);
            }
        }
    }
    // don't forget to calculate the accumulate amount from current block
    CAmount nOriginalAccumulated = GetBlockAccumulateSubsidy(FindPrevIndex(nTargetHeight, m_pindexTip), m_params);
    CAmount nDistributions = CalcNumOfDistributions(nTargetHeight);
    return std::make_tuple(nTotalAmount + nOriginalAccumulated / nDistributions, mapFullMortgageAccumulatedInfo);
}

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

int CMortgageCalculator::CalcNumOfDistributedForTargetHeight(int nDistributeFromHeight, int nTargetHeight) const {
    int nNumOfDistributed{0};
    for (auto pindex = FindPrevIndex(nTargetHeight, m_pindexTip); pindex->nHeight >= nDistributeFromHeight;
         pindex = pindex->pprev) {
        if (IsFullMortgageBlock(pindex, m_params)) {
            ++nNumOfDistributed;
        }
    }
    return nNumOfDistributed;
}

std::tuple<CAmount, CAmount> CMortgageCalculator::GetDistrInfo(int nDistributeFromHeight, int nTargetHeight) const {
    int nDistributions = CalcNumOfDistributions(nDistributeFromHeight);
    int nDistributed = CalcNumOfDistributedForTargetHeight(nDistributeFromHeight, nTargetHeight);
    return std::make_tuple(nDistributions, nDistributed);
}

CAmount CMortgageCalculator::CalcDistributeAmountToTargetHeight(int nDistributeFromHeight, int nTargetHeight) const {
    auto [nDistributions, nDistributed] = GetDistrInfo(nDistributeFromHeight, nTargetHeight);
    if (nDistributed >= nDistributions) {
        // The distribution is completed for the nDistributeFromHeight
        return 0;
    }

    CAmount nOriginalAccumulatedAmount =
            GetBlockAccumulateSubsidy(FindPrevIndex(nDistributeFromHeight, m_pindexTip), m_params);
    return nOriginalAccumulatedAmount / nDistributions;
}

bool CMortgageCalculator::IsFullMortgageBlock(CBlockIndex const* pindex, Consensus::Params const& params) {
    if (pindex->nHeight < params.BHDIP009Height) {
        return false;
    }
    return (pindex->nStatus & BLOCK_UNCONDITIONAL) == 0;
}
