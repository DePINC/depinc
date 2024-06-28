#ifndef BITCOIN_MORTGAGE_CALCULATOR_H
#define BITCOIN_MORTGAGE_CALCULATOR_H

#include <attributes.h>

#include <consensus/params.h>

#include <univalue.h>

class CBlockIndex;
class CCoinsViewCache;

struct FullMortgageBlock;
using FullMortgageBlockMap = std::map<int, FullMortgageBlock>;

struct FullMortgageBlock {
    int nHeight{0};
    CAmount nOriginalAccumulatedToDistribute{0};
    int nNumOfDistribution{0};  // number of full mortgage blocks from previous 3360 blocks
    std::set<int> vDistributedToBlocks;
    std::set<int> vDistributedFromBlocks;

    NODISCARD CAmount GetDistributeAmount() const { return nOriginalAccumulatedToDistribute / nNumOfDistribution; }

    NODISCARD bool IsDistributionFinished() const { return vDistributedToBlocks.size() == nNumOfDistribution; }

    NODISCARD CAmount CalcActualAccumulatedAmount(FullMortgageBlockMap const& mapBlocks) const {
        CAmount nActualAccumulated{0};
        for (int nFromHeight : vDistributedFromBlocks) {
            auto iter = mapBlocks.find(nFromHeight);
            assert(iter != std::cend(mapBlocks));
            nActualAccumulated += iter->second.GetDistributeAmount();
        }
        return nActualAccumulated;
    }

    NODISCARD UniValue ToUniValue(FullMortgageBlockMap const& mapBlocks) const;
};

class CMortgageCalculator {
public:
    explicit CMortgageCalculator(Consensus::Params params);

    NODISCARD bool IsEmpty() const;

    /**
     * @brief Build data for mortgage calculator
     *
     * @param pindex Current top block
     * @param cache Coins view
     */
    void Build(CBlockIndex* pindex);

    /**
     * @brief A new full mortgage block will be released, now add the information of the new block and calculate related
     * data
     *
     * @param pindexPrev Last top block
     * @param view Coins view
     */
    void AddNewFullMortgageBlock(CBlockIndex* pindexPrev, CCoinsViewCache const& view);

    /**
     * @brief Get the Actual Accumulated For Block Height object
     *
     * @param nHeight The height to calculate accumulated amount
     *
     * @return The calculated amount
     */
    NODISCARD CAmount GetActualAccumulatedForBlockHeight(int nHeight) const;

    /**
     * @brief Return the full map
     *
     * @return The full map
     */
    NODISCARD FullMortgageBlockMap const& GetMap() const;

private:
    NODISCARD int GetNumOfBlocksToDistribute(CBlockIndex* pindexFullMortgage) const;

    Consensus::Params m_params;
    FullMortgageBlockMap m_mapBlocks;
};

#endif
