// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPC_REGISTER_H
#define BITCOIN_RPC_REGISTER_H

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

/** These are in one header file to avoid creating tons of single-function
 * headers for everything under src/rpc/ */
class CRPCTable;

/** Register block chain RPC commands */
void RegisterBlockchainRPCCommands(CRPCTable &tableRPC);
/** Register P2P networking RPC commands */
void RegisterNetRPCCommands(CRPCTable &tableRPC);
/** Register miscellaneous RPC commands */
void RegisterMiscRPCCommands(CRPCTable &tableRPC);
/** Register mining RPC commands */
void RegisterMiningRPCCommands(CRPCTable &tableRPC);
/** Register raw transaction RPC commands */
void RegisterRawTransactionRPCCommands(CRPCTable &tableRPC);
/** Register PoC RPC commands */
void RegisterPoCRPCCommands(CRPCTable &tableRPC);
/** Register Chia RPC commands */
namespace chiapos {
void RegisterChiaRPCCommands(CRPCTable &t);
}

#ifdef ENABLE_OMNICORE
/** Register Omni data retrieval RPC commands */
void RegisterOmniDataRetrievalRPCCommands(CRPCTable &tableRPC);
#ifdef ENABLE_WALLET
/** Register Omni transaction creation RPC commands */
void RegisterOmniTransactionCreationRPCCommands(CRPCTable &tableRPC);
#endif
/** Register Omni payload creation RPC commands */
void RegisterOmniPayloadCreationRPCCommands(CRPCTable &tableRPC);
/** Register Omni raw transaction RPC commands */
void RegisterOmniRawTransactionRPCCommands(CRPCTable &tableRPC);
#endif

static inline void RegisterAllCoreRPCCommands(CRPCTable &t, bool enableOmni = false)
{
    RegisterBlockchainRPCCommands(t);
    RegisterNetRPCCommands(t);
    RegisterMiscRPCCommands(t);
    RegisterMiningRPCCommands(t);
    RegisterRawTransactionRPCCommands(t);
    RegisterPoCRPCCommands(t);
    chiapos::RegisterChiaRPCCommands(t);

#ifdef ENABLE_OMNICORE
    if (enableOmni) {
        /* Omni Core RPCs: */
        RegisterOmniDataRetrievalRPCCommands(t);
#ifdef ENABLE_WALLET
        RegisterOmniTransactionCreationRPCCommands(t);
#endif
        RegisterOmniPayloadCreationRPCCommands(t);
        RegisterOmniRawTransactionRPCCommands(t);
    }
#endif
}

#endif // BITCOIN_RPC_REGISTER_H
