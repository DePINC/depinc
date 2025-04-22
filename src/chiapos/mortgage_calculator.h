#ifndef BITCOIN_MORTGAGE_CALCULATOR_H
#define BITCOIN_MORTGAGE_CALCULATOR_H

#include <map>
#include <unordered_map>

#include <attributes.h>

#include <consensus/params.h>

#include <univalue.h>

class CBlockIndex;
class CCoinsViewCache;

class CMortgageCalculator {
public:
    struct FullMortgageAccumulatedInfo {
        int nHeight;
        CAmount nAccumulatedAmount;
    };
    using FullMortgageAccumulatedInfoMap = std::map<int, FullMortgageAccumulatedInfo>;

    CMortgageCalculator(CBlockIndex const* pindexTip, Consensus::Params params);

    NODISCARD std::tuple<CAmount, FullMortgageAccumulatedInfoMap> CalcAccumulatedAmount(int nTargetHeight) const;

    NODISCARD int CalcNumOfDistributions(int nHeight) const;

    NODISCARD int CalcNumOfDistributedForTargetHeight(int nDistributeFromHeight, int nTargetHeight) const;

    NODISCARD std::tuple<CAmount, CAmount> GetDistrInfo(int nDistributeFromHeight, int nTargetHeight) const;

    NODISCARD CAmount CalcDistributeAmountToTargetHeight(int nDistributeFromHeight, int nTargetHeight) const;

    NODISCARD static bool IsFullMortgageBlock(CBlockIndex const* pindex, Consensus::Params const& params);

private:
    CBlockIndex const* m_pindexTip;
    Consensus::Params m_params;
    mutable std::unordered_map<int, int> m_cached_n_distr;
};

#endif
