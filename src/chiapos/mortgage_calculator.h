#ifndef BITCOIN_MORTGAGE_CALCULATOR_H
#define BITCOIN_MORTGAGE_CALCULATOR_H

#include <attributes.h>

#include <consensus/params.h>

#include <univalue.h>

class CBlockIndex;
class CCoinsViewCache;

class CMortgageCalculator {
public:
    CMortgageCalculator(CBlockIndex const* pindexTip, Consensus::Params params);

    NODISCARD int CalcNumOfDistributions(int nHeight) const;

    NODISCARD int CalcNumOfDistributed(int nDistributeFromHeight, int nTargetHeight) const;

    NODISCARD CAmount CalcDistributeAmount(int nHeight, int nTargetHeight) const;

    NODISCARD CAmount CalcAccumulatedAmount(int nTargetHeight) const;

    NODISCARD static bool IsFullMortgageBlock(CBlockIndex const* pindex, Consensus::Params const& params);

private:
    CBlockIndex const* m_pindexTip;
    Consensus::Params m_params;
};

#endif
