// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_UTIL_VALIDATION_H
#define BITCOIN_UTIL_VALIDATION_H

#include <string>

#include <amount.h>

class CValidationState;

/** Convert CValidationState to a human-readable message for logging */
std::string FormatStateMessage(const CValidationState &state);

inline CAmount GetWithdrawAmount(int nLockHeight, int nPledgeHeight, int nCalcHeight, CAmount nPledgeAmount)
{
    int nNumHeight = nCalcHeight - nPledgeHeight;
    if (nNumHeight >= nLockHeight) {
        return nPledgeAmount;
    }
    return nPledgeAmount * nNumHeight / nLockHeight;
}

extern const std::string strMessageMagic;

#endif // BITCOIN_UTIL_VALIDATION_H
