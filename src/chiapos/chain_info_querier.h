#ifndef BHD_CHIA_HELPER_H
#define BHD_CHIA_HELPER_H

#include <uint256.h>
#include <amount.h>
#include <chain.h>
#include <coins.h>
#include <txdb.h>

#include <primitives/transaction.h>
#include <consensus/params.h>

#include <chiapos/kernel/chiapos_types.h>

#include "attributes.h"

namespace chiapos {

struct MinedBlock {
    int nHeight;
    uint256 hash;
    Bytes vchFarmerPubkey;
    CAccountID accountID;
};

struct ChainSupplyInfo {
    int nCalcHeight;
    CAmount nAccumulate;
    CAmount nTotalSupplied;
    CAmount nBurned;
};

struct PointEntry {
    DatacarrierType type;
    CTxDestination from;
    CTxDestination to;
    DatacarrierType originalType{0};  // it will only work with type is retarget point
    int nOriginalHeight{0};           // it will only work with type is retarget point
    CAmount nAmount;
    uint256 txid;
    uint256 blockHash;
    int64_t blockTime;
    int nHeight;
};

struct PointEntries {
    std::vector<PointEntry> points;
    std::vector<PointEntry> pointT1s;
    std::vector<PointEntry> pointT2s;
    std::vector<PointEntry> pointT3s;
    std::vector<PointEntry> pointRTs;
};

class ChainInfoQuerier {
    CCoinsViewCache* m_pviewCache;
    CCoinsViewDB* m_pviewDB;
    CBlockIndex* m_pindex;
    Consensus::Params const* m_pparams;

    ChainInfoQuerier(CCoinsViewCache* pviewCache, CCoinsViewDB* pviewDB, CBlockIndex* pindex,
                     Consensus::Params const* pparams);

public:
    NODISCARD static ChainInfoQuerier CreateQuerier();

    NODISCARD arith_uint256 GetNetSpace() const;

    NODISCARD arith_uint256 GetAverageNetSpace() const;

    NODISCARD int GetTargetHeight() const;

    NODISCARD int GetPledgeCalcHeight() const;

    NODISCARD CAmount GetAccumulate() const;

    NODISCARD CAmount GetTotalSupplied() const;

    NODISCARD CAmount GetBurned() const;

    NODISCARD CAmount GetMiningRequireBalance(CAccountID accountID, int* pmined = nullptr,
                                              int* pcounted = nullptr) const;

    NODISCARD std::vector<CChiaFarmerPk> GetBoundFarmerPkList(CAccountID accountID) const;

    NODISCARD std::vector<MinedBlock> GetMinedBlockList(std::vector<CChiaFarmerPk> const& fpks) const;

    NODISCARD std::tuple<CAmount, PointEntries> GetTotalPledgedAmount(CAccountID accountID) const;

    NODISCARD CAmount GetPledgeActualAmount(DatacarrierType type, int nPledgeHeight, int nCurrHeight,
                                            CAmount nAmount) const;

    NODISCARD CAmount GetPledgeActualAmount(std::vector<PointEntry> const& entries, int nHeight) const;

    NODISCARD int GetPledgeRemainingBlocks(DatacarrierType type, int nPledgeHeight, int nHeight) const;

    NODISCARD int CheckPledgeIsExpired(DatacarrierType type, int nPledgeHeight, int nHeight) const;
};

}  // namespace chiapos

#endif
