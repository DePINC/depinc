// Copyright (c) 2012-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <coins.h>

#include <chainparams.h>
#include <consensus/consensus.h>
#include <logging.h>
#include <pubkey.h>
#include <random.h>
#include <version.h>
#include <key_io.h>
#include <amount.h>

#include <script/script.h>
#include <script/standard.h>

bool CCoinsView::GetCoin(const COutPoint &outpoint, Coin &coin) const { return false; }
uint256 CCoinsView::GetBestBlock() const { return uint256(); }
std::vector<uint256> CCoinsView::GetHeadBlocks() const { return std::vector<uint256>(); }
bool CCoinsView::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock) { return false; }
CCoinsViewCursorRef CCoinsView::Cursor() const { return nullptr; }
CCoinsViewCursorRef CCoinsView::Cursor(const CAccountID &accountID) const { return nullptr; }
CCoinsViewCursorRef CCoinsView::PointSendCursor(const CAccountID &accountID, PointType pt) const { return nullptr; }
CCoinsViewCursorRef CCoinsView::PointReceiveCursor(const CAccountID &accountID, PointType pt) const { return nullptr; }
CAmount CCoinsView::GetBalance(const CAccountID &accountID, const CCoinsMap &mapChildCoins, CAmount *balanceBindPlotter, CAmount *balancePointSend, CAmount *balancePointReceive, PledgeTerms const* terms, int nHeight, bool includeBurst) const {
    if (balanceBindPlotter != nullptr) *balanceBindPlotter = 0;
    if (balancePointSend != nullptr) *balancePointSend = 0;
    if (balancePointReceive != nullptr) *balancePointReceive = 0;
    return 0;
}
CBindPlotterCoinsMap CCoinsView::GetAccountBindPlotterEntries(const CAccountID &accountID, const CPlotterBindData &bindData) const { return {}; }
CBindPlotterCoinsMap CCoinsView::GetBindPlotterEntries(const CPlotterBindData &bindData) const { return {}; }
bool CCoinsView::HaveCoin(const COutPoint &outpoint) const {
    Coin coin;
    return GetCoin(outpoint, coin);
}

CCoinsViewBacked::CCoinsViewBacked(CCoinsView *viewIn) : base(viewIn) { }
bool CCoinsViewBacked::GetCoin(const COutPoint &outpoint, Coin &coin) const { return base->GetCoin(outpoint, coin); }
bool CCoinsViewBacked::HaveCoin(const COutPoint &outpoint) const { return base->HaveCoin(outpoint); }
uint256 CCoinsViewBacked::GetBestBlock() const { return base->GetBestBlock(); }
std::vector<uint256> CCoinsViewBacked::GetHeadBlocks() const { return base->GetHeadBlocks(); }
void CCoinsViewBacked::SetBackend(CCoinsView &viewIn) { base = &viewIn; }
bool CCoinsViewBacked::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock) { return base->BatchWrite(mapCoins, hashBlock); }
CCoinsViewCursorRef CCoinsViewBacked::Cursor() const { return base->Cursor(); }
CCoinsViewCursorRef CCoinsViewBacked::Cursor(const CAccountID &accountID) const { return base->Cursor(accountID); }
CCoinsViewCursorRef CCoinsViewBacked::PointSendCursor(const CAccountID &accountID, PointType pt) const { return base->PointSendCursor(accountID, pt); }
CCoinsViewCursorRef CCoinsViewBacked::PointReceiveCursor(const CAccountID &accountID, PointType pt) const { return base->PointReceiveCursor(accountID, pt); }
size_t CCoinsViewBacked::EstimateSize() const { return base->EstimateSize(); }
CAmount CCoinsViewBacked::GetBalance(const CAccountID &accountID, const CCoinsMap &mapChildCoins, CAmount *balanceBindPlotter, CAmount *balancePointSend, CAmount *balancePointReceive, PledgeTerms const* terms, int nHeight, bool includeBurst) const {
    return base->GetBalance(accountID, mapChildCoins, balanceBindPlotter, balancePointSend, balancePointReceive, terms, nHeight, includeBurst);
}
CBindPlotterCoinsMap CCoinsViewBacked::GetAccountBindPlotterEntries(const CAccountID &accountID, const CPlotterBindData &bindData) const {
    return base->GetAccountBindPlotterEntries(accountID, bindData);
}
CBindPlotterCoinsMap CCoinsViewBacked::GetBindPlotterEntries(const CPlotterBindData &bindData) const {
    return base->GetBindPlotterEntries(bindData);
}

SaltedOutpointHasher::SaltedOutpointHasher() : k0(GetRand(std::numeric_limits<uint64_t>::max())), k1(GetRand(std::numeric_limits<uint64_t>::max())) {}

CCoinsViewCache::CCoinsViewCache(CCoinsView *baseIn) : CCoinsViewBacked(baseIn), cachedCoinsUsage(0) {}

size_t CCoinsViewCache::DynamicMemoryUsage() const {
    return memusage::DynamicUsage(cacheCoins) + cachedCoinsUsage;
}

CCoinsMap::iterator CCoinsViewCache::FetchCoin(const COutPoint &outpoint) const {
    CCoinsMap::iterator it = cacheCoins.find(outpoint);
    if (it != cacheCoins.end())
        return it;
    Coin tmp;
    if (!base->GetCoin(outpoint, tmp))
        return cacheCoins.end();
    CCoinsMap::iterator ret = cacheCoins.emplace(std::piecewise_construct, std::forward_as_tuple(outpoint), std::forward_as_tuple(std::move(tmp))).first;
    if (ret->second.coin.IsSpent()) {
        assert(false); // GetCoin() only return unspent coin
        // The parent only has an empty entry for this outpoint; we can consider our
        // version as fresh.
        ret->second.flags = CCoinsCacheEntry::FRESH;
    }
    cachedCoinsUsage += ret->second.coin.DynamicMemoryUsage();
    return ret;
}

bool CCoinsViewCache::GetCoin(const COutPoint &outpoint, Coin &coin) const {
    CCoinsMap::const_iterator it = FetchCoin(outpoint);
    if (it != cacheCoins.end()) {
        coin = it->second.coin;
        return !coin.IsSpent();
    }
    return false;
}

void CCoinsViewCache::AddCoin(const COutPoint &outpoint, Coin&& coin, bool possible_overwrite) {
    assert(!coin.IsSpent());
    if (coin.out.scriptPubKey.IsUnspendable()) return;
    CCoinsMap::iterator it;
    bool inserted;
    std::tie(it, inserted) = cacheCoins.emplace(std::piecewise_construct, std::forward_as_tuple(outpoint), std::tuple<>());
    bool fresh = false;
    if (!inserted) {
        cachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();
    }
    if (!possible_overwrite) {
        if (!it->second.coin.IsSpent()) {
            throw std::logic_error("Adding new coin that replaces non-pruned entry");
        }
        // If the coin exists in this cache as a spent coin and is DIRTY, then
        // its spentness hasn't been flushed to the parent cache. We're
        // re-adding the coin to this cache now but we can't mark it as FRESH.
        // If we mark it FRESH and then spend it before the cache is flushed
        // we would remove it from this cache and would never flush spentness
        // to the parent cache.
        //
        // Re-adding a spent coin can happen in the case of a re-org (the coin
        // is 'spent' when the block adding it is disconnected and then
        // re-added when it is also added in a newly connected block).
        //
        // If the coin doesn't exist in the current cache, or is spent but not
        // DIRTY, then it can be marked FRESH.
        fresh = !(it->second.flags & CCoinsCacheEntry::DIRTY);
    }
    if (fresh && it->second.coin.IsBindPlotter())
        fresh = false;
    it->second.coin = std::move(coin);
    it->second.coin.Refresh();
    it->second.flags |= CCoinsCacheEntry::DIRTY | (fresh ? CCoinsCacheEntry::FRESH : 0);
    it->second.flags &= ~CCoinsCacheEntry::UNBIND;
    if (it->second.coin.IsBindPlotter())
        it->second.flags &= ~CCoinsCacheEntry::FRESH;
    cachedCoinsUsage += it->second.coin.DynamicMemoryUsage();
}

void AddCoins(CCoinsViewCache& cache, const CTransaction &tx, int nHeight, bool check) {
    // Parse special transaction
    CDatacarrierPayloadRef extraData;
    if (nHeight >= Params().GetConsensus().BHDIP006Height) {
        if (nHeight >= Params().GetConsensus().BHDIP009Height) {
            extraData = ExtractTransactionDatacarrier(tx, nHeight, DatacarrierTypes{DATACARRIER_TYPE_BINDCHIAFARMER,
                                                                                    DATACARRIER_TYPE_CHIA_POINT,
                                                                                    DATACARRIER_TYPE_CHIA_POINT_TERM_1,
                                                                                    DATACARRIER_TYPE_CHIA_POINT_TERM_2,
                                                                                    DATACARRIER_TYPE_CHIA_POINT_TERM_3,
                                                                                    DATACARRIER_TYPE_CHIA_POINT_RETARGET});
        } else {
            extraData = ExtractTransactionDatacarrier(tx, nHeight, DatacarrierTypes{DATACARRIER_TYPE_BINDPLOTTER, DATACARRIER_TYPE_POINT});
        }
    }

    // Add coin
    bool fCoinbase = tx.IsCoinBase();
    const uint256& txid = tx.GetHash();
    for (size_t i = 0; i < tx.vout.size(); ++i) {
        bool overwrite = check ? cache.HaveCoin(COutPoint(txid, i)) : fCoinbase;
        // Always set the possible_overwrite flag to AddCoin for coinbase txn, in order to correctly
        // deal with the pre-BIP30 occurrences of duplicate coinbase transactions.

        Coin coin(tx.vout[i], nHeight, fCoinbase);
        // Set extra data to coin of vout[0]
        if (i == 0 && extraData)
            coin.extraData = std::move(extraData);
        if (!coin.IsSpent()) {
            cache.AddCoin(COutPoint(txid, i), std::move(coin), overwrite);
        } else {
            LogPrintf("%s: Warning, a spent coin is trying to be added to coin cache, ignored. coinbase=%s, tx=%s, vout.i=%d\n", __func__,
                    (fCoinbase ? "yes" : "no"), tx.GetHash().GetHex(), i);
            for (uint32_t i = 0; i < tx.vin.size(); ++i) {
                LogPrintf("%s: dump txin[%d]=%s\n", __func__, i, tx.vin[i].ToString());
            }
        }
    }
}

bool CCoinsViewCache::SpendCoin(const COutPoint &outpoint, Coin* moveout, bool rollback) {
    CCoinsMap::iterator it = FetchCoin(outpoint);
    if (it == cacheCoins.end())
        return false;

    cachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();
    if (moveout)
        *moveout = it->second.coin;

    if (!rollback && it->second.coin.IsBindPlotter() && it->second.coin.nHeight >= Params().GetConsensus().BHDIP007Height) {
        it->second.flags |= CCoinsCacheEntry::DIRTY | CCoinsCacheEntry::UNBIND;
        it->second.flags &= ~CCoinsCacheEntry::FRESH;
        it->second.coin.Clear();
    } else if (it->second.flags & CCoinsCacheEntry::FRESH) {
        cacheCoins.erase(it);
    } else {
        it->second.flags |= CCoinsCacheEntry::DIRTY;
        it->second.flags &= ~CCoinsCacheEntry::UNBIND;
        if (it->second.coin.IsBindPlotter())
            it->second.flags &= ~CCoinsCacheEntry::FRESH;
        it->second.coin.Clear();
    }
    return true;
}

static const Coin coinEmpty;

const Coin& CCoinsViewCache::AccessCoin(const COutPoint &outpoint) const {
    CCoinsMap::const_iterator it = FetchCoin(outpoint);
    if (it == cacheCoins.end()) {
        return coinEmpty;
    } else {
        return it->second.coin;
    }
}

bool CCoinsViewCache::HaveCoin(const COutPoint &outpoint) const {
    CCoinsMap::const_iterator it = FetchCoin(outpoint);
    return (it != cacheCoins.end() && !it->second.coin.IsSpent());
}

bool CCoinsViewCache::HaveCoinInCache(const COutPoint &outpoint) const {
    CCoinsMap::const_iterator it = cacheCoins.find(outpoint);
    return (it != cacheCoins.end() && !it->second.coin.IsSpent());
}

uint256 CCoinsViewCache::GetBestBlock() const {
    if (hashBlock.IsNull())
        hashBlock = base->GetBestBlock();
    return hashBlock;
}

void CCoinsViewCache::SetBestBlock(const uint256 &hashBlockIn) {
    hashBlock = hashBlockIn;
}

bool CCoinsViewCache::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlockIn) {
    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end(); it = mapCoins.erase(it)) {
        // Ignore non-dirty entries (optimization).
        if (!(it->second.flags & CCoinsCacheEntry::DIRTY)) {
            continue;
        }
        CCoinsMap::iterator itUs = cacheCoins.find(it->first);
        if (itUs == cacheCoins.end()) {
            // The parent cache does not have an entry, while the child does
            // We can ignore it if it's both FRESH and pruned in the child
            if (!(it->second.flags & CCoinsCacheEntry::FRESH && it->second.coin.IsSpent())) {
                // Otherwise we will need to create it in the parent
                // and move the data up and mark it as dirty
                CCoinsCacheEntry& entry = cacheCoins[it->first];
                entry.coin = std::move(it->second.coin);
                cachedCoinsUsage += entry.coin.DynamicMemoryUsage();
                entry.flags = CCoinsCacheEntry::DIRTY;
                // We can mark it FRESH in the parent if it was FRESH in the child
                // Otherwise it might have just been flushed from the parent's cache
                // and already exist in the grandparent
                if (it->second.flags & CCoinsCacheEntry::FRESH) {
                    assert(!entry.coin.IsBindPlotter());
                    entry.flags |= CCoinsCacheEntry::FRESH;
                }
                // Sync UNBIND from child
                if (it->second.flags & CCoinsCacheEntry::UNBIND) {
                    assert(entry.coin.IsSpent());
                    entry.flags |= CCoinsCacheEntry::UNBIND;
                }
                if (LogAcceptCategory(BCLog::COINDB))
                    LogPrintf("%s: <%s,%3u> (height=%u spent=%d flags=%08x type=%08x) <Add new>\n", __func__,
                        it->first.hash.ToString(), it->first.n,
                        entry.coin.nHeight, entry.coin.IsSpent() ? 1 : 0, entry.flags, entry.coin.extraData ? entry.coin.extraData->type : 0);
            } else {
                if (LogAcceptCategory(BCLog::COINDB))
                    LogPrintf("%s: <%s,%3u> (height=%u spent=%d flags=%08x type=%08x) <Discard>\n", __func__,
                        it->first.hash.ToString(), it->first.n,
                        it->second.coin.nHeight, it->second.coin.IsSpent() ? 1 : 0, it->second.flags, it->second.coin.extraData ? it->second.coin.extraData->type : 0);
            }
        } else {
            // Assert that the child cache entry was not marked FRESH if the
            // parent cache entry has unspent outputs. If this ever happens,
            // it means the FRESH flag was misapplied and there is a logic
            // error in the calling code.
            if ((it->second.flags & CCoinsCacheEntry::FRESH) && !itUs->second.coin.IsSpent()) {
                throw std::logic_error("FRESH flag misapplied to cache entry for base transaction with spendable outputs");
            }

            // Found the entry in the parent cache
            if ((itUs->second.flags & CCoinsCacheEntry::FRESH) && it->second.coin.IsSpent() && !it->second.coin.IsBindPlotter()) {
                if (LogAcceptCategory(BCLog::COINDB))
                    LogPrintf("%s: <%s,%3u> (height=%u spent=%d flags=%08x type=%08x) => (height=%u spent=%d flags=%08x type=%08x) <Discard>\n", __func__,
                        it->first.hash.ToString(), it->first.n,
                        it->second.coin.nHeight, it->second.coin.IsSpent() ? 1 : 0, it->second.flags, it->second.coin.extraData ? it->second.coin.extraData->type : 0,
                        itUs->second.coin.nHeight, itUs->second.coin.IsSpent() ? 1 : 0, itUs->second.flags, itUs->second.coin.extraData ? itUs->second.coin.extraData->type : 0);
                // The grandparent does not have an entry, and the child is
                // modified and being pruned. This means we can just delete
                // it from the parent.
                cachedCoinsUsage -= itUs->second.coin.DynamicMemoryUsage();
                cacheCoins.erase(itUs);
            } else {
                if (LogAcceptCategory(BCLog::COINDB))
                    LogPrintf("%s: <%s,%3u> (height=%u spent=%d flags=%08x type=%08x) => (height=%u spent=%d flags=%08x type=%08x) <Merge>\n", __func__,
                        it->first.hash.ToString(), it->first.n,
                        it->second.coin.nHeight, it->second.coin.IsSpent() ? 1 : 0, it->second.flags, it->second.coin.extraData ? it->second.coin.extraData->type : 0,
                        itUs->second.coin.nHeight, itUs->second.coin.IsSpent() ? 1 : 0, itUs->second.flags, itUs->second.coin.extraData ? itUs->second.coin.extraData->type : 0);
                // A normal modification.
                cachedCoinsUsage -= itUs->second.coin.DynamicMemoryUsage();
                itUs->second.coin = std::move(it->second.coin);
                cachedCoinsUsage += itUs->second.coin.DynamicMemoryUsage();
                itUs->second.flags |= CCoinsCacheEntry::DIRTY;
                itUs->second.flags &= ~CCoinsCacheEntry::UNBIND;
                if (itUs->second.coin.IsBindPlotter()) {
                    itUs->second.flags &= ~CCoinsCacheEntry::FRESH;
                }
                // Sync UNBIND from child
                if (it->second.flags & CCoinsCacheEntry::UNBIND) {
                    assert(itUs->second.coin.IsSpent());
                    itUs->second.flags |= CCoinsCacheEntry::UNBIND;
                }
                // NOTE: It is possible the child has a FRESH flag here in
                // the event the entry we found in the parent is pruned. But
                // we must not copy that FRESH flag to the parent as that
                // pruned state likely still needs to be communicated to the
                // grandparent.
            }
        }
    }
    hashBlock = hashBlockIn;
    return true;
}

CAmount CCoinsViewCache::GetBalance(const CAccountID &accountID, const CCoinsMap &mapChildCoins, CAmount *balanceBindPlotter, CAmount *balancePointSend, CAmount *balancePointReceive, PledgeTerms const* terms, int nHeight, bool includeBurst) const {
    if (cacheCoins.empty()) {
        return base->GetBalance(accountID, mapChildCoins, balanceBindPlotter, balancePointSend, balancePointReceive, terms, nHeight, includeBurst);
    } else if (mapChildCoins.empty()) {
        return base->GetBalance(accountID, cacheCoins, balanceBindPlotter, balancePointSend, balancePointReceive, terms, nHeight, includeBurst);
    } else {
        CCoinsMap mapCoinsMerged;
        // Copy mine relative coins
        for (CCoinsMap::const_iterator it = cacheCoins.cbegin(); it != cacheCoins.cend(); it++) {
            if (it->second.coin.refOutAccountID != accountID) {
                // Not mine
                if ((!it->second.coin.IsPoint() || PointPayload::As(it->second.coin.extraData)->GetReceiverID() != accountID) &&
                    (!it->second.coin.IsPointRetarget() || PointRetargetPayload::As(it->second.coin.extraData)->GetReceiverID() != accountID)) {
                    // NOT debit to me
                    continue;
                }
            }
            mapCoinsMerged[it->first] = it->second;
        }
        if (mapCoinsMerged.empty()) {
            // Cannot find any coin that's related to the account(accountID)
            return base->GetBalance(accountID, mapChildCoins, balanceBindPlotter, balancePointSend, balancePointReceive, terms, nHeight, includeBurst);
        } else {
            // Merge child and mine coins
            // See CCoinsViewCache::BatchWrite()
            for (CCoinsMap::const_iterator it = mapChildCoins.cbegin(); it != mapChildCoins.cend(); it++) {
                if (!(it->second.flags & CCoinsCacheEntry::DIRTY)) {
                    // Skip the `fresh` entries
                    continue;
                }
                if (it->second.coin.refOutAccountID != accountID) {
                    // NOT mine
                    if ((!it->second.coin.IsPoint() || PointPayload::As(it->second.coin.extraData)->GetReceiverID() != accountID) &&
                        (!it->second.coin.IsPointRetarget() || PointRetargetPayload::As(it->second.coin.extraData)->GetReceiverID() != accountID)) {
                        // NOT debit to me
                        continue;
                    }
                }
                CCoinsMap::iterator itUs = mapCoinsMerged.find(it->first);
                if (itUs == mapCoinsMerged.end()) {
                    if (!(it->second.flags & CCoinsCacheEntry::FRESH && it->second.coin.IsSpent())) {
                        CCoinsCacheEntry& entry = mapCoinsMerged[it->first]; // Insert a new record `entry` into `mapCoinsMerged`
                        entry.coin = it->second.coin;
                        entry.flags = CCoinsCacheEntry::DIRTY;
                        if (it->second.flags & CCoinsCacheEntry::FRESH) {
                            assert(!entry.coin.IsBindPlotter());
                            entry.flags |= CCoinsCacheEntry::FRESH;
                        }
                        if (it->second.flags & CCoinsCacheEntry::UNBIND) {
                            assert(entry.coin.IsSpent());
                            entry.flags |= CCoinsCacheEntry::UNBIND;
                        }
                    }
                } else {
                    if ((it->second.flags & CCoinsCacheEntry::FRESH) && !itUs->second.coin.IsSpent()) {
                        throw std::logic_error("FRESH flag misapplied to cache entry for base transaction with spendable outputs");
                    }
                    if ((itUs->second.flags & CCoinsCacheEntry::FRESH) && it->second.coin.IsSpent() && !it->second.coin.IsBindPlotter()) {
                        mapCoinsMerged.erase(itUs); // Remove the coin from `mapCoinsMerged` casue the coin is spent
                    } else {
                        itUs->second.coin = it->second.coin;
                        itUs->second.flags |= CCoinsCacheEntry::DIRTY; // +DIRTY
                        itUs->second.flags &= ~CCoinsCacheEntry::UNBIND; // -UNBIND
                        if (itUs->second.coin.IsBindPlotter()) {
                            itUs->second.flags &= ~CCoinsCacheEntry::FRESH; // -FRESH
                        }
                        if (it->second.flags & CCoinsCacheEntry::UNBIND) {
                            assert(itUs->second.coin.IsSpent());
                            itUs->second.flags |= CCoinsCacheEntry::UNBIND; // +UNBIND
                        }
                    }
                }
            }
            return base->GetBalance(accountID, mapCoinsMerged, balanceBindPlotter, balancePointSend, balancePointReceive, terms, nHeight, includeBurst);
        }
    }
}

CBindPlotterCoinsMap CCoinsViewCache::GetAccountBindPlotterEntries(const CAccountID &accountID, const CPlotterBindData &bindData) const {
    // From base view
    CBindPlotterCoinsMap outpoints = base->GetAccountBindPlotterEntries(accountID, bindData);

    // Apply modified
    for (CCoinsMap::iterator it = cacheCoins.begin(); it != cacheCoins.end(); it++) {
        if (!(it->second.flags & CCoinsCacheEntry::DIRTY))
            continue;

        if (accountID != it->second.coin.refOutAccountID) {
            outpoints.erase(it->first);
            continue;
        }

        auto itSelected = outpoints.find(it->first);
        if (itSelected != outpoints.end()) {
            if (it->second.coin.IsSpent() && !(it->second.flags & CCoinsCacheEntry::UNBIND)) {
                outpoints.erase(itSelected);
            } else if (it->second.coin.IsBindPlotter()) {
                if (bindData.IsZero() || bindData == BindPlotterPayload::As(it->second.coin.extraData)->GetId()) {
                    itSelected->second.nHeight = it->second.coin.nHeight;
                    itSelected->second.accountID = it->second.coin.refOutAccountID;
                    itSelected->second.bindData = BindPlotterPayload::As(it->second.coin.extraData)->GetId();
                    itSelected->second.valid = !it->second.coin.IsSpent();
                } else {
                    outpoints.erase(itSelected);
                }
            } else {
                outpoints.erase(itSelected);
            }
        } else {
            if (it->second.coin.IsBindPlotter() && (bindData.IsZero() || bindData == BindPlotterPayload::As(it->second.coin.extraData)->GetId())) {
                if (!it->second.coin.IsSpent() || (it->second.flags & CCoinsCacheEntry::UNBIND)) {
                    CBindPlotterCoinInfo &info = outpoints[it->first];
                    info.nHeight = it->second.coin.nHeight;
                    info.accountID = it->second.coin.refOutAccountID;
                    info.bindData = BindPlotterPayload::As(it->second.coin.extraData)->GetId();
                    info.valid = !it->second.coin.IsSpent();
                }
            }
        }
    }

    return outpoints;
}

CBindPlotterCoinsMap CCoinsViewCache::GetBindPlotterEntries(const CPlotterBindData &bindData) const {
    // From base view
    CBindPlotterCoinsMap outpoints = base->GetBindPlotterEntries(bindData);

    // Apply modified
    for (CCoinsMap::iterator it = cacheCoins.begin(); it != cacheCoins.end(); it++) {
        if (!(it->second.flags & CCoinsCacheEntry::DIRTY))
            continue;

        auto itSelected = outpoints.find(it->first);
        if (itSelected != outpoints.end()) {
            if (it->second.coin.IsSpent() && !(it->second.flags & CCoinsCacheEntry::UNBIND)) {
                outpoints.erase(itSelected);
            } else if (it->second.coin.IsBindPlotter()) {
                if (bindData == BindPlotterPayload::As(it->second.coin.extraData)->GetId()) {
                    itSelected->second.nHeight = it->second.coin.nHeight;
                    itSelected->second.accountID = it->second.coin.refOutAccountID;
                    itSelected->second.bindData = BindPlotterPayload::As(it->second.coin.extraData)->GetId();
                    itSelected->second.valid = !it->second.coin.IsSpent();
                } else {
                    outpoints.erase(itSelected);
                }
            } else {
                outpoints.erase(itSelected);
            }
        } else {
            if (it->second.coin.IsBindPlotter() && bindData == BindPlotterPayload::As(it->second.coin.extraData)->GetId()) {
                if (!it->second.coin.IsSpent() || (it->second.flags & CCoinsCacheEntry::UNBIND)) {
                    CBindPlotterCoinInfo &info = outpoints[it->first];
                    info.nHeight = it->second.coin.nHeight;
                    info.accountID = it->second.coin.refOutAccountID;
                    info.bindData = BindPlotterPayload::As(it->second.coin.extraData)->GetId();
                    info.valid = !it->second.coin.IsSpent();
                }
            }
        }
    }

    return outpoints;
}

CAmount CCoinsViewCache::GetAccountBalance(bool includeBurst, const CAccountID &accountID, CAmount *balanceBindPlotter, CAmount *balancePointSend, CAmount *balancePointReceive, PledgeTerms const* terms, int nHeight) const {
    // Merge to parent
    return base->GetBalance(accountID, cacheCoins, balanceBindPlotter, balancePointSend, balancePointReceive, terms, nHeight, includeBurst);
}

CBindPlotterInfo CCoinsViewCache::GetChangeBindPlotterInfo(const CBindPlotterInfo &sourceBindInfo, bool compatible) const {
    assert(!sourceBindInfo.outpoint.IsNull());

    CBindPlotterInfo changeBindInfo;
    if (compatible) {
        // Compatible BHDIP007 before. Use last active coin
        for (const auto& pair : GetBindPlotterEntries(sourceBindInfo.bindData)) {
            if (!pair.second.valid ||
                    (pair.first == sourceBindInfo.outpoint) ||
                    (pair.second.nHeight < sourceBindInfo.nHeight) ||
                    (pair.second.nHeight == sourceBindInfo.nHeight && pair.first < sourceBindInfo.outpoint))
                continue;

            // Select smallest bind
            if (changeBindInfo.nHeight < pair.second.nHeight ||
                    (changeBindInfo.nHeight == pair.second.nHeight && changeBindInfo.outpoint < pair.first))
                changeBindInfo = CBindPlotterInfo(pair);
        }
    } else {
        changeBindInfo.nHeight = 0x7fffffff;
        for (const auto& pair : GetBindPlotterEntries(sourceBindInfo.bindData)) {
            if ((pair.first == sourceBindInfo.outpoint) ||
                    (pair.second.nHeight < sourceBindInfo.nHeight) ||
                    (pair.second.nHeight == sourceBindInfo.nHeight && pair.first < sourceBindInfo.outpoint))
                continue;

            // Select smallest bind
            if (changeBindInfo.nHeight > pair.second.nHeight ||
                    (changeBindInfo.nHeight == pair.second.nHeight && pair.first < changeBindInfo.outpoint))
                changeBindInfo = CBindPlotterInfo(pair);
        }
    }
    return changeBindInfo.outpoint.IsNull() ? sourceBindInfo : changeBindInfo;
}

CBindPlotterInfo CCoinsViewCache::GetLastBindPlotterInfo(const CPlotterBindData &bindData) const {
    CBindPlotterInfo lastBindInfo;
    for (const auto& pair : GetBindPlotterEntries(bindData)) {
        assert(pair.second.bindData == bindData);
        LogPrint(BCLog::COINDB, "%s: queried bind-data valid(%s) height(%d) account(%s) id(%s)\n",
                __func__, (pair.second.valid ? "true" : "false"), pair.second.nHeight,
                EncodeDestination(ScriptHash(pair.second.accountID)), pair.second.bindData.ToString());
        if (lastBindInfo.outpoint.IsNull() ||
                (lastBindInfo.nHeight < pair.second.nHeight) ||
                (lastBindInfo.nHeight == pair.second.nHeight && lastBindInfo.outpoint < pair.first)) {
            lastBindInfo = CBindPlotterInfo(pair);
            LogPrint(BCLog::COINDB, "%s: accept height(%d) account(%s) id(%s)\n", __func__, pair.second.nHeight,
                    EncodeDestination(ScriptHash(pair.second.accountID)), pair.second.bindData.ToString());
        }
    }
    return lastBindInfo;
}

const Coin& CCoinsViewCache::GetLastBindPlotterCoin(const CPlotterBindData &bindData, COutPoint *outpoint) const {
    CBindPlotterInfo lastBindInfo = GetLastBindPlotterInfo(bindData);
    if (outpoint) *outpoint = lastBindInfo.outpoint;
    if (!lastBindInfo.valid)
        return coinEmpty;

    const Coin& coin = AccessCoin(lastBindInfo.outpoint);
    assert(!coin.IsSpent());
    assert(coin.IsBindPlotter());
    assert(BindPlotterPayload::As(coin.extraData)->GetId() == bindData);
    return coin;
}

bool CCoinsViewCache::HaveActiveBindPlotter(const CAccountID &accountID, const CPlotterBindData &bindData) const {
    CBindPlotterInfo lastBindInfo = GetLastBindPlotterInfo(bindData);
    bool res = lastBindInfo.valid && lastBindInfo.accountID == accountID;
    if (!res) {
        LogPrint(BCLog::POC, "%s: warning - bind plotter(%s) account(%s) id(%s) can not be found\n", __func__,
                CPlotterBindData::TypeToString(bindData.GetType()), EncodeDestination(ScriptHash(accountID)),
                bindData.ToString());
        return false;
    }
    return true;
}

std::set<CPlotterBindData> CCoinsViewCache::GetAccountBindPlotters(const CAccountID &accountID, CPlotterBindData::Type bindType) const {
    std::set<CPlotterBindData> plotters;
    auto getEntriesByDefaultBindType = [this](const CAccountID &accountID, CPlotterBindData::Type bindType) {
        if (bindType == CPlotterBindData::Type::BURST) {
            return GetAccountBindPlotterEntries(accountID, CPlotterBindData(0));
        } else if (bindType == CPlotterBindData::Type::CHIA) {
            return GetAccountBindPlotterEntries(accountID, CPlotterBindData(CChiaFarmerPk()));
        }
        throw std::runtime_error("cannot construct a bind-data with an unknown type");
    };
    for (const auto& pair: getEntriesByDefaultBindType(accountID, bindType)) {
        if (pair.second.valid)
            plotters.insert(pair.second.bindData);
    }
    return plotters;
}

bool CCoinsViewCache::Flush() {
    bool fOk = base->BatchWrite(cacheCoins, hashBlock);
    cacheCoins.clear();
    cachedCoinsUsage = 0;
    return fOk;
}

void CCoinsViewCache::Uncache(const COutPoint& hash)
{
    CCoinsMap::iterator it = cacheCoins.find(hash);
    if (it != cacheCoins.end() && it->second.flags == 0) {
        if (it->second.coin.refOutAccountID != GetBurnToAccountID()) {
            cachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();
            cacheCoins.erase(it);
        }
    }
}

unsigned int CCoinsViewCache::GetCacheSize() const {
    return cacheCoins.size();
}

CAmount CCoinsViewCache::GetValueIn(const CTransaction& tx) const
{
    if (tx.IsCoinBase())
        return 0;

    CAmount nResult = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
        nResult += AccessCoin(tx.vin[i].prevout).out.nValue;

    return nResult;
}

bool CCoinsViewCache::HaveInputs(const CTransaction& tx) const
{
    if (!tx.IsCoinBase()) {
        for (unsigned int i = 0; i < tx.vin.size(); i++) {
            if (!HaveCoin(tx.vin[i].prevout)) {
                return false;
            }
        }
    }
    return true;
}

static const size_t MIN_TRANSACTION_OUTPUT_WEIGHT = WITNESS_SCALE_FACTOR * ::GetSerializeSize(CTxOut(), 0, PROTOCOL_VERSION);
static const size_t MAX_OUTPUTS_PER_BLOCK = MAX_BLOCK_WEIGHT / MIN_TRANSACTION_OUTPUT_WEIGHT;

const Coin& AccessByTxid(const CCoinsViewCache& view, const uint256& txid)
{
    COutPoint iter(txid, 0);
    while (iter.n < MAX_OUTPUTS_PER_BLOCK) {
        const Coin& alternate = view.AccessCoin(iter);
        if (!alternate.IsSpent()) return alternate;
        ++iter.n;
    }
    return coinEmpty;
}

bool CCoinsViewErrorCatcher::GetCoin(const COutPoint &outpoint, Coin &coin) const {
    try {
        return CCoinsViewBacked::GetCoin(outpoint, coin);
    } catch(const std::runtime_error& e) {
        for (auto f : m_err_callbacks) {
            f();
        }
        LogPrintf("Error reading from database: %s\n", e.what());
        // Starting the shutdown sequence and returning false to the caller would be
        // interpreted as 'entry not found' (as opposed to unable to read data), and
        // could lead to invalid interpretation. Just exit immediately, as we can't
        // continue anyway, and all writes should be atomic.
        std::abort();
    }
}
