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

struct PointEntriesWithAmounts {
    std::vector<PointEntry> points;
    CAmount nTotalAmount;
    CAmount nActualAmount;
};

using PointEntries = std::map<DatacarrierType, PointEntriesWithAmounts>;

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

    /**
     * @brief Get total pledge details by some accountID
     *
     * @param accountID the argument represents an account
     *
     * @return <_1, _2, _3> _1: The total amount deposit on chain, _2: The actual amount, _3: details
     */
    NODISCARD std::tuple<CAmount, CAmount, PointEntries> GetTotalPledgedAmount(CAccountID accountID) const;

    NODISCARD CAmount GetPledgeActualAmount(DatacarrierType type, int nPledgeHeight, int nCurrHeight,
                                            CAmount nAmount) const;

    NODISCARD CAmount GetPledgeActualAmount(std::vector<PointEntry> const& entries, int nHeight,
                                            CAmount* pnTotalAmount = nullptr) const;

    NODISCARD int GetPledgeRemainingBlocks(DatacarrierType type, int nPledgeHeight, int nHeight) const;

    NODISCARD int CheckPledgeIsExpired(DatacarrierType type, int nPledgeHeight, int nHeight) const;
};

}  // namespace chiapos

#endif
