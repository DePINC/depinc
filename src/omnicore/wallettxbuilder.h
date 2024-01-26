#ifndef BITCOIN_OMNICORE_WALLETTXBUILDER_H
#define BITCOIN_OMNICORE_WALLETTXBUILDER_H

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

class uint256;
class CWallet;

namespace interfaces {
class Wallet;
} // namespace interfaces

#include <amount.h>

#include <stdint.h>
#include <string>
#include <vector>

/**
 * Creates and sends a transaction.
 */
int WalletTxBuilder(
        const std::string& senderAddress,
        const std::string& receiverAddress,
        const std::string& redemptionAddress,
        int64_t referenceAmount,
        const std::vector<unsigned char>& payload,
        uint256& retTxid,
        std::string& retRawTx,
        bool commit,
        interfaces::Wallet* iWallet = nullptr,
        CAmount min_fee = 0);

#ifdef ENABLE_WALLET
/**
 * Creates and sends a raw transaction by selecting all coins from the sender
 * and enough coins from a fee source. Change is sent to the fee source!
 */
int CreateFundedTransaction(
        const std::string& senderAddress,
        const std::string& receiverAddress,
        const std::string& feeAddress,
        const std::vector<unsigned char>& payload,
        uint256& retTxid,
        interfaces::Wallet* iWallet);

int CreateDExTransaction(interfaces::Wallet* pwallet, const std::string& buyerAddress, const std::string& sellerAddress, const CAmount& nAmount, uint256& txid);
#endif

#endif // BITCOIN_OMNICORE_WALLETTXBUILDER_H
