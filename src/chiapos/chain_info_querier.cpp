
#include "chain_info_querier.h"

#include <subsidy_utils.h>
#include <validation.h>
#include <key_io.h>

#include <chiapos/kernel/calc_diff.h>

#include <poc/poc.h>
#include <rpc/protocol.h>

#include "post.h"

namespace chiapos {

namespace {

std::vector<PointEntry> enumerate_points(CCoinsViewCursorRef pcursor) {
    assert(pcursor != nullptr);
    std::vector<PointEntry> res;
    int nCount{0};
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

}  // namespace

ChainInfoQuerier::ChainInfoQuerier(CCoinsViewCache* pviewCache, CCoinsViewDB* pviewDB, CBlockIndex* pindex,
                                   Consensus::Params const* pparams)
        : m_pviewCache(pviewCache), m_pviewDB(pviewDB), m_pindex(pindex), m_pparams(pparams) {}

ChainInfoQuerier ChainInfoQuerier::CreateQuerier() {
    return ChainInfoQuerier(&::ChainstateActive().CoinsTip(), &::ChainstateActive().CoinsDB(), ::ChainActive().Tip(),
                            &Params().GetConsensus());
}

arith_uint256 ChainInfoQuerier::GetNetSpace() const {
    return chiapos::CalculateNetworkSpace(GetDifficultyForNextIterations(m_pindex->pprev, *m_pparams),
                                          m_pindex->chiaposFields.GetTotalIters(),
                                          m_pparams->BHDIP009DifficultyConstantFactorBits);
}

arith_uint256 ChainInfoQuerier::GetAverageNetSpace() const {
    return poc::CalculateAverageNetworkSpace(m_pindex, *m_pparams);
}

int ChainInfoQuerier::GetTargetHeight() const { return m_pindex->nHeight + 1; }

int ChainInfoQuerier::GetPledgeCalcHeight() const {
    return GetHeightForCalculatingTotalSupply(GetTargetHeight(), *m_pparams);
}

CAmount ChainInfoQuerier::GetAccumulate() const { return GetBlockAccumulateSubsidy(m_pindex, *m_pparams); }

CAmount ChainInfoQuerier::GetTotalSupplied() const {
    return GetTotalSupplyBeforeHeight(GetPledgeCalcHeight(), *m_pparams) +
           GetTotalSupplyBeforeBHDIP009(*m_pparams) * (m_pparams->BHDIP009TotalAmountUpgradeMultiply - 1);
}

CAmount ChainInfoQuerier::GetBurned() const {
    return m_pviewCache->GetAccountBalance(false, GetBurnToAccountID(), nullptr, nullptr, nullptr,
                                           &m_pparams->BHDIP009PledgeTerms, GetPledgeCalcHeight());
}

CAmount ChainInfoQuerier::GetMiningRequireBalance(CAccountID accountID, int* pmined, int* pcounted) const {
    CPlotterBindData bindData;

    return poc::GetMiningRequireBalance(accountID, bindData, GetTargetHeight(), m_pviewCache, nullptr, nullptr,
                                        GetBurned(), *m_pparams, pmined, pcounted, GetPledgeCalcHeight());
}

std::vector<CChiaFarmerPk> ChainInfoQuerier::GetBoundFarmerPkList(CAccountID accountID) const {
    std::vector<CChiaFarmerPk> pklist;
    auto fpks = m_pviewCache->GetAccountBindPlotters(accountID, CPlotterBindData::Type::CHIA);
    for (auto const& fpk : fpks) {
        pklist.push_back(fpk.GetChiaFarmerPk());
    }
    return pklist;
}

std::vector<MinedBlock> ChainInfoQuerier::GetMinedBlockList(std::vector<CChiaFarmerPk> const& fpks) const {
    auto pcurrIndex = m_pindex;
    int count{0}, mined{0};
    std::vector<MinedBlock> blks;
    while (pcurrIndex && pcurrIndex->nHeight >= m_pparams->BHDIP009Height && count < m_pparams->nCapacityEvalWindow) {
        // check fpk from the block
        for (auto const& fpk : fpks) {
            if (fpk.ToBytes() == pcurrIndex->chiaposFields.posProof.vchFarmerPk) {
                // Now we export the block to UniValue and push it to array
                MinedBlock block;
                auto dest = CTxDestination(static_cast<ScriptHash>(pcurrIndex->generatorAccountID));
                std::string accountIDStr = EncodeDestination(dest);
                block.nHeight = pcurrIndex->nHeight;
                block.hash = pcurrIndex->GetBlockHash();
                block.vchFarmerPubkey = pcurrIndex->chiaposFields.posProof.vchFarmerPk;
                block.accountID = pcurrIndex->generatorAccountID;
                blks.push_back(block);
                break;
            }
        }
        // next
        pcurrIndex = pcurrIndex->pprev;
        ++count;
    }
    return blks;
}

std::tuple<CAmount, PointEntries> ChainInfoQuerier::GetTotalPledgedAmount(CAccountID accountID) const {
    PointEntries entries;
    entries.points = enumerate_points(m_pviewDB->PointReceiveCursor(accountID, PointType::Chia));
    entries.pointT1s = enumerate_points(m_pviewDB->PointReceiveCursor(accountID, PointType::ChiaT1));
    entries.pointT2s = enumerate_points(m_pviewDB->PointReceiveCursor(accountID, PointType::ChiaT2));
    entries.pointT3s = enumerate_points(m_pviewDB->PointReceiveCursor(accountID, PointType::ChiaT3));
    entries.pointRTs = enumerate_points(m_pviewDB->PointReceiveCursor(accountID, PointType::ChiaRT));

    CAmount nPointsAmount = GetPledgeActualAmount(entries.points, m_pindex->nHeight);
    CAmount nPointsT1Amount = GetPledgeActualAmount(entries.pointT1s, m_pindex->nHeight);
    CAmount nPointsT2Amount = GetPledgeActualAmount(entries.pointT2s, m_pindex->nHeight);
    CAmount nPointsT3Amount = GetPledgeActualAmount(entries.pointT3s, m_pindex->nHeight);
    CAmount nPointsRTAmount = GetPledgeActualAmount(entries.pointRTs, m_pindex->nHeight);

    CAmount nTotalPledgeAmount = nPointsAmount + nPointsT1Amount + nPointsT2Amount + nPointsT3Amount + nPointsRTAmount;
    return std::make_tuple(nTotalPledgeAmount, entries);
}

CAmount ChainInfoQuerier::GetPledgeActualAmount(DatacarrierType type, int nPledgeHeight, int nCurrHeight,
                                                CAmount nAmount) const {
    uint16_t nIndex = type - DATACARRIER_TYPE_CHIA_POINT;
    auto const& term = m_pparams->BHDIP009PledgeTerms.at(nIndex);
    bool expired = nPledgeHeight + term.nLockHeight <= nCurrHeight;
    CAmount nActual;
    if (expired) {
        auto const& term = m_pparams->BHDIP009PledgeTerms.at(0);
        nActual = term.nWeightPercent * nAmount / 100;
    } else {
        nActual = term.nWeightPercent * nAmount / 100;
    }
    return nActual;
}

CAmount ChainInfoQuerier::GetPledgeActualAmount(std::vector<PointEntry> const& entries, int nHeight) const {
    CAmount nActualTotal{0};
    for (auto const& entry : entries) {
        // check the entry expired
        if (DatacarrierTypeIsChiaPoint(entry.type)) {
            nActualTotal += GetPledgeActualAmount(entry.type, entry.nHeight, nHeight, entry.nAmount);
        } else if (entry.type == DATACARRIER_TYPE_CHIA_POINT_RETARGET) {
            nActualTotal += GetPledgeActualAmount(entry.originalType, entry.nOriginalHeight, nHeight, entry.nAmount);
        } else {
            // TODO otherwise the entry type is invalid, need to check bug from the way to make these entries
            assert(false);
        }
    }
    return nActualTotal;
}

int ChainInfoQuerier::GetPledgeRemainingBlocks(DatacarrierType type, int nPledgeHeight, int nHeight) const {
    uint16_t nIndex = type - DATACARRIER_TYPE_CHIA_POINT;
    auto const& term = m_pparams->BHDIP009PledgeTerms.at(nIndex);
    return std::max<int>(0, (nPledgeHeight + term.nLockHeight) - nHeight);
}

int ChainInfoQuerier::CheckPledgeIsExpired(DatacarrierType type, int nPledgeHeight, int nHeight) const {
    uint16_t nIndex = type - DATACARRIER_TYPE_CHIA_POINT;
    auto const& term = m_pparams->BHDIP009PledgeTerms.at(nIndex);
    bool expired = nPledgeHeight + term.nLockHeight <= nHeight;
    return expired;
}

}  // namespace chiapos
