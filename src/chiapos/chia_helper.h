#ifndef BHD_CHIA_HELPER_H
#define BHD_CHIA_HELPER_H

#include <uint256.h>
#include <amount.h>
#include <chain.h>
#include <coins.h>

#include <primitives/transaction.h>
#include <consensus/params.h>

#include <chiapos/kernel/chiapos_types.h>

namespace chiapos::pledge {

struct MinedBlock
{
    int nHeight;
    uint256 hash;
    Bytes vchFarmerPubkey;
    CAccountID accountID;
};

struct ChainSupplyInfo
{
    int nCalcHeight;
    CAmount nAccumulate;
    CAmount nTotalSupplied;
    CAmount nBurned;
};

struct PointEntry
{
    DatacarrierType type;
    CTxDestination from;
    CTxDestination to;
    DatacarrierType originalType{0}; // it will only work with type is retarget point
    int nOriginalHeight{0}; // it will only work with type is retarget point
    CAmount nAmount;
    uint256 txid;
    uint256 blockHash;
    int64_t blockTime;
    int nHeight;
};

[[nodiscard]] CAmount get_total_supplied(int nHeight, Consensus::Params const& params);

[[nodiscard]] arith_uint256 get_netspace(CBlockIndex* pindex, Consensus::Params const& params);

[[nodiscard]] arith_uint256 get_avg_netspace(CBlockIndex* pindex, Consensus::Params const& params);

[[nodiscard]] std::vector<MinedBlock> get_blocks_mined_by_account(CAccountID const& accountID, CBlockIndex* pstartIndex, CCoinsViewCache const& view, Consensus::Params const& params);

[[nodiscard]] ChainSupplyInfo get_chain_supply_info(CBlockIndex* pindex, CCoinsViewCache const& view, Consensus::Params const& params);

[[nodiscard]] std::vector<PointEntry> enumerate_points(CCoinsViewCursorRef pcursor);

[[nodiscard]] int get_remaining_blocks(DatacarrierType type, int nPledgeHeight, int nHeight, Consensus::Params const& params);

[[nodiscard]] bool is_pledge_expired(DatacarrierType type, int nPledgeHeight, int nHeight, Consensus::Params const& params);

[[nodiscard]] CAmount calculate_actual_amount(DatacarrierType type, int nPledgeHeight, int nCurrHeight, CAmount nAmount, Consensus::Params const& params);

[[nodiscard]] CAmount calculate_actual_amount(std::vector<PointEntry> const& entries, int nHeight, Consensus::Params const& params);

} // namespace chiapos::pledge

#endif
