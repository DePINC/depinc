// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>

#include <hash.h>
#include <tinyformat.h>
#include <crypto/common.h>

#include <inttypes.h>

uint256 CBlockHeader::GetHash() const
{
    int ser_flags = IsChiaBlock() ? SERIALIZE_BLOCK_CHIAPOS : 0;
    return SerializeHash(*this, SER_GETHASH, ser_flags);
}

uint256 CBlockHeader::GetUnsignaturedHash() const
{
    int ser_flags = IsChiaBlock() ? SERIALIZE_BLOCK_CHIAPOS : 0;
    return SerializeHash(*this, SER_GETHASH | SER_UNSIGNATURED, ser_flags);
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBaseTarget=%08x, nNonce=%" PRIu64 ", nPlotterId=%" PRIu64 ", vtx=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        nTime, nBaseTarget, nNonce, nPlotterId,
        vtx.size());
    for (const auto& tx : vtx) {
        s << "  " << tx->ToString() << "\n";
    }
    return s.str();
}
