// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_UNDO_H
#define BITCOIN_UNDO_H

#include <coins.h>
#include <compressor.h>
#include <consensus/consensus.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <serialize.h>
#include <version.h>

/** Undo information for a CTxIn
 *
 *  Contains the prevout's CTxOut being spent, and its metadata as well
 *  (coinbase or not, height). The serialization contains a dummy value of
 *  zero. This is compatible with older versions which expect to see
 *  the transaction version there.
 */
class TxInUndoSerializer
{
    const Coin* txout;

public:
    template<typename Stream>
    void Serialize(Stream &s) const {
        unsigned int nCode = (txout->extraData ? 0x80000000 : 0) | (txout->nHeight * 2 + (txout->fCoinBase ? 1u : 0u));
        ::Serialize(s, VARINT(nCode));
        if (txout->nHeight > 0) {
            // Required to maintain compatibility with older undo format.
            ::Serialize(s, (unsigned char)0);
        }
        ::Serialize(s, CTxOutCompressor(REF(txout->out)));

        if (nCode & 0x80000000) {
            ::Serialize(s, VARINT((unsigned int&)txout->extraData->type));
            if (txout->extraData->type == DATACARRIER_TYPE_BINDPLOTTER) {
                ::Serialize(s, VARINT(BindPlotterPayload::As(txout->extraData)->GetId().GetBurstPlotterId()));
            } else if (txout->extraData->type == DATACARRIER_TYPE_BINDCHIAFARMER) {
                ::Serialize(s, BindPlotterPayload::As(txout->extraData)->GetId().GetChiaFarmerPk());
            } else if (txout->extraData->type == DATACARRIER_TYPE_POINT || DatacarrierTypeIsChiaPoint(txout->extraData->type)) {
                ::Serialize(s, REF(PointPayload::As(txout->extraData)->GetReceiverID()));
            } else if (txout->extraData->type == DATACARRIER_TYPE_CHIA_POINT_RETARGET) {
                ::Serialize(s, REF(PointRetargetPayload::As(txout->extraData)->GetReceiverID()));
            } else
                assert(false);
        }
    }

    explicit TxInUndoSerializer(const Coin* coin) : txout(coin) {}
};

class TxInUndoDeserializer
{
    Coin* txout;

public:
    template<typename Stream>
    void Unserialize(Stream &s) {
        unsigned int nCode = 0;
        ::Unserialize(s, VARINT(nCode));
        txout->nHeight = (nCode & 0x7fffffff) / 2;
        txout->fCoinBase = nCode & 1;
        if (txout->nHeight > 0) {
            // Old versions stored the version number for the last spend of
            // a transaction's outputs. Non-final spends were indicated with
            // height = 0.
            unsigned int nVersionDummy{0};
            ::Unserialize(s, VARINT(nVersionDummy));
        }
        ::Unserialize(s, CTxOutCompressor(REF(txout->out)));

        txout->Refresh();
        txout->extraData = nullptr;
        if (nCode & 0x80000000) {
            unsigned int extraDataType = 0;
            ::Unserialize(s, VARINT(extraDataType));
            if (extraDataType == DATACARRIER_TYPE_BINDPLOTTER) {
                txout->extraData = std::make_shared<BindPlotterPayload>(DATACARRIER_TYPE_BINDPLOTTER);
                uint64_t nPlotterId{0};
                ::Unserialize(s, VARINT(nPlotterId));
                BindPlotterPayload::As(txout->extraData)->SetId(CPlotterBindData(nPlotterId));
            } else if (extraDataType == DATACARRIER_TYPE_BINDCHIAFARMER) {
                txout->extraData = std::make_shared<BindPlotterPayload>(DATACARRIER_TYPE_BINDCHIAFARMER);
                CChiaFarmerPk pk;
                ::Unserialize(s, pk);
                BindPlotterPayload::As(txout->extraData)->SetId(CPlotterBindData(pk));
            } else if (extraDataType == DATACARRIER_TYPE_POINT || DatacarrierTypeIsChiaPoint((DatacarrierType)extraDataType)) {
                txout->extraData = std::make_shared<PointPayload>(static_cast<DatacarrierType>(extraDataType));
                ::Unserialize(s, REF(PointPayload::As(txout->extraData)->GetReceiverID()));
            } else if (extraDataType == DATACARRIER_TYPE_CHIA_POINT_RETARGET) {
                txout->extraData = std::make_shared<PointRetargetPayload>();
                ::Unserialize(s, REF(PointRetargetPayload::As(txout->extraData)->GetReceiverID()));
            } else
                assert(false);
        }
    }

    explicit TxInUndoDeserializer(Coin* coin) : txout(coin) {}
};

static const size_t MIN_TRANSACTION_INPUT_WEIGHT = WITNESS_SCALE_FACTOR * ::GetSerializeSize(CTxIn(), 0, PROTOCOL_VERSION);
static const size_t MAX_INPUTS_PER_BLOCK = MAX_BLOCK_WEIGHT / MIN_TRANSACTION_INPUT_WEIGHT;

/** Undo information for a CTransaction */
class CTxUndo
{
public:
    // undo information for all txins
    std::vector<Coin> vprevout;

    template <typename Stream>
    void Serialize(Stream& s) const {
        // TODO: avoid reimplementing vector serializer
        uint64_t count = vprevout.size();
        ::Serialize(s, COMPACTSIZE(REF(count)));
        for (const auto& prevout : vprevout) {
            ::Serialize(s, TxInUndoSerializer(&prevout));
        }
    }

    template <typename Stream>
    void Unserialize(Stream& s) {
        // TODO: avoid reimplementing vector deserializer
        uint64_t count = 0;
        ::Unserialize(s, COMPACTSIZE(count));
        if (count > MAX_INPUTS_PER_BLOCK) {
            throw std::ios_base::failure("Too many input undo records");
        }
        vprevout.resize(count);
        for (auto& prevout : vprevout) {
            ::Unserialize(s, TxInUndoDeserializer(&prevout));
        }
    }
};

/** Undo information for a CBlock */
class CBlockUndo
{
public:
    std::vector<CTxUndo> vtxundo; // for all but the coinbase

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(vtxundo);
    }
};

#endif // BITCOIN_UNDO_H
