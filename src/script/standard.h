// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRIPT_STANDARD_H
#define BITCOIN_SCRIPT_STANDARD_H

#include <script/interpreter.h>
#include <uint256.h>

#include <boost/variant.hpp>

#include <stdint.h>
#include <memory>
#include <set>

#include <chiapos/plotter_id.h>

static const bool DEFAULT_ACCEPT_DATACARRIER = true;

class CKeyID;
class CScript;

/** A reference to a CScript: the Hash160 of its serialization (see script.h) */
class CScriptID : public uint160
{
public:
    CScriptID() : uint160() {}
    explicit CScriptID(const CScript& in);
    CScriptID(const uint160& in) : uint160(in) {}
};

/**
 * Default setting for nMaxDatacarrierBytes. 8000 bytes of data, +1 for OP_RETURN,
 * +2 for the pushdata opcodes.
 */
static const unsigned int MAX_OP_RETURN_RELAY = 8003;

/**
 * A data carrying output is an unspendable output containing data. The script
 * type is designated as TX_NULL_DATA.
 */
extern bool fAcceptDatacarrier;

/** Maximum size of TX_NULL_DATA scripts that this node considers standard. */
extern unsigned nMaxDatacarrierBytes;

/**
 * Mandatory script verification flags that all new blocks must comply with for
 * them to be valid. (but old blocks may not comply with) Currently just P2SH,
 * but in the future other flags may be added, such as a soft-fork to enforce
 * strict DER encoding.
 *
 * Failing one of these tests may trigger a DoS ban - see CheckInputs() for
 * details.
 */
static const unsigned int MANDATORY_SCRIPT_VERIFY_FLAGS = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC;

enum txnouttype
{
    TX_NONSTANDARD,
    // 'standard' transaction types:
    TX_PUBKEY,
    TX_PUBKEYHASH,
    TX_SCRIPTHASH,
    TX_MULTISIG,
    TX_NULL_DATA, //!< unspendable OP_RETURN script that carries data
    TX_WITNESS_V0_SCRIPTHASH,
    TX_WITNESS_V0_KEYHASH,
    TX_WITNESS_UNKNOWN, //!< Only for Witness versions not already defined above
};

class CNoDestination {
public:
    friend bool operator==(const CNoDestination &a, const CNoDestination &b) { return true; }
    friend bool operator<(const CNoDestination &a, const CNoDestination &b) { return true; }
};

struct PKHash : public uint160
{
    PKHash() : uint160() {}
    explicit PKHash(const uint160& hash) : uint160(hash) {}
    explicit PKHash(const CPubKey& pubkey);
    using uint160::uint160;
};

struct WitnessV0KeyHash;
struct ScriptHash : public uint160
{
    ScriptHash() : uint160() {}
    // These don't do what you'd expect.
    // Use ScriptHash(GetScriptForDestination(...)) instead.
    explicit ScriptHash(const WitnessV0KeyHash& hash) = delete;
    explicit ScriptHash(const PKHash& hash) = delete;
    explicit ScriptHash(const uint160& hash) : uint160(hash) {}
    explicit ScriptHash(const CScript& script);
    using uint160::uint160;
};

struct WitnessV0ScriptHash : public uint256
{
    WitnessV0ScriptHash() : uint256() {}
    explicit WitnessV0ScriptHash(const uint256& hash) : uint256(hash) {}
    explicit WitnessV0ScriptHash(const CScript& script);
    using uint256::uint256;
};

struct WitnessV0KeyHash : public uint160
{
    WitnessV0KeyHash() : uint160() {}
    explicit WitnessV0KeyHash(const uint160& hash) : uint160(hash) {}
    using uint160::uint160;
};

//! CTxDestination subtype to encode any future Witness version
struct WitnessUnknown
{
    unsigned int version;
    unsigned int length;
    unsigned char program[40];

    friend bool operator==(const WitnessUnknown& w1, const WitnessUnknown& w2) {
        if (w1.version != w2.version) return false;
        if (w1.length != w2.length) return false;
        return std::equal(w1.program, w1.program + w1.length, w2.program);
    }

    friend bool operator<(const WitnessUnknown& w1, const WitnessUnknown& w2) {
        if (w1.version < w2.version) return true;
        if (w1.version > w2.version) return false;
        if (w1.length < w2.length) return true;
        if (w1.length > w2.length) return false;
        return std::lexicographical_compare(w1.program, w1.program + w1.length, w2.program, w2.program + w2.length);
    }
};

/**
 * A txout script template with a specific destination. It is either:
 *  * CNoDestination: no destination set
 *  * PKHash: TX_PUBKEYHASH destination (P2PKH)
 *  * ScriptHash: TX_SCRIPTHASH destination (P2SH)
 *  * WitnessV0ScriptHash: TX_WITNESS_V0_SCRIPTHASH destination (P2WSH)
 *  * WitnessV0KeyHash: TX_WITNESS_V0_KEYHASH destination (P2WPKH)
 *  * WitnessUnknown: TX_WITNESS_UNKNOWN destination (P2W???)
 *  A CTxDestination is the internal data type encoded in a bitcoin address
 */
typedef boost::variant<CNoDestination, PKHash, ScriptHash, WitnessV0ScriptHash, WitnessV0KeyHash, WitnessUnknown> CTxDestination;

/** Check whether a CTxDestination is a CNoDestination. */
bool IsValidDestination(const CTxDestination& dest);

/** Get the name of a txnouttype as a C string, or nullptr if unknown. */
const char* GetTxnOutputType(txnouttype t);

/**
 * Parse a scriptPubKey and identify script type for standard scripts. If
 * successful, returns script type and parsed pubkeys or hashes, depending on
 * the type. For example, for a P2SH script, vSolutionsRet will contain the
 * script hash, for P2PKH it will contain the key hash, etc.
 *
 * @param[in]   scriptPubKey   Script to parse
 * @param[out]  vSolutionsRet  Vector of parsed pubkeys and hashes
 * @return                     The script type. TX_NONSTANDARD represents a failed solve.
 */
txnouttype Solver(const CScript& scriptPubKey, std::vector<std::vector<unsigned char>>& vSolutionsRet);

/**
 * Parse a standard scriptPubKey for the destination address. Assigns result to
 * the addressRet parameter and returns true if successful. For multisig
 * scripts, instead use ExtractDestinations. Currently only works for P2PK,
 * P2PKH, P2SH, P2WPKH, and P2WSH scripts.
 */
bool ExtractDestination(const CScript& scriptPubKey, CTxDestination& addressRet);
CTxDestination ExtractDestination(const CScript& scriptPubKey);

/**
 * Parse a standard scriptPubKey with one or more destination addresses. For
 * multisig scripts, this populates the addressRet vector with the pubkey IDs
 * and nRequiredRet with the n required to spend. For other destinations,
 * addressRet is populated with a single value and nRequiredRet is set to 1.
 * Returns true if successful.
 *
 * Note: this function confuses destinations (a subset of CScripts that are
 * encodable as an address) with key identifiers (of keys involved in a
 * CScript), and its use should be phased out.
 */
bool ExtractDestinations(const CScript& scriptPubKey, txnouttype& typeRet, std::vector<CTxDestination>& addressRet, int& nRequiredRet);

/**
 * Generate a Bitcoin scriptPubKey for the given CTxDestination. Returns a P2PKH
 * script for a CKeyID destination, a P2SH script for a CScriptID, and an empty
 * script for CNoDestination.
 */
CScript GetScriptForDestination(const CTxDestination& dest);

/** Generate a P2PK script for the given pubkey. */
CScript GetScriptForRawPubKey(const CPubKey& pubkey);

/** Generate a multisig script. */
CScript GetScriptForMultisig(int nRequired, const std::vector<CPubKey>& keys);

/**
 * Generate a pay-to-witness script for the given redeem script. If the redeem
 * script is P2PK or P2PKH, this returns a P2WPKH script, otherwise it returns a
 * P2WSH script.
 *
 * TODO: replace calls to GetScriptForWitness with GetScriptForDestination using
 * the various witness-specific CTxDestination subtypes.
 */
CScript GetScriptForWitness(const CScript& redeemscript);

/** Utility function to get account ID. */
CAccountID ExtractAccountID(const CPubKey& pubkey);
CAccountID ExtractAccountID(const CScript& scriptPubKey);
CAccountID ExtractAccountID(const CTxDestination& dest);

/** opreturn type. See https://depinc.org/wiki/datacarrier */
enum DatacarrierType : unsigned int {
    // Range
    DATACARRIER_TYPE_MIN = 0x0000000f,
    DATACARRIER_TYPE_MAX = 0x10000000,

    // Alias for unknown
    DATACARRIER_TYPE_UNKNOWN = DATACARRIER_TYPE_MIN,

    // Type of consensus relevant
    //! See https://depinc.org/wiki/datacarrier/bind-plotter
    DATACARRIER_TYPE_BINDPLOTTER            = 0x00000010,
    //! See https://depinc.org/wiki/datacarrier/point
    DATACARRIER_TYPE_POINT                  = 0x00000011,
    //! See https://depinc.org/wiki/datacarrier/contract
    DATACARRIER_TYPE_CONTRACT               = 0x00000012,
    //! See https://depinc.org/wiki/datacarrier/text
    DATACARRIER_TYPE_TEXT                   = 0x00000013,
    //! Chia farmer-pk 48-byte data
    DATACARRIER_TYPE_BINDCHIAFARMER         = 0x00000014,
    //! Chia point data
    DATACARRIER_TYPE_CHIA_POINT             = 0x00000015,
    DATACARRIER_TYPE_CHIA_POINT_TERM_1      = 0x00000016,
    DATACARRIER_TYPE_CHIA_POINT_TERM_2      = 0x00000017,
    DATACARRIER_TYPE_CHIA_POINT_TERM_3      = 0x00000018,
    DATACARRIER_TYPE_CHIA_POINT_RETARGET    = 0x00000019,
};
typedef std::set<DatacarrierType> DatacarrierTypes;

inline std::string DatacarrierTypeToString(DatacarrierType type) {
    std::stringstream ss;
    switch (type) {
    case DATACARRIER_TYPE_BINDPLOTTER:
        return "DATACARRIER_TYPE_BINDPLOTTER";
    case DATACARRIER_TYPE_POINT:
        return "DATACARRIER_TYPE_POINT";
    case DATACARRIER_TYPE_CONTRACT:
        return "DATACARRIER_TYPE_CONTRACT";
    case DATACARRIER_TYPE_TEXT:
        return "DATACARRIER_TYPE_TEXT";
    case DATACARRIER_TYPE_BINDCHIAFARMER:
        return "DATACARRIER_TYPE_BINDCHIAFARMER";
    case DATACARRIER_TYPE_CHIA_POINT:
        return "DATACARRIER_TYPE_CHIA_POINT";
    case DATACARRIER_TYPE_CHIA_POINT_TERM_1:
        return "DATACARRIER_TYPE_CHIA_POINT_TERM_1";
    case DATACARRIER_TYPE_CHIA_POINT_TERM_2:
        return "DATACARRIER_TYPE_CHIA_POINT_TERM_2";
    case DATACARRIER_TYPE_CHIA_POINT_TERM_3:
        return "DATACARRIER_TYPE_CHIA_POINT_TERM_3";
    case DATACARRIER_TYPE_CHIA_POINT_RETARGET:
        return "DATACARRIER_TYPE_CHIA_POINT_RETARGET";
    default:
        ss << "DATACARRIER_TYPE_UNKNOWN(" << static_cast<int>(type) << ")";
        return ss.str();
    }
}

inline bool DatacarrierTypeIsChiaPoint(DatacarrierType type) {
    return type >= DATACARRIER_TYPE_CHIA_POINT && type <= DATACARRIER_TYPE_CHIA_POINT_TERM_3;
}

/** Datacarrier payload */
struct DatacarrierPayload
{
    DatacarrierType type;

    explicit DatacarrierPayload(DatacarrierType inType) : type(inType) {}
    virtual ~DatacarrierPayload() {}
};
typedef std::shared_ptr<DatacarrierPayload> CDatacarrierPayloadRef;

/** For bind plotter */
struct BindPlotterPayload : public DatacarrierPayload
{
public:
    explicit BindPlotterPayload(DatacarrierType inType) : DatacarrierPayload(inType) {}

    void SetId(CPlotterBindData const& inId)
    {
        if (inId.GetType() == CPlotterBindData::Type::BURST) {
            assert(type == DATACARRIER_TYPE_BINDPLOTTER);
        } else if (inId.GetType() == CPlotterBindData::Type::CHIA) {
            assert(type == DATACARRIER_TYPE_BINDCHIAFARMER);
        } else {
            throw std::runtime_error("invalid type of plotter-id in order to create `BindPlotterPayload`");
        }
        m_id = std::move(inId);
    }

    const CPlotterBindData &GetId() const { return m_id; }

    // Checkable cast for CDatacarrierPayloadRef
    static BindPlotterPayload * As(CDatacarrierPayloadRef &ref) {
        assert(ref->type == DATACARRIER_TYPE_BINDPLOTTER || ref->type == DATACARRIER_TYPE_BINDCHIAFARMER);
        return (BindPlotterPayload*) ref.get();
    }
    static const BindPlotterPayload * As(const CDatacarrierPayloadRef &ref) {
        assert(ref->type == DATACARRIER_TYPE_BINDPLOTTER || ref->type == DATACARRIER_TYPE_BINDCHIAFARMER);
        return (const BindPlotterPayload*) ref.get();
    }

private:
    CPlotterBindData m_id;
};

/** For point */
struct PointPayload : public DatacarrierPayload
{
    CAccountID receiverID;

    explicit PointPayload(DatacarrierType type) : DatacarrierPayload(type) {
        assert(type == DATACARRIER_TYPE_POINT || DatacarrierTypeIsChiaPoint(type));
    }
    const CAccountID& GetReceiverID() const { return receiverID; }

    // Checkable cast for CDatacarrierPayloadRef
    static PointPayload * As(CDatacarrierPayloadRef &ref) {
        assert(ref->type == DATACARRIER_TYPE_POINT || DatacarrierTypeIsChiaPoint(ref->type));
        return (PointPayload*) ref.get();
    }
    static const PointPayload * As(const CDatacarrierPayloadRef &ref) {
        assert(ref->type == DATACARRIER_TYPE_POINT || DatacarrierTypeIsChiaPoint(ref->type));
        return (const PointPayload*) ref.get();
    }
};

/** For retarget */
struct PointRetargetPayload : public DatacarrierPayload
{
    CAccountID receiverID;
    DatacarrierType pointType;
    int nPointHeight;

    explicit PointRetargetPayload() : DatacarrierPayload(DATACARRIER_TYPE_CHIA_POINT_RETARGET) {}

    const CAccountID& GetReceiverID() const { return receiverID; }
    DatacarrierType GetPointType() const { return pointType; }
    int GetPointHeight() const { return nPointHeight; }

    // Checkable cast for CDatacarrierPayloadRef
    static PointRetargetPayload * As(CDatacarrierPayloadRef &ref) {
        assert(ref->type == DATACARRIER_TYPE_CHIA_POINT_RETARGET);
        return (PointRetargetPayload*) ref.get();
    }
    static const PointRetargetPayload * As(const CDatacarrierPayloadRef &ref) {
        assert(ref->type == DATACARRIER_TYPE_CHIA_POINT_RETARGET);
        return (const PointRetargetPayload*) ref.get();
    }
};

/** For text */
struct TextPayload : public DatacarrierPayload
{
    std::string text;

    TextPayload() : DatacarrierPayload(DATACARRIER_TYPE_TEXT) {}
    const std::string& GetText() const { return text; }

    // Checkable cast for CDatacarrierPayloadRef
    static TextPayload * As(CDatacarrierPayloadRef &ref) {
        assert(ref->type == DATACARRIER_TYPE_TEXT);
        return (TextPayload*) ref.get();
    }
    static const TextPayload * As(const CDatacarrierPayloadRef &ref) {
        assert(ref->type == DATACARRIER_TYPE_TEXT);
        return (const TextPayload*) ref.get();
    }
};

/** The bind plotter lock amount */
static const CAmount PROTOCOL_BINDPLOTTER_LOCKAMOUNT = COIN / 10;

/** The bind plotter transaction fee */
static const CAmount PROTOCOL_BINDPLOTTER_MINFEE = COIN / 10;

/** The height for bind plotter default maximum relative tip height */
static const int PROTOCOL_BINDPLOTTER_DEFAULTMAXALIVE = 24;

/** The height for bind plotter maximum relative tip height */
static const int PROTOCOL_BINDPLOTTER_MAXALIVE = 288 * 7;

/** The bind plotter script size */
static const int PROTOCOL_BINDPLOTTER_SCRIPTSIZE = 109;

/** The bind chia-plotter script size */
static const int PROTOCOL_BINDCHIAFARMER_SCRIPTSIZE = 158;

/** Check whether a string is a valid passphrase. */
bool IsValidPassphrase(const std::string& passphrase);

/** Check whether a string is a valid plotter ID. */
bool IsValidPlotterID(const std::string& strPlotterId, uint64_t *id = nullptr);

/** Generate a bind plotter script. */
CScript GetBindPlotterScriptForDestination(const CTxDestination& dest, const std::string& passphrase, int lastActiveHeight);

CScript GetBindChiaPlotterScriptForDestination(const CTxDestination &dest,
    const chiapos::CKey &farmerSk,
    int lastActiveHeight);

/** Decode bind plotter script. */
bool DecodeBindPlotterScript(const CScript &script, uint64_t& plotterId, std::string& pubkeyHex, std::string& signatureHex, int& lastActiveHeight);
bool DecodeBindChiaFarmerScript(const CScript &script, std::string &pubkeyHex, std::string signatureHex, int &lastActiveHeight);
CPlotterBindData GetPlotterBindDataFromScript(const CScript &script);

/** The minimal point amount */
static const CAmount PROTOCOL_POINT_AMOUNT_MIN = 1 * COIN;

/** The point script size */
static const int PROTOCOL_POINT_SCRIPTSIZE = 27;

/** The point retarget script size */
static const int PROTOCOL_POINT_RETARGET_SCRIPTSIZE = 37;

/** Generate a point script. */
CScript GetPointScriptForDestination(const CTxDestination& dest, DatacarrierType type);

/** Generate a point retarget script. */
CScript GetPointRetargetScriptForDestination(const CTxDestination& dest, DatacarrierType type, int nPointHeight);

/** The text script maximum size. OP_RETURN(1) + type(5) + size(4) */
static const int PROTOCOL_TEXT_MAXSIZE = MAX_OP_RETURN_RELAY - 10;

/** Get text script */
CScript GetTextScript(const std::string& text);

/** Parse a datacarrier transaction. */
CDatacarrierPayloadRef ExtractTransactionDatacarrier(const CTransaction& tx, int nHeight = 0, const DatacarrierTypes &filters = {});
CDatacarrierPayloadRef ExtractTransactionDatacarrier(const CTransaction& tx, int nHeight, const DatacarrierTypes &filters, bool& fReject, int& lastActiveHeight, bool& fIsBindTx);

#endif // BITCOIN_SCRIPT_STANDARD_H
