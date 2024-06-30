#include "mortgage_calculator.h"

#include <chain.h>
#include <validation.h>
#include <subsidy_utils.h>

#include <util/moneystr.h>

namespace {

bool IsBlockFullMortgage(CBlockIndex* pindex, Consensus::Params const& params) {
    if (pindex->nHeight >= params.BHDIP009Height) {
        return (pindex->nStatus & BLOCK_UNCONDITIONAL) == 0;
    }
    return false;
}

void FindBlocksToDistribute(CBlockIndex* pfrom, CBlockIndex* pto, int nNumOfDistributions,
                            Consensus::Params const& params, std::set<int>& outBlockHeights) {
    while (!IsBlockFullMortgage(pfrom, params)) {
        pfrom = pfrom->pprev;
    }
    if (pfrom != pto) {
        FindBlocksToDistribute(pfrom->pprev, pto, nNumOfDistributions, params, outBlockHeights);
    }
    if (outBlockHeights.size() < nNumOfDistributions) {
        outBlockHeights.insert(pfrom->nHeight);
    }
}

}  // namespace

UniValue FullMortgageBlock::ToUniValue(FullMortgageBlockMap const& mapBlocks) const {
    auto trans_func = [](std::set<int> const& heights) -> UniValue {
        UniValue outVals(UniValue::VARR);
        for (int nHeight : heights) {
            outVals.push_back(nHeight);
        }
        return outVals;
    };

    UniValue resVal(UniValue::VOBJ);
    resVal.pushKV("height", nHeight);
    resVal.pushKV("totalReward", nTotalReward);
    resVal.pushKV("blockSubsidy", nBlockSubsidy);
    resVal.pushKV("originalAccumulated", nOriginalAccumulatedToDistribute);

    CAmount nActualAccumulated = CalcActualAccumulatedAmount(mapBlocks);
    resVal.pushKV("actualAccumulated", nActualAccumulated);

    UniValue humanVal(UniValue::VOBJ);
    humanVal.pushKV("totalReward", FormatMoney(nTotalReward));
    humanVal.pushKV("blockSubsidy", FormatMoney(nBlockSubsidy));
    humanVal.pushKV("originalAccumulated", FormatMoney(nOriginalAccumulatedToDistribute));
    humanVal.pushKV("actualAccumulated", FormatMoney(nActualAccumulated));
    resVal.pushKV("forHuman", humanVal);

    resVal.pushKV("numOfDistribution", nNumOfDistribution);
    resVal.pushKV("distributedToBlocks", trans_func(vDistributedToBlocks));
    resVal.pushKV("distributedFromBlocks", trans_func(vDistributedFromBlocks));

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
        if (IsBlockFullMortgage(pcurr, m_params)) {
            // build full mortgage block
            FullMortgageBlock fmb;
            fmb.nHeight = pcurr->nHeight;
            fmb.nOriginalAccumulatedToDistribute = GetBlockAccumulateSubsidy(pcurr->pprev, m_params);
            fmb.nNumOfDistribution = GetNumOfBlocksToDistribute(pcurr);
            fmb.nTotalReward = GetBlockTotalReward(pcurr);
            fmb.nBlockSubsidy = GetBlockSubsidy(pcurr->nHeight, m_params);
            // find those blocks to distribute
            FindBlocksToDistribute(pindex, pcurr, fmb.nNumOfDistribution, m_params, fmb.vDistributedToBlocks);
            m_mapBlocks.insert(std::make_pair(pcurr->nHeight, std::move(fmb)));
        }
    }
    for (auto const& blockEntry : m_mapBlocks) {
        for (auto nHeightToDistribute : blockEntry.second.vDistributedToBlocks) {
            m_mapBlocks[nHeightToDistribute].vDistributedFromBlocks.insert(blockEntry.first);
        }
    }
}

void CMortgageCalculator::AddNewFullMortgageBlock(CBlockIndex* pindexPrev, CCoinsViewCache const& view) {
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
        if (IsBlockFullMortgage(pindexPrev, m_params)) {
            ++nDistribution;
        }
        // next
        pindexPrev = pindexPrev->pprev;
    }
    return std::max(m_params.BHDIP011MinFullMortgageBlocksToDistribute, nDistribution);
}

CAmount CMortgageCalculator::GetBlockTotalReward(CBlockIndex* pindex) const {
    // read the block from disk
    CBlock block;
    if (!ReadBlockFromDisk(block, pindex, m_params)) {
        throw std::runtime_error(tinyformat::format("cannot read block from disk, height=%d", pindex->nHeight));
    }
    // find the coinbase and get exactly the reward amount
    for (auto const& tx : block.vtx) {
        if (tx->IsCoinBase()) {
            return tx->vout[0].nValue;
            break;
        }
    }
    throw std::runtime_error(
            tinyformat::format("coinbase from block (height=%d) is not able to be found", pindex->nHeight));
}
