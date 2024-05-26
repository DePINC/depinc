
#include "chia_helper.h"

#include <subsidy_utils.h>
#include <validation.h>
#include <key_io.h>

#include <chiapos/kernel/calc_diff.h>

#include <poc/poc.h>
#include <rpc/protocol.h>

#include "post.h"

namespace chiapos::pledge {

CAmount get_total_supplied(int nHeight, Consensus::Params const& params)
{
    return GetTotalSupplyBeforeBHDIP009(params) * (params.BHDIP009TotalAmountUpgradeMultiply - 1) + GetTotalSupplyBeforeHeight(nHeight, params);
}

arith_uint256 get_netspace(CBlockIndex* pindex, Consensus::Params const& params)
{
    return chiapos::CalculateNetworkSpace(GetDifficultyForNextIterations(pindex->pprev, params), pindex->chiaposFields.GetTotalIters(), params.BHDIP009DifficultyConstantFactorBits);
}

arith_uint256 get_avg_netspace(CBlockIndex* pindex, Consensus::Params const& params)
{
    return poc::CalculateAverageNetworkSpace(pindex, params);
}

std::vector<MinedBlock> get_blocks_mined_by_account(CAccountID const& accountID, CBlockIndex* pstartIndex, CCoinsViewCache const& view, Consensus::Params const& params)
{
    std::vector<MinedBlock> res;

    auto pcurrIndex = pstartIndex;
    int count{0}, mined{0};
    auto fpks = view.GetAccountBindPlotters(accountID, CPlotterBindData::Type::CHIA);
    while (pcurrIndex && pcurrIndex->nHeight >= params.BHDIP009Height && count < params.nCapacityEvalWindow) {
        // check fpk from the block
        auto it = std::find_if(std::cbegin(fpks), std::cend(fpks), [&pcurrIndex](CPlotterBindData const& fpk) -> bool {
            return fpk.GetChiaFarmerPk().ToBytes() == pcurrIndex->chiaposFields.posProof.vchFarmerPk;
        });
        if (it != std::cend(fpks)) {
            MinedBlock mb;
            mb.nHeight = pcurrIndex->nHeight;
            mb.hash = pcurrIndex->GetBlockHash();
            mb.vchFarmerPubkey = pcurrIndex->chiaposFields.posProof.vchFarmerPk;
            res.push_back(std::move(mb));
            ++mined;
        }
        // next
        pcurrIndex = pcurrIndex->pprev;
        ++count;
    }

    return res;
}

ChainSupplyInfo get_chain_supply_info(CBlockIndex* pindex, CCoinsViewCache const& view, Consensus::Params const& params)
{
    ChainSupplyInfo res;

    int nTargetHeight = pindex->nHeight + 1;
    res.nCalcHeight = GetHeightForCalculatingTotalSupply(nTargetHeight, params);

    res.nAccumulate = GetBlockAccumulateSubsidy(pindex, params);
    res.nTotalSupplied = GetTotalSupplyBeforeHeight(res.nCalcHeight, params) +
                             GetTotalSupplyBeforeBHDIP009(params) * (params.BHDIP009TotalAmountUpgradeMultiply - 1);
    res.nBurned = view.GetAccountBalance(false, GetBurnToAccountID(), nullptr, nullptr, nullptr,
                                             &params.BHDIP009PledgeTerms, res.nCalcHeight);
    return res;
}

std::vector<PointEntry> enumerate_points(CCoinsViewCursorRef pcursor) {
    assert(pcursor != nullptr);
    std::vector<PointEntry> res;
    int nCount { 0 };
    for (; pcursor->Valid(); pcursor->Next()) {
        COutPoint key;
        Coin coin;
        if (pcursor->GetKey(key) && pcursor->GetValue(coin)) {
            assert(key.n == 0);
            assert(!coin.IsSpent());
            assert(coin.IsChiaPointRelated());

            PointEntry entry;
            entry.type = coin.GetExtraDataType();
            entry.from = ExtractDestination(coin.out.scriptPubKey);
            if (coin.IsPointRetarget()) {
                auto retargetPayload = PointRetargetPayload::As(coin.extraData);
                entry.to = CTxDestination(ScriptHash(retargetPayload->GetReceiverID()));
                entry.originalType = retargetPayload->GetPointType();
                entry.nOriginalHeight = retargetPayload->GetPointHeight();
            } else {
                entry.to = CTxDestination(ScriptHash(PointPayload::As(coin.extraData)->GetReceiverID()));
            }
            entry.nAmount = coin.out.nValue;
            entry.txid = key.hash;
            entry.blockHash = ::ChainActive()[(int)coin.nHeight]->GetBlockHash();
            entry.blockTime = ::ChainActive()[(int)coin.nHeight]->GetBlockTime();
            entry.nHeight = coin.nHeight;
            res.push_back(std::move(entry));
            ++nCount;
        } else {
            throw std::runtime_error("Unable to read UTXO set");
        }
    }
    return res;
}

int get_remaining_blocks(DatacarrierType type, int nPledgeHeight, int nHeight, Consensus::Params const& params)
{
    uint16_t nIndex = type - DATACARRIER_TYPE_CHIA_POINT;
    auto const& term = params.BHDIP009PledgeTerms.at(nIndex);
    return std::max<int>(0, (nPledgeHeight + term.nLockHeight) - nHeight);
}

bool is_pledge_expired(DatacarrierType type, int nPledgeHeight, int nHeight, Consensus::Params const& params)
{
    uint16_t nIndex = type - DATACARRIER_TYPE_CHIA_POINT;
    auto const& term = params.BHDIP009PledgeTerms.at(nIndex);
    bool expired = nPledgeHeight + term.nLockHeight <= nHeight;
    return expired;
 }

CAmount calculate_actual_amount(DatacarrierType type, int nPledgeHeight, int nCurrHeight, CAmount nAmount, Consensus::Params const& params)
{
    uint16_t nIndex = type - DATACARRIER_TYPE_CHIA_POINT;
    auto const& term = params.BHDIP009PledgeTerms.at(nIndex);
    bool expired = nPledgeHeight + term.nLockHeight <= nCurrHeight;
    CAmount nActual;
    if (expired) {
        auto const& term = params.BHDIP009PledgeTerms.at(0);
        nActual = term.nWeightPercent * nAmount / 100;
    } else {
        nActual = term.nWeightPercent * nAmount / 100;
    }
    return nActual;
}

CAmount calculate_actual_amount(std::vector<PointEntry> const& entries, int nHeight, Consensus::Params const& params)
{
    CAmount nActualTotal{0};
    for (auto const& entry : entries) {
        // check the entry expired
        if (DatacarrierTypeIsChiaPoint(entry.type)) {
            nActualTotal += calculate_actual_amount(entry.type, entry.nHeight, nHeight, entry.nAmount, params);;
        } else if (entry.type == DATACARRIER_TYPE_CHIA_POINT_RETARGET) {
            nActualTotal += calculate_actual_amount(entry.originalType, entry.nOriginalHeight, nHeight, entry.nAmount, params);
        } else {
            // TODO otherwise the entry type is invalid, need to check bug from the way to make these entries
            assert(false);
        }
    }
    return nActualTotal;
}

} // namespace chiapos::pledge
