// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CORE_IO_H
#define BITCOIN_CORE_IO_H

#include <amount.h>
#include <attributes.h>
#include <script/standard.h>
#include <optional.h>

#include <string>
#include <vector>

class CBlock;
class CBlockHeader;
class CScript;
class CTransaction;
class CTxOut;
struct CMutableTransaction;
class uint256;
class UniValue;
class COutPoint;

using CoinAmountQuerier = std::function<Optional<CAmount>(COutPoint const& outpoint)>;

// core_read.cpp
CScript ParseScript(const std::string& s);
std::string ScriptToAsmStr(const CScript& script, const bool fAttemptSighashDecode = false);
NODISCARD bool DecodeHexTx(CMutableTransaction& tx, const std::string& hex_tx, bool try_no_witness = false, bool try_witness = true);
NODISCARD bool DecodeHexBlk(CBlock&, const std::string& strHexBlk, bool fChiapos);
bool DecodeHexBlockHeader(CBlockHeader&, const std::string& hex_header);

/**
 * Parse a hex string into 256 bits
 * @param[in] strHex a hex-formatted, 64-character string
 * @param[out] result the result of the parasing
 * @returns true if successful, false if not
 *
 * @see ParseHashV for an RPC-oriented version of this
 */
bool ParseHashStr(const std::string& strHex, uint256& result);
std::vector<unsigned char> ParseHexUV(const UniValue& v, const std::string& strName);
int ParseSighashString(const UniValue& sighash);

// core_write.cpp
UniValue ValueFromAmount(const CAmount& amount);
UniValue ValueFromCapacity(const uint64_t& capacityTB);
std::string FormatScript(const CScript& script);
std::string EncodeHexTx(const CTransaction& tx, const int serializeFlags = 0);
std::string SighashToStr(unsigned char sighash_type);
void ScriptPubKeyToUniv(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex);
void ScriptToUniv(const CScript& script, UniValue& out, bool include_address);

struct PledgeAmountsPack {
    CAmount nActualAmount;
    CAmount nTxAmount;
    int nTxHeight{0};
    int nCurrHeight{0};
    std::string strTermName;
};
void TxToUniv(const CTransaction& tx, const uint256& hashBlock, UniValue& entry, bool include_hex = true, int serialize_flags = 0, PledgeAmountsPack const& pack = {}, CoinAmountQuerier querier = {});
void DatacarrierPayloadToUniv(const CDatacarrierPayloadRef& payload, const CTxOut& txOut, PledgeAmountsPack const& heights, UniValue& out);

#endif // BITCOIN_CORE_IO_H
