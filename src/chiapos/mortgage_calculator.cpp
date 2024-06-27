#include "mortgage_calculator.h"

#include <chain.h>
#include <validation.h>
#include <subsidy_utils.h>

namespace {

using BlockPred = std::function<int64_t(CBlockIndex* curr)>;

void FindBlocksToDistribute(CBlockIndex* pfrom, CBlockIndex* pto, int nNumOfDistributions,
                            std::set<int>& outBlockHeights) {
    while (!pfrom->GetBlockHeader().IsFullMortgageBlock()) pfrom = pfrom->pprev;
    if (pfrom != pto) {
        FindBlocksToDistribute(pfrom->pprev, pto, nNumOfDistributions, outBlockHeights);
    }
    if (outBlockHeights.size() < nNumOfDistributions) {
        outBlockHeights.insert(pfrom->nHeight);
    }
}

}  // namespace

UniValue FullMortgageBlock::ToUniValue() const {
    auto trans_func = [](std::set<int> const& heights) -> UniValue {
        UniValue outVals(UniValue::VARR);
        for (int nHeight : heights) {
            outVals.push_back(nHeight);
        }
        return outVals;
    };

    UniValue resVal(UniValue::VOBJ);
    resVal.pushKV("height", nHeight);
    resVal.pushKV("originalAccumulated", nOriginalAccumulatedToDistribute);
    resVal.pushKV("numOfDistribution", nNumOfDistribution);
    resVal.pushKV("distributedToBlocks", trans_func(vDistributedToBlocks));
    resVal.pushKV("distributedFromBlocks", trans_func(vDistributedFromBlocks));
    resVal.pushKV("actualAccumulated", nActualAccumulated);

    return resVal;
}

CMortgageCalculator::CMortgageCalculator(Consensus::Params params) : m_params(std::move(params)) {}

bool CMortgageCalculator::IsEmpty() const {
    bool fEmpty = m_mapBlocks.empty();
    return fEmpty;
}

void CMortgageCalculator::Build(CBlockIndex* pindex) {
    assert(IsEmpty());
    for (auto pcurr = pindex; pcurr != nullptr && pcurr->nHeight >= m_params.BHDIP011Height; pcurr = pcurr->pprev) {
        if (pcurr->GetBlockHeader().IsFullMortgageBlock()) {
            // build full mortgage block
            FullMortgageBlock fmb;
            fmb.nHeight = pcurr->nHeight;
            fmb.nOriginalAccumulatedToDistribute = GetBlockAccumulateSubsidy(pcurr->pprev, m_params);
            fmb.nNumOfDistribution = GetNumOfBlocksToDistribute(pcurr);
            // find those blocks to distribute
            FindBlocksToDistribute(pindex, pcurr, fmb.nNumOfDistribution, fmb.vDistributedToBlocks);
            m_mapBlocks.insert(std::make_pair(pcurr->nHeight, std::move(fmb)));
        }
    }
    for (auto const& blockEntry : m_mapBlocks) {
        for (auto nHeightToDistribute : blockEntry.second.vDistributedToBlocks) {
            m_mapBlocks[nHeightToDistribute].vDistributedFromBlocks.insert(blockEntry.first);
        }
    }
}

void CMortgageCalculator::AddNewFullMortgageBlock(CBlockIndex* pindexPrev) {
    // ensure the mortgage block doesn't exist
    assert(m_mapBlocks.find(pindexPrev->nHeight + 1) == std::cend(m_mapBlocks));

    FullMortgageBlock fmb;
    fmb.nHeight = pindexPrev->nHeight + 1;
    fmb.nOriginalAccumulatedToDistribute = GetBlockAccumulateSubsidy(pindexPrev, m_params);
    fmb.nNumOfDistribution = GetNumOfBlocksToDistribute(pindexPrev);
    fmb.vDistributedToBlocks.insert(fmb.nHeight);

    for (auto& blockEntry : m_mapBlocks) {
        if (!blockEntry.second.IsDistributionFinished()) {
            blockEntry.second.vDistributedToBlocks.insert(fmb.nHeight);
            fmb.vDistributedFromBlocks.insert(blockEntry.first);
        }
    }
}

CAmount CMortgageCalculator::GetActualAccumulatedForBlockHeight(int nHeight) const {
    auto iter = m_mapBlocks.find(nHeight);
    if (iter == std::cend(m_mapBlocks)) {
        throw std::runtime_error(
                tinyformat::format("the height of the block doesn't exist from full mortgage map, height=%d", nHeight));
    }
    return iter->second.CalcActualAccumulatedAmount(m_mapBlocks);
}

FullMortgageBlockMap const& CMortgageCalculator::GetMap() const { return m_mapBlocks; }

int CMortgageCalculator::GetNumOfBlocksToDistribute(CBlockIndex* pindexPrev) const {
    int nLowestHeight = pindexPrev->nHeight - m_params.BHDIP011NumHeightsToCalcDistributionPercentageOfFullMortgage;
    int nDistribution{0};
    while (pindexPrev->nHeight >= nLowestHeight) {
        if (IsBlockFullMortgage(pindexPrev)) {
            ++nDistribution;
        }
        // next
        pindexPrev = pindexPrev->pprev;
    }
    return std::max(m_params.BHDIP011MinFullMortgageBlocksToDistribute, nDistribution);
}

bool CMortgageCalculator::IsBlockFullMortgage(CBlockIndex* pindex) const {
    if (pindex->nHeight >= m_params.BHDIP011Height) {
        return pindex->GetBlockHeader().IsFullMortgageBlock();
    }
    CBlock block;
    if (!ReadBlockFromDisk(block, pindex, m_params)) {
        throw std::runtime_error(tinyformat::format("the block cannot be found from disk, height=%d", pindex->nHeight));
    }
    CAmount nSubsidy = GetBlockSubsidy(pindex->nHeight, m_params);
    for (auto const& tx : block.vtx) {
        if (tx->IsCoinBase()) {
            if (tx->GetValueOut() > nSubsidy) {
                return true;
            }
            break;
        }
    }
    return false;
}
