#include <chainparams.h>
#include <key_io.h>
#include <miner.h>
#include <net.h>
#include <netmessagemaker.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <util/strencodings.h>
#include <validation.h>
#include <subsidy_utils.h>
#include <core_io.h>
#include <util/moneystr.h>

#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <chrono>

#include <amount.h>

#include <arith_uint256.h>
#include <chain.h>

#include "coins.h"
#include <consensus/params.h>

#include <openssl/rand.h>

#include <primitives/transaction.h>
#include <uint256.h>
#include <updatetip_log_helper.hpp>
#include <logging.h>
#include <chiapos/mortgage_calculator.h>

#include <poc/poc.h>

#include "kernel/bls_key.h"
#include "kernel/calc_diff.h"
#include "kernel/chiapos_types.h"
#include "kernel/pos.h"
#include "kernel/utils.h"

#include "chain_info_querier.h"
#include "chiapos/elapsed_time.hpp"

extern std::unique_ptr<CConnman> g_connman;

namespace chiapos {

namespace utils {

std::shared_ptr<CBlock> CreateFakeBlock(CTxDestination const& dest) {
    std::unique_ptr<CBlockTemplate> pblocktemplate;
    try {
        pblocktemplate = BlockAssembler(Params()).CreateNewBlock(GetScriptForDestination(dest), 0, 0);
    } catch (std::exception& e) {
        char const* what = e.what();
        LogPrintf("CreateBlock() fail: %s\n", what ? what : "Catch unknown exception");
    }
    if (!pblocktemplate.get()) return nullptr;

    CBlock* pblock = &pblocktemplate->block;
    return std::make_shared<CBlock>(*pblock);
}

}  // namespace utils

static UniValue checkChiapos(JSONRPCRequest const& request) {
    RPCHelpMan("checkchiapos", "Check the chain is ready for chiapos", {},
               RPCResult{"\"ready\" (bool) true if the chain is ready"},
               RPCExamples{HelpExampleCli("checkchiapos", "")})
            .Check(request);

    LOCK(cs_main);

    CBlockIndex const* pindexPrev = ChainActive().Tip();
    Consensus::Params const& params = Params().GetConsensus();

    return IsTheChainReadyForChiapos(pindexPrev, params);
}

static UniValue queryChallenge(JSONRPCRequest const& request) {
    RPCHelpMan("querychallenge", "Query next challenge for PoST", {},
               RPCResult{"\"challenge\" (hex) the challenge in hex string"},
               RPCExamples{HelpExampleCli("querychallenge", "")})
            .Check(request);

    LOCK(cs_main);

    CBlockIndex const* pindexPrev = ChainActive().Tip();
    Consensus::Params const& params = Params().GetConsensus();

    if (!IsTheChainReadyForChiapos(pindexPrev, params)) {
        throw std::runtime_error("chiapos is not ready");
    }

    UniValue res(UniValue::VOBJ);
    int nTargetHeight = pindexPrev->nHeight + 1;
    uint256 challenge;
    res.pushKV("difficulty", GetDifficultyForNextIterations(pindexPrev, params));
    if (nTargetHeight == params.BHDIP009Height) {
        Bytes initialVdfProof(100, 0);
        challenge = MakeChallenge(pindexPrev->GetBlockHash(), initialVdfProof);
        res.pushKV("challenge", challenge.GetHex());
        res.pushKV("prev_vdf_iters", params.BHDIP009StartBlockIters);
        res.pushKV("prev_vdf_duration", params.BHDIP008TargetSpacing);
    } else {
        // We need to read the challenge from last block
        challenge = MakeChallenge(pindexPrev->GetBlockHash(), pindexPrev->chiaposFields.vdfProof.vchProof);
        res.pushKV("challenge", challenge.GetHex());
        res.pushKV("prev_vdf_iters", pindexPrev->chiaposFields.vdfProof.nVdfIters);
        res.pushKV("prev_vdf_duration", pindexPrev->chiaposFields.vdfProof.nVdfDuration);
    }
    assert(!challenge.IsNull());
    res.pushKV("prev_block_hash", pindexPrev->GetBlockHash().GetHex());
    res.pushKV("prev_block_height", pindexPrev->nHeight);
    res.pushKV("prev_block_time", pindexPrev->GetBlockTime());
    res.pushKV("target_height", nTargetHeight);
    res.pushKV("target_duration", params.BHDIP008TargetSpacing);
    res.pushKV("filter_bits",
               nTargetHeight < params.BHDIP009PlotIdBitsOfFilterEnableOnHeight ? 0 : params.BHDIP009PlotIdBitsOfFilter);
    int nBaseIters = GetBaseIters(nTargetHeight, params, pindexPrev->chiaposFields.GetItersPerSec());
    res.pushKV("base_iters", nBaseIters);

    // vdf requests
    UniValue vdf_reqs(UniValue::VARR);
    auto iters_vec = QueryLocalVdfRequests(challenge);
    for (auto iters : iters_vec) {
        if (iters >= nBaseIters) {
            vdf_reqs.push_back(iters);
        }
    }
    res.pushKV("vdf_reqs", vdf_reqs);

    // vdf proofs
    auto vVdfProofs = QueryLocalVdfProof(challenge);
    UniValue vdf_proofs(UniValue::VARR);
    for (auto const& vdfProof : vVdfProofs) {
        UniValue vdf_proof(UniValue::VOBJ);
        vdf_proof.pushKV("challenge", vdfProof.challenge.GetHex());
        vdf_proof.pushKV("y", BytesToHex(vdfProof.vchY));
        vdf_proof.pushKV("proof", BytesToHex(vdfProof.vchProof));
        vdf_proof.pushKV("witness_type", vdfProof.nWitnessType);
        vdf_proof.pushKV("iters", vdfProof.nVdfIters);
        vdf_proof.pushKV("duration", vdfProof.nVdfDuration);
        LogPrint(BCLog::NET, "%s (VDF proof): challenge=%s, iters=%ld, duration=%d (secs)\n", __func__, vdfProof.challenge.GetHex(), vdfProof.nVdfIters, vdfProof.nVdfDuration);
        vdf_proofs.push_back(std::move(vdf_proof));
    }
    res.pushKV("vdf_proofs", vdf_proofs);
    return res;
}

static UniValue submitVdfRequest(JSONRPCRequest const& request) {
    RPCHelpMan("submitvdfrequest", "Submit vdf request to P2P network",
        {
            {"challenge", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The challenge of the request"},
            {"iters", RPCArg::Type::NUM, RPCArg::Optional::NO, "The number of iters of the request"},
        },
        RPCResult{"{boolean} True means the request is submitted successfully, otherwise the request is not accepted"},
        RPCExamples{HelpExampleCli("submitvdfrequest", "xxxxxxxx 10239")}).Check(request);

    uint256 challenge = ParseHashV(request.params[0], "challenge");
    int64_t nIters = request.params[1].get_int64();

    if (nIters < 1) {
        throw std::runtime_error(tinyformat::format("%s: invalid iters=(%d)", __func__, nIters));
    }

    LOCK(cs_main);
    AddLocalVdfRequest(challenge, nIters);

    // send the request to P2P network
    g_connman->ForEachNode(
        [&challenge, nIters](CNode* pnode) {
            if (pnode->nVersion >= VDF_P2P_VERSION) {
                CNetMsgMaker maker(pnode->GetSendVersion());
                g_connman->PushMessage(pnode, maker.Make(NetMsgType::VDFREQ64, challenge, nIters));
            }
        }
    );

    return true;
}

static UniValue submitVdfProof(JSONRPCRequest const& request) {
    RPCHelpMan("submitvdfproof", "Submit vdf proof to P2P network", {
        {"challenge", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The challenge of the vdf proof"},
        {"y", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Y of the proof"},
        {"proof", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Proof of the proof"},
        {"witness_type", RPCArg::Type::NUM, RPCArg::Optional::NO, "Witness type of the proof"},
        {"iters", RPCArg::Type::NUM, RPCArg::Optional::NO, "Iterations of the proof"},
        {"duration", RPCArg::Type::NUM, RPCArg::Optional::NO, "Time consumed to calculate the proof"}
    }, RPCResult{"{boolean} True means the proof is submitted to P2P network, otherwise the proof is not accepted"},
    RPCExamples{
        HelpExampleCli("submitvdfproof", "xxxx xxxx xxxx 0 20000 60")
    }).Check(request);

    CVdfProof vdfProof;
    vdfProof.challenge = ParseHashV(request.params[0], "challenge");
    vdfProof.vchY = ParseHexV(request.params[1], "y");
    vdfProof.vchProof = ParseHexV(request.params[2], "proof");
    vdfProof.nWitnessType = request.params[3].get_int();
    if (vdfProof.nWitnessType < 0 || vdfProof.nWitnessType > 255) {
        throw std::runtime_error("invalid value of witness_type");
    }
    vdfProof.nVdfIters = request.params[4].get_int64();
    vdfProof.nVdfDuration = request.params[5].get_int();

    // verify the proof
    CValidationState state;
    if (!CheckVdfProof(vdfProof, state)) {
        throw std::runtime_error(tinyformat::format("%s: the vdf proof (challenge=%s, proof=%s) is invalid", __func__, vdfProof.challenge.GetHex(), BytesToHex(vdfProof.vchProof)));
    }

    LOCK(cs_main);

    // save the proof
    if (!AddLocalVdfProof(vdfProof)) {
        LogPrint(BCLog::POC, "%s: warning - proof (challenge=%s, iters=%ld) does exist in local\n", __func__, vdfProof.challenge.GetHex(), vdfProof.nVdfIters);
    }

    // dispatch the message to P2P network
    g_connman->ForEachNode([&vdfProof](CNode *pnode) {
        if (pnode->nVersion >= VDF_P2P_VERSION) {
            CNetMsgMaker msgMaker(pnode->GetSendVersion());
            g_connman->PushMessage(pnode, msgMaker.Make(NetMsgType::VDF, vdfProof));
        }
    });

    return true;
}

CVdfProof ParseVdfProof(UniValue const& val) {
    CVdfProof proof;
    proof.challenge = ParseHashV(val["challenge"], "challenge");
    proof.vchY = ParseHexV(val["y"], "y");
    proof.vchProof = ParseHexV(val["proof"], "proof");
    proof.nVdfIters = val["iters"].get_int64();
    proof.nWitnessType = val["witness_type"].get_int();
    proof.nVdfDuration = val["duration"].get_int64();
    return proof;
}

void GenerateChiaBlock(uint256 const& hashPrevBlock, int nHeightOfPrevBlock, CTxDestination const& rewardDest,
                       uint256 const& initialChallenge, chiapos::Bytes const& vchFarmerSk, CPosProof const& posProof,
                       CVdfProof const& vdfProof, uint64_t nDifficulty) {
    CKey farmerSk(MakeArray<SK_LEN>(vchFarmerSk));
    auto params = Params();
    std::shared_ptr<CBlock> pblock;
    {
        LOCK(cs_main);

        CBlockIndex const* pindexPrev = LookupBlockIndex(hashPrevBlock);  // The previous block for the new block
        if (pindexPrev == nullptr) {
            throw std::runtime_error("Cannot find the block index");
        }
        if (pindexPrev->nHeight != nHeightOfPrevBlock) {
            throw std::runtime_error("Invalid height number of the previous block");
        }

        if (!IsTheChainReadyForChiapos(pindexPrev, params.GetConsensus())) {
            LogPrintf("%s error: The chain is not ready for chiapos.\n", __func__);
            throw std::runtime_error("chiapos is not ready");
        }

        CBlockIndex* pindexCurr = ::ChainActive().Tip();
        if (pindexPrev->GetBlockHash() != pindexCurr->GetBlockHash()) {
            // The chain has changed during the proofs generation, we need to ensure:
            // 1. The new block is able to connect to the pevious block
            // 2. The difficulty of the new proofs should be larger than the last block's difficulty on the chain

            if (pindexCurr->pprev->GetBlockHash() != pindexPrev->GetBlockHash()) {
                // It seems the new block is not be able to connect to previous block
                LogPrintf("%s(drop proofs): it's not able to find the previous block of the new proofs\n", __func__);
                throw std::runtime_error(
                        "invalid new proofs, the chain has been changed and it is not able to accept it");
            }

            // Quality for the block we are going to generate
            uint256 mixed_quality_string = GenerateMixedQualityString(posProof);
            uint64_t nDuration = vdfProof.nVdfDuration;
            if (nDifficulty < pindexCurr->chiaposFields.nDifficulty) {
                // The quality is too low, and it will not be accepted by the chain
                throw std::runtime_error("the quality is too low, the new block will not be accepted by the chain");
            }

            // We reset the chain states to previous block and try to release the new one after
            {
                CValidationState state;
                LOCK(mempool.cs);
                ::ChainstateActive().DisconnectTip(state, params, nullptr);
            }

            LogPrintf("%s: the chain is reset to previous block in order to release a new block\n", __func__);
        }

        // Check bind
        const CAccountID accountID = ExtractAccountID(rewardDest);
        if (accountID.IsNull()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid DePINC address");
        }
        bool fFundAccount { false };
        for (auto const& fundAddr : params.GetConsensus().BHDIP009FundAddresses) {
            auto fundAccountID = ExtractAccountID(DecodeDestination(fundAddr));
            if (fundAccountID == accountID) {
                fFundAccount = true;
                break;
            }
        }
        if (!fFundAccount) {
            auto vchFarmerPk = MakeBytes(farmerSk.GetPubKey());
            if (!::ChainstateActive().CoinsTip().HaveActiveBindPlotter(accountID, CPlotterBindData(CChiaFarmerPk(vchFarmerPk)))) {
                throw JSONRPCError(RPC_INVALID_REQUEST,
                    strprintf("%s with %s not active bind", BytesToHex(vchFarmerPk), EncodeDestination(rewardDest)));
            }
        }

        // Trying to release a new block
        PubKeyOrHash poolPkOrHash =
                MakePubKeyOrHash(static_cast<PlotPubKeyType>(posProof.nPlotType), posProof.vchPoolPkOrHash);
        std::unique_ptr<CBlockTemplate> ptemplate = BlockAssembler(params).CreateNewChiaBlock(
                pindexPrev, GetScriptForDestination(rewardDest), farmerSk, posProof, vdfProof);
        if (ptemplate == nullptr) {
            throw std::runtime_error("cannot generate new block, the template object is null");
        }
        pblock.reset(new CBlock(ptemplate->block));
    }

    if (pblock == nullptr) {
        throw std::runtime_error("pblock is null, cannot release new block");
    }
    ReleaseBlock(pblock, params);
}

static UniValue submitProof(JSONRPCRequest const& request) {
    RPCHelpMan("submitproof", "Submit proof to the chain and release new block",
        {
            RPCArg("Hash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The hash value of the previous block"),
            RPCArg("Height", RPCArg::Type::NUM, RPCArg::Optional::NO, "The number of height for the previous block"),
            RPCArg("Challenge", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The challenge of the chain"),
            RPCArg("ProofOfSpace", RPCArg::Type::OBJ, RPCArg::Optional::NO, "The proof of space", {
                {
                    RPCArg("Challenge", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The challenge from current chain"),
                    RPCArg("K", RPCArg::Type::NUM, RPCArg::Optional::NO, "The k value presents the size of the plot file"),
                    RPCArg("PkHash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Pool public-key or the hash value"),
                    RPCArg("LocalPk", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Local public-key from the plot file"),
                    RPCArg("PlotType", RPCArg::Type::NUM, RPCArg::Optional::NO, "Plot type in integer"),
                    RPCArg("Proof", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The proof of space"),
                },
            }),
            RPCArg("FarmerSk", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The farmer secure key"),
            RPCArg("VDFProof", RPCArg::Type::OBJ, RPCArg::Optional::NO, "The VDF proof", {
                {
                    RPCArg("Challenge", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The challenge from current chain"),
                    RPCArg("Y", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The y with the proof"),
                    RPCArg("Proof", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Proof of the VDF"),
                    RPCArg("Iters", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Iterations for the VDF proof"),
                    RPCArg("WitnessType", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Witness type"),
                    RPCArg("Duration", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Duration in seconds for calculating the vdf proof"),
                },
            }),
            RPCArg("RewardAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "The reward address"),
        },
        RPCResult("proof"),
        RPCExamples("depinc-cli submitproof ...")).Check(request);

    uint256 hashPrevBlock = ParseHashV(request.params[0], "prev_block_hash");
    int nHeightOfPrevBlock = request.params[1].get_int();
    uint256 initialChallenge = ParseHashV(request.params[2], "challenge");
    UniValue posVal = request.params[3];
    if (!posVal.isObject()) {
        throw std::runtime_error("pos is not an object");
    }
    // PoS proof
    CPosProof posProof;
    posProof.challenge = ParseHashV(posVal["challenge"], "challenge");
    posProof.nPlotK = posVal["k"].get_int();
    posProof.vchPoolPkOrHash = ParseHexV(posVal["pool_pk_or_hash"], "pool_pk_or_hash");
    posProof.vchLocalPk = ParseHexV(posVal["local_pk"], "local_pk");
    posProof.nPlotType = posVal["plot_type"].get_int();
    posProof.vchProof = ParseHexV(posVal["proof"], "proof");
    // Farmer secure-key
    Bytes vchFarmerSk = ParseHexV(request.params[4], "farmer_sk");
    // Generate Farmer public-key
    CKey farmerSk(MakeArray<SK_LEN>(vchFarmerSk));
    posProof.vchFarmerPk = MakeBytes(farmerSk.GetPubKey());
    // VDF proof
    CVdfProof vdfProof = ParseVdfProof(request.params[5]);
    uint64_t nTotalDuration = vdfProof.nVdfDuration;
    if (nTotalDuration == 0) {
        throw std::runtime_error("duration is zero from vdf proof");
    }
    // Reward address
    std::string strRewardDest = request.params[6].get_str();
    CTxDestination rewardDest = DecodeDestination(strRewardDest);
    if (!IsValidDestination(rewardDest)) {
        throw std::runtime_error("The reward destination is invalid");
    }

    auto params = Params().GetConsensus();

    uint64_t nDifficulty{1};
    {
        LOCK(cs_main);

        CBlockIndex* pindexPrev = LookupBlockIndex(hashPrevBlock);
        if (pindexPrev == nullptr) {
            LogPrintf("%s: cannot find block by hash: %s, the proof will not be submitted\n", __func__,
                      hashPrevBlock.GetHex());
            return false;
        }
        int nTargetHeight = pindexPrev->nHeight + 1;
        double targetMulFactor = GetTargetMulFactor(nTargetHeight, params);
        int adjust_target_spacing = GetAdjustTargetSpacing(nTargetHeight, params);
        nDifficulty = AdjustDifficulty(GetChiaBlockDifficulty(pindexPrev, params), static_cast<int64_t>(nTotalDuration),
                                       adjust_target_spacing, QueryDurationFix(nTargetHeight, params.BHDIP009TargetDurationFixes),
                                       GetDifficultyChangeMaxFactor(nTargetHeight, params),
                                       params.BHDIP009StartDifficulty, targetMulFactor);
    }

    // We should put it to the chain immediately
    GenerateChiaBlock(hashPrevBlock, nHeightOfPrevBlock, rewardDest, initialChallenge, vchFarmerSk, posProof, vdfProof,
                      nDifficulty);

    return true;
}

static UniValue queryNetspace(JSONRPCRequest const& request) {
    RPCHelpMan("querynetspace", "Query current netspace", {}, RPCResult{"\"result\" (uint64) The netspace in TB"},
               RPCExamples{HelpExampleCli("querynetspace", "")})
            .Check(request);

    LOCK(cs_main);

    auto querier = ChainInfoQuerier::CreateQuerier();

    CBlockIndex* pindex = ::ChainActive().Tip();
    auto params = Params().GetConsensus();
    CAmount nTotalSupplied = querier.GetTotalSupplied();

    auto netspace_avg = querier.GetAverageNetSpace();
    auto netspace = querier.GetNetSpace();

    UniValue res(UniValue::VOBJ);
    res.pushKV("supplied", nTotalSupplied);
    res.pushKV("supplied(Human)", chiapos::FormatNumberStr(std::to_string(nTotalSupplied)));
    res.pushKV("supplied(DePC)", MakeNumberStr(nTotalSupplied / COIN));
    res.pushKV("netspace", FormatNumberStr(std::to_string(netspace.GetLow64())));
    res.pushKV("netspace_tib", MakeNumberTiB(netspace).GetLow64());
    res.pushKV("netspace_tib(Human)", chiapos::FormatNumberStr(std::to_string(MakeNumberTiB(netspace).GetLow64())));
    res.pushKV("netspace_avg", FormatNumberStr(std::to_string(netspace_avg.GetLow64())));
    res.pushKV("netspace_avg_tib", MakeNumberTiB(netspace_avg).GetLow64());
    res.pushKV("netspace_avg_tib(Human)", chiapos::FormatNumberStr(std::to_string(MakeNumberTiB(netspace_avg).GetLow64())));

    return res;
}

static UniValue queryMiningRequirement(JSONRPCRequest const& request) {
    RPCHelpMan("queryminingrequirement", "Query the pledge requirement for the miner",
               {
                   {"address", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "The miner address"},
                   {"plotter-id", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "The arg is useless, provide it only to ensure the consensus is compatible with old blocks."},
               },
               RPCResult("\"{json}\" the requirement for the miner"),
               RPCExamples(HelpExampleCli("queryminerpledgeinfo", "xxxxxx")))
            .Check(request);

    // elapsed time entries will be recorded to `Debug` json object
    UniValue debug(UniValue::VOBJ);

    TimeElapsed te_init("initialize");

    LOCK(cs_main);
    CBlockIndex* pindex = ::ChainActive().Tip();
    auto params = Params().GetConsensus();
    if (pindex->nHeight < params.BHDIP009Height) {
        throw std::runtime_error("BHDIP009 is required");
    }

    CCoinsViewCache const& view = ::ChainstateActive().CoinsTip();

    debug.pushKV("te_init", te_init.PrintAndRecordElapsedTime());

    UniValue res(UniValue::VOBJ);
    UniValue summary(UniValue::VOBJ);

    int nTargetHeight = pindex->nHeight + 1;

    TimeElapsed te_get_height_for_calculating_total_supply("get_height_for_calculating_total_supply");
    int nHeightForCalculatingTotalSupply = GetHeightForCalculatingTotalSupply(nTargetHeight, params);
    debug.pushKV("te_get_height_for_calculating_total_supply", te_get_height_for_calculating_total_supply.PrintAndRecordElapsedTime());

    TimeElapsed te_get_accumulate("get_accumulate");
    CAmount nAccumulate = GetBlockAccumulateSubsidy(pindex, params);
    debug.pushKV("te_get_accumulate", te_get_accumulate.PrintAndRecordElapsedTime());

    TimeElapsed te_get_total_supply("get_total_supply");
    CAmount nTotalSupplied = GetTotalSupplyBeforeHeight(nHeightForCalculatingTotalSupply, params) +
                             GetTotalSupplyBeforeBHDIP009(params) * (params.BHDIP009TotalAmountUpgradeMultiply - 1);
    debug.pushKV("te_get_total_supply", te_get_total_supply.PrintAndRecordElapsedTime());

    TimeElapsed te_get_burned("get_burned");
    CAmount nBurned = view.GetAccountBalance(false, GetBurnToAccountID(), nullptr, nullptr, nullptr,
                                             &params.BHDIP009PledgeTerms, nHeightForCalculatingTotalSupply);
    debug.pushKV("te_get_burned", te_get_burned.PrintAndRecordElapsedTime());

    bool summaryForAddress { false };

    if (request.params.size() >= 1) {
        std::string address = request.params[0].get_str();
        CAccountID accountID = ExtractAccountID(DecodeDestination(address));

        CPlotterBindData bindData;
        if (request.params.size() >= 2) {
            int32_t nPlotterId;
            if (ParseInt32(request.params[1].get_str(), &nPlotterId)) {
                // plotter-id, burst
                bindData = nPlotterId;
            } else {
                std::vector<uint8_t> vchFarmerPk = ParseHexV(request.params[1], "farmerpk");
                CChiaFarmerPk farmerPk(vchFarmerPk);
                bindData = farmerPk;
            }
        }

        summaryForAddress = true; // TODO maybe we need to make this to an optional parameter

        TimeElapsed te_get_mining_require("get_mining_require");
        int nMinedCount, nTotalCount;
        CAmount nReq = poc::GetMiningRequireBalance(accountID, bindData, nTargetHeight, view, nullptr, nullptr, nBurned,
                                                    params, &nMinedCount, &nTotalCount, nHeightForCalculatingTotalSupply);
        summary.pushKV("address", address);
        summary.pushKV("require", nReq);
        summary.pushKV("require-human", FormatMoney(nReq));
        debug.pushKV("te_get_mining_require", te_get_mining_require.PrintAndRecordElapsedTime());

        // Retrieve all public-key which are binding with this account
        TimeElapsed te_get_fpks("get_fpks");
        UniValue pklist(UniValue::VARR);
        auto fpks = view.GetAccountBindPlotters(accountID, CPlotterBindData::Type::CHIA);
        for (auto const& fpk : fpks) {
            pklist.push_back(fpk.GetChiaFarmerPk().ToString());
        }
        debug.pushKV("te_get_fpks", te_get_fpks.PrintAndRecordElapsedTime());

        // List mined blocks which are related to this account
        TimeElapsed te_get_blks("get_blks");
        UniValue blks(UniValue::VARR);
        auto pcurrIndex = pindex;
        int count{0}, mined{0};
        while (pcurrIndex && pcurrIndex->nHeight >= params.BHDIP009Height && count < params.nCapacityEvalWindow) {
            // check fpk from the block
            for (auto const& fpk : fpks) {
                if (fpk.GetChiaFarmerPk().ToBytes() == pcurrIndex->chiaposFields.posProof.vchFarmerPk) {
                    // Now we export the block to UniValue and push it to array
                    UniValue blkVal(UniValue::VOBJ);
                    auto dest = CTxDestination(static_cast<ScriptHash>(pcurrIndex->generatorAccountID));
                    std::string accountIDStr = EncodeDestination(dest);
                    blkVal.pushKV("height", pcurrIndex->nHeight);
                    blkVal.pushKV("hash", pcurrIndex->GetBlockHash().GetHex());
                    blkVal.pushKV("fpk", chiapos::BytesToHex(pcurrIndex->chiaposFields.posProof.vchFarmerPk));
                    blkVal.pushKV("accountID", accountIDStr);
                    blks.push_back(blkVal);
                    break;
                }
            }
            // next
            pcurrIndex = pcurrIndex->pprev;
            ++count;
        }
        debug.pushKV("te_get_blks", te_get_blks.PrintAndRecordElapsedTime());

        res.pushKV("mined", blks);
        res.pushKV("fpks", pklist);
        summary.pushKV("mined", nMinedCount);
        summary.pushKV("count", nTotalCount);
    }

    summary.pushKV("burned", nBurned);
    summary.pushKV("accumulate", nAccumulate);
    summary.pushKV("supplied", nTotalSupplied);
    summary.pushKV("height", nTargetHeight);
    summary.pushKV("calc-height", nHeightForCalculatingTotalSupply);
    summary.pushKV("debug", debug);

    if (summaryForAddress) {
        res.pushKV("summary", summary);
        return res;
    }

    // just return the summary as the result, cause the request doesn't include miner information.
    return summary;
}

static UniValue queryChainVdfInfo(JSONRPCRequest const& request) {
    RPCHelpMan("querychainvdfinfo", "Query vdf speed and etc from current block chain",
               {{"height", RPCArg::Type::NUM, RPCArg::Optional::NO,
                 "The summary information will be calculated from this height"}},
               RPCResult("\"{json}\" the basic information of the vdf from block chain"),
               RPCExamples(HelpExampleCli("querychainvdfinfo", "200000")))
            .Check(request);

    LOCK(cs_main);
    auto pindex = ::ChainActive().Tip();

    auto params = Params().GetConsensus();
    int nHeight = atoi(request.params[0].get_str());
    if (nHeight < params.BHDIP009Height) {
        throw std::runtime_error("The height is out of the BHDIP009 range");
    }

    uint64_t vdf_best{0}, vdf_worst{999999}, vdf_total{0}, vdf_count{0};
    while (pindex->nHeight >= nHeight) {
        uint64_t vdf_curr = pindex->chiaposFields.GetTotalIters() / pindex->chiaposFields.GetTotalDuration();
        if (vdf_best < vdf_curr) {
            vdf_best = vdf_curr;
        }
        if (vdf_worst > vdf_curr) {
            vdf_worst = vdf_curr;
        }
        vdf_total += vdf_curr;
        ++vdf_count;
        // next
        pindex = pindex->pprev;
    }

    uint64_t vdf_average = vdf_total / vdf_count;
    UniValue res(UniValue::VOBJ);
    res.pushKV("best", MakeNumberStr(vdf_best));
    res.pushKV("worst", MakeNumberStr(vdf_worst));
    res.pushKV("average", MakeNumberStr(vdf_average));
    res.pushKV("from", nHeight);
    res.pushKV("count", vdf_count);

    return res;
}

static UniValue generateBurstBlocks(JSONRPCRequest const& request) {
    RPCHelpMan("generateburstblocks", "Submit burst blocks to chain",
               {{"count", RPCArg::Type::NUM, RPCArg::Optional::NO, "how many blocks want to generate"}},
               RPCResult{"\"succ\" (bool) True means the block is generated successfully"},
               RPCExamples{HelpExampleCli("generateburstblocks", "")})
            .Check(request);

    int nNumBlocks = request.params[0].get_int();
    if (nNumBlocks <= 0) {
        throw std::runtime_error("invalid number of blocks");
    }

    CChainParams const& params = Params();

    assert(!params.GetConsensus().BHDIP009FundAddresses.empty());
    CTxDestination dest = DecodeDestination(params.GetConsensus().BHDIP009FundAddresses[0]);

    for (int i = 0; i < nNumBlocks; ++i) {
        auto pblock = utils::CreateFakeBlock(dest);
        ReleaseBlock(pblock, params);
    }

    return true;
}

static UniValue queryUpdateTipHistory(JSONRPCRequest const& request) {
    RPCHelpMan("queryupdatetiphistory", "Query update tip logs",
               {
                   {"count", RPCArg::Type::NUM, RPCArg::Optional::NO, "how many logs want to be generated"},
                   {"skip", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "skip number of blocks before starts to count"},
                   {"vdf_match_req", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "only show those vdf match the requires"},
               },
               RPCResult{"\"succ\" (result) The update tips history"},
               RPCExamples{HelpExampleCli("queryupdatetiphistory", "")})
            .Check(request);

    int nCount = atoi(request.params[0].get_str());

    int nSkip { 0 };
    if (request.params.size() > 1) {
        if (!ParseInt32(request.params[1].get_str(), &nSkip)) {
            throw std::runtime_error("cannot parse integer from parameter[1] (skip)");
        }
    }

    bool fOnlyVdfMatches {false};
    if (request.params.size() > 2) {
        int val;
        if (!ParseInt32(request.params[2].get_str(), &val)) {
            throw std::runtime_error("cannot parse integer from parameter[2] (only matched vdf tips)");
        }
        fOnlyVdfMatches = val != 0;
    }

    auto params = Params().GetConsensus();

    LOCK(cs_main);
    auto pindex = ::ChainActive().Tip();

    while (nSkip > 0) {
        pindex = pindex->pprev;
        --nSkip;
    }

    UpdateTipLogHelper helper(pindex, Params());
    UniValue tips(UniValue::VARR);

    int nTotal { 0 };
    for (int i = 0; i < nCount; ++i) {
        UniValue entryVal = helper.PrintJson();
        if (fOnlyVdfMatches && entryVal["vdf-req-match"].get_str() == "false") {
            if (!helper.MoveToPrevIndex()) {
                break;
            }
            continue;
        }
        ++nTotal;
        // query the block
        CBlockIndex const* pindex = helper.GetBlockIndex();
        if (IsBlockPruned(pindex)) {
            entryVal.pushKV("error", "block is pruned");
        } else {
            CBlock block;
            if (!ReadBlockFromDisk(block, pindex, params)) {
                entryVal.pushKV("error", "cannot read block from disk");
            } else {
                UniValue txVal(UniValue::VARR);
                for (auto const& tx : block.vtx) {
                    if (tx->IsCoinBase()) {
                        CAccountID generatorAccountID = ExtractAccountID(tx->vout[0].scriptPubKey);
                        UniValue minerVal(UniValue::VOBJ);
                        minerVal.pushKV("address", EncodeDestination(CTxDestination((ScriptHash)generatorAccountID)));
                        minerVal.pushKV("reward", ValueFromAmount(tx->vout[0].nValue));
                        // accumulate
                        CAmount nAccumulate = GetBlockAccumulateSubsidy(pindex->pprev, params);
                        minerVal.pushKV("accumulate", ValueFromAmount(nAccumulate));
                        // save to entry
                        txVal.push_back(minerVal);
                    } else {
                        auto payload = ExtractTransactionDatacarrier(
                                *tx, pindex->nHeight,
                                {DATACARRIER_TYPE_BINDPLOTTER, DATACARRIER_TYPE_BINDCHIAFARMER,
                                 DATACARRIER_TYPE_CHIA_POINT, DATACARRIER_TYPE_CHIA_POINT_TERM_1,
                                 DATACARRIER_TYPE_CHIA_POINT_TERM_2, DATACARRIER_TYPE_CHIA_POINT_TERM_3,
                                 DATACARRIER_TYPE_CHIA_POINT_RETARGET});
                        if (payload) {
                            UniValue payloadVal(UniValue::VOBJ);
                            if (payload->type == DATACARRIER_TYPE_BINDPLOTTER ||
                                payload->type == DATACARRIER_TYPE_BINDCHIAFARMER) {
                                auto p = BindPlotterPayload::As(payload);
                                CAccountID accountID = ExtractAccountID(tx->vout[0].scriptPubKey);
                                std::string strAddress = EncodeDestination(static_cast<ScriptHash>(accountID));
                                payloadVal.pushKV("action", "bind");
                                payloadVal.pushKV("address", strAddress);
                                if (payload->type == DATACARRIER_TYPE_BINDPLOTTER) {
                                    payloadVal.pushKV("plotter", p->GetId().GetBurstPlotterId());
                                } else {
                                    payloadVal.pushKV("farmer", p->GetId().GetChiaFarmerPk().ToString());
                                }
                            } else if (payload->type == DATACARRIER_TYPE_CHIA_POINT ||
                                       payload->type == DATACARRIER_TYPE_CHIA_POINT_TERM_1 ||
                                       payload->type == DATACARRIER_TYPE_CHIA_POINT_TERM_2 ||
                                       payload->type == DATACARRIER_TYPE_CHIA_POINT_TERM_3) {
                                auto p = PointPayload::As(payload);
                                payloadVal.pushKV("action", "point");
                                payloadVal.pushKV("type", DatacarrierTypeToString(payload->type));
                                payloadVal.pushKV("amount", ValueFromAmount(tx->vout[0].nValue));
                                payloadVal.pushKV(
                                        "address",
                                        EncodeDestination(CTxDestination(static_cast<ScriptHash>(p->GetReceiverID()))));
                            } else if (payload->type == DATACARRIER_TYPE_CHIA_POINT_RETARGET) {
                                auto p = PointRetargetPayload::As(payload);
                                payloadVal.pushKV("action", "retarget");
                                payloadVal.pushKV("amount", ValueFromAmount(tx->vout[0].nValue));
                                payloadVal.pushKV(
                                        "address",
                                        EncodeDestination(CTxDestination(static_cast<ScriptHash>(p->GetReceiverID()))));
                                payloadVal.pushKV("type", DatacarrierTypeToString(p->GetPointType()));
                                payloadVal.pushKV("height", p->GetPointHeight());
                            }
                            txVal.push_back(payloadVal);
                        }
                    }
                }
                entryVal.pushKV("txs", txVal);
            }
        }
        tips.push_back(entryVal);
        // next
        if (!helper.MoveToPrevIndex()) {
            break;
        }
    }

    UniValue res(UniValue::VOBJ);
    res.pushKV("tips", tips);
    res.pushKV("total", nTotal);

    return res;
}

static UniValue querySupply(JSONRPCRequest const& request) {
    RPCHelpMan("querysupply", "Query distributed amount, burned amount from the height",
               {{"height", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "The height to calculate the amounts"}},
               RPCResult{"\"succ\" (result) The result of the amounts"},
               RPCExamples{HelpExampleCli("querysupply", "200000")})
            .Check(request);

    LOCK(cs_main);

    // calculate from last height
    auto pindex = ::ChainActive().Tip();
    int nLastHeight = pindex->nHeight;

    int nRequestedHeight = atoi(request.params[0].get_str());
    if (nRequestedHeight == 0) {
        nRequestedHeight = nLastHeight;
    }

    auto const& params = Params().GetConsensus();

    // calculate from the calculation height
    int nHeightForCalculatingTotalSupply = GetHeightForCalculatingTotalSupply(nRequestedHeight, params);
    CCoinsViewCache const& view = ::ChainstateActive().CoinsTip();

    CAmount nBurned = view.GetAccountBalance(false, GetBurnToAccountID(), nullptr, nullptr, nullptr,
                                             &params.BHDIP009PledgeTerms, nHeightForCalculatingTotalSupply);
    CAmount nTotalSupplied = GetTotalSupplyBeforeHeight(nHeightForCalculatingTotalSupply, params) +
                             GetTotalSupplyBeforeBHDIP009(params) * (params.BHDIP009TotalAmountUpgradeMultiply - 1);
    CAmount nActualAmount = nTotalSupplied - nBurned;

    UniValue calcValue(UniValue::VOBJ);
    calcValue.pushKV("request_height", nRequestedHeight);
    calcValue.pushKV("calc_height", nHeightForCalculatingTotalSupply);
    calcValue.pushKV("total_supplied", ValueFromAmount(nTotalSupplied));
    calcValue.pushKV("burned", ValueFromAmount(nBurned));
    calcValue.pushKV("actual_supplied", ValueFromAmount(nActualAmount));

    CAmount nLastBurned = view.GetAccountBalance(false, GetBurnToAccountID(), nullptr, nullptr, nullptr,
                                                 &params.BHDIP009PledgeTerms, nLastHeight);
    CAmount nLastTotalSupplied = GetTotalSupplyBeforeHeight(nLastHeight, params) +
                                 GetTotalSupplyBeforeBHDIP009(params) * (params.BHDIP009TotalAmountUpgradeMultiply - 1);
    CAmount nLastActualAmount = nLastTotalSupplied - nLastBurned;

    UniValue lastValue(UniValue::VOBJ);
    lastValue.pushKV("last_height", nLastHeight);
    lastValue.pushKV("total_supplied", ValueFromAmount(nLastTotalSupplied));
    lastValue.pushKV("burned", ValueFromAmount(nLastBurned));
    lastValue.pushKV("actual_supplied", ValueFromAmount(nLastActualAmount));

    UniValue resValue(UniValue::VOBJ);
    resValue.pushKV("dist_height", params.BHDIP009CalculateDistributedAmountEveryHeights);
    resValue.pushKV("calc", calcValue);
    resValue.pushKV("last", lastValue);

    return resValue;
}

static UniValue queryPledgeInfo(JSONRPCRequest const& request) {
    auto const& params = Params().GetConsensus();

    int nHeight = ::ChainActive().Height();

    if (nHeight < params.BHDIP009Height) {
        throw std::runtime_error("cannot query pledge info. cause the hard-fork is not applied");
    }

    UniValue resValue(UniValue::VOBJ);
    if (nHeight >= params.BHDIP010Height) {
        resValue.pushKV("retarget_min_heights", params.BHDIP010PledgeOverrideRetargetMinHeights);
    } else {
        resValue.pushKV("retarget_min_heights", params.BHDIP009PledgeRetargetMinHeights);
    }
    resValue.pushKV("capacity_eval_window", params.nCapacityEvalWindow);

    UniValue termsValue(UniValue::VARR);
    for (int i = 0; i < params.BHDIP009PledgeTerms.size(); ++i) {
        auto const& term = params.BHDIP009PledgeTerms.at(i);
        UniValue termValue(UniValue::VOBJ);
        termValue.pushKV("lock_height", term.nLockHeight);
        termValue.pushKV("actual_percent", term.nWeightPercent);
        termsValue.push_back(std::move(termValue));
    }
    resValue.pushKV("terms", termsValue);

    return resValue;
}

static UniValue dumpBurstCheckpoints(JSONRPCRequest const& request) {
    RPCHelpMan("dumpburstcheckpoints", "Dump checkpoints for burst blocks", {
        {"from_height", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "From this number of heights"}
    }, RPCResult("\"hash list\""), RPCExamples(HelpExampleCli("dumpburstcheckpoints", "xxx"))).Check(request);

    const int GAP_NUM = 2000;
    int nFromHeight = 310000;
    if (!request.params[0].isNull()) {
        nFromHeight = request.params[0].get_int();
    }

    LOCK(cs_main);
    auto const& params = Params().GetConsensus();
    UniValue res(UniValue::VARR);

    for (int nCurrHeight = nFromHeight; nCurrHeight < params.BHDIP009Height; nCurrHeight += GAP_NUM) {
        auto pindex = ::ChainActive()[nCurrHeight];
        UniValue entry(UniValue::VOBJ);
        entry.pushKV("height", nCurrHeight);
        entry.pushKV("hash", pindex->GetBlockHash().GetHex());
        res.push_back(std::move(entry));
    }

    return res;
}

NODISCARD static UniValue dumpPosProof(CPosProof const& posProof, CVdfProof const& vdfProof, int nHeight) {
    UniValue res(UniValue::VOBJ);
    res.pushKV("height", nHeight);

    UniValue posVal(UniValue::VOBJ);
    posVal.pushKV("challenge", posProof.challenge.GetHex());
    posVal.pushKV("poolpk_puzzlehash", BytesToHex(posProof.vchPoolPkOrHash));
    posVal.pushKV("localpk", BytesToHex(posProof.vchLocalPk));
    posVal.pushKV("farmerpk", BytesToHex(posProof.vchFarmerPk));
    posVal.pushKV("plot_type", posProof.nPlotType);
    posVal.pushKV("plot_k", posProof.nPlotK);
    posVal.pushKV("proof", BytesToHex(posProof.vchProof));
    res.pushKV("pos", posVal);

    UniValue vdfVal(UniValue::VOBJ);
    vdfVal.pushKV("challenge", vdfProof.challenge.GetHex());
    vdfVal.pushKV("y", BytesToHex(vdfProof.vchY));
    vdfVal.pushKV("proof", BytesToHex(vdfProof.vchProof));
    vdfVal.pushKV("witness_type", vdfProof.nWitnessType);
    vdfVal.pushKV("iters", vdfProof.nVdfIters);
    vdfVal.pushKV("duration", vdfProof.nVdfDuration);
    res.pushKV("vdf", vdfVal);

    return res;
}

static UniValue dumpPosProofs(JSONRPCRequest const& request) {
    RPCHelpMan("dumpposproof", "Dump pos proofs", {
        {"count", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "how many blocks are wanted"}
    }, RPCResult("\"result\", the pos list in json array"), RPCExamples(HelpExampleCli("dumpposproof", "n"))).Check(request);

    LOCK(cs_main);
    auto pindex = ::ChainActive().Tip();

    auto const& params = Params().GetConsensus();
    int nNumBlocks = pindex->nHeight - params.BHDIP009Height + 1;

    if (request.params.size() == 1 && !ParseInt32("count", &nNumBlocks)) {
        throw std::runtime_error("cannot parse parameter `count`");
    }

    UniValue res(UniValue::VARR);
    for (int i = 0; i < nNumBlocks; ++i) {
        UniValue proofVal = dumpPosProof(pindex->chiaposFields.posProof, pindex->chiaposFields.vdfProof, pindex->nHeight);
        res.push_back(std::move(proofVal));
        pindex = pindex->pprev;
        if (pindex == nullptr) {
            break;
        }
    }
    return res;
}

struct PledgeTx {
    uint256 blockHash;
    int nHeight;
    uint256 txHash;
    CAccountID sender;
    CAccountID receiver;
    CAmount nReceivedAmount;
    CAmount nActualAmount;
    DatacarrierType pledgeType;
    DatacarrierType pointType;
    int nPointHeight;
    int nExpiresOnHeight;
    bool fAvailable;
    bool fInTerm;
};

using PledgeTxSet = std::map<uint256, PledgeTx>;

std::string GetStrFromAccountID(CAccountID const& accountID) {
    CTxDestination dest((ScriptHash)accountID);
    return EncodeDestination(dest);
}

UniValue ConvertPledgeTxToUniValue(PledgeTx const& pledgeTx) {
    UniValue resVal(UniValue::VOBJ);
    resVal.pushKV("blockHash", pledgeTx.blockHash.GetHex());
    resVal.pushKV("height", pledgeTx.nHeight);
    resVal.pushKV("txHash", pledgeTx.txHash.GetHex());
    resVal.pushKV("sender", GetStrFromAccountID(pledgeTx.sender));
    resVal.pushKV("receiver", GetStrFromAccountID(pledgeTx.receiver));
    resVal.pushKV("receivedAmount", ValueFromAmount(pledgeTx.nReceivedAmount));
    resVal.pushKV("actualAmount", ValueFromAmount(pledgeTx.nActualAmount));
    resVal.pushKV("type", DatacarrierTypeToString(pledgeTx.pledgeType));
    resVal.pushKV("pointType", DatacarrierTypeToString(pledgeTx.pointType));
    resVal.pushKV("pointHeight", pledgeTx.nPointHeight);
    resVal.pushKV("expiresOnHeight", pledgeTx.nExpiresOnHeight);
    resVal.pushKV("available", pledgeTx.fAvailable);
    resVal.pushKV("inTerm", pledgeTx.fInTerm);
    return resVal;
}

bool IsPledgeTx(PledgeTxSet const& txs, uint256 const& txHash) {
    return txs.find(txHash) != std::cend(txs);
}

void MarkPledgeToUnavailable(PledgeTxSet& txs, uint256 const& txHashToMark, uint256 const& txHashCurr) {
    auto it = txs.find(txHashToMark);
    if (it == std::cend(txs)) {
        // cannot find the prevous tx, throw error
        throw std::runtime_error(tinyformat::format("previous tx(%s) cannot be found from retarget tx(%s)", txHashToMark.GetHex(), txHashCurr.GetHex()));
    }
    it->second.fAvailable = false;
}

bool GetPledgeTx(PledgeTxSet const& txs, uint256 const& txHash, PledgeTx& outPledgeTx) {
    auto it = txs.find(txHash);
    if (it == std::cend(txs)) {
        return false;
    }
    outPledgeTx = it->second;
    return true;
}

static void StripPledgeTx(PledgeTxSet& pledgeTxs, CBlock const& block, int nHeight, Consensus::Params const& params) {
    for (auto const& tx : block.vtx) {
        if (tx->IsCoinBase() || !tx->IsUniform()) {
            continue;
        }
        // TODO check if the type of the tx is a withdraw then mark it to unavailable
        auto ppayload = ExtractTransactionDatacarrier(*tx, nHeight, {DATACARRIER_TYPE_BINDCHIAFARMER, DATACARRIER_TYPE_CHIA_POINT, DATACARRIER_TYPE_CHIA_POINT_TERM_1, DATACARRIER_TYPE_CHIA_POINT_TERM_2, DATACARRIER_TYPE_CHIA_POINT_TERM_3, DATACARRIER_TYPE_CHIA_POINT_RETARGET});
        if (ppayload == nullptr) {
            if (IsPledgeTx(pledgeTxs, tx->vin[0].prevout.hash)) {
                MarkPledgeToUnavailable(pledgeTxs, tx->vin[0].prevout.hash, tx->GetHash());
            }
        } else {
            assert(tx->vout.size() >= 2);
            PledgeTx pledgeTx;
            pledgeTx.blockHash = block.GetHash();
            pledgeTx.nHeight = nHeight;
            pledgeTx.txHash = tx->GetHash();
            // account IDs
            pledgeTx.sender = ExtractAccountID(tx->vout[0].scriptPubKey);
            pledgeTx.pledgeType = ppayload->type;
            pledgeTx.fAvailable = true;
            if (ppayload->type == DATACARRIER_TYPE_BINDCHIAFARMER) {
                pledgeTx.receiver = pledgeTx.sender;
                pledgeTx.nReceivedAmount = tx->vout[0].nValue;
                pledgeTx.nActualAmount = 0;
                pledgeTx.pointType = DATACARRIER_TYPE_BINDCHIAFARMER;
                pledgeTx.nPointHeight = 0;
                pledgeTx.nExpiresOnHeight = 99999999;
                pledgeTx.fInTerm = false;
            } else {
                if (ppayload->type == DATACARRIER_TYPE_CHIA_POINT_RETARGET) {
                    // TODO find the previous tx and mark it to unavailable
                    auto retargetPayload = PointRetargetPayload::As(ppayload);
                    pledgeTx.pointType = retargetPayload->GetPointType();
                    pledgeTx.nPointHeight = retargetPayload->nPointHeight;
                    auto txHash = tx->vin[0].prevout.hash;
                    PledgeTx originalPledgeTx;
                    if (!GetPledgeTx(pledgeTxs, txHash, originalPledgeTx)) {
                        throw std::runtime_error(tinyformat::format("cannot find original pledge-tx(%s)", txHash.GetHex()));
                    }
                    pledgeTx.nReceivedAmount = originalPledgeTx.nReceivedAmount;
                    MarkPledgeToUnavailable(pledgeTxs, txHash, tx->GetHash());
                } else {
                    // point
                    auto pointPayload = PointPayload::As(ppayload);
                    pledgeTx.receiver = pointPayload->GetReceiverID();
                    pledgeTx.nReceivedAmount = tx->vout[0].nValue;
                    pledgeTx.pointType = pledgeTx.pledgeType;
                    pledgeTx.nPointHeight = nHeight;
                }
                // check if it's state is in-term
                int nTermIndex = static_cast<int>(pledgeTx.pointType - DATACARRIER_TYPE_CHIA_POINT);
                auto const& term = params.BHDIP009PledgeTerms.at(nTermIndex);
                int nExpiresOnHeight = pledgeTx.nPointHeight + term.nLockHeight;
                pledgeTx.fInTerm = nHeight < nExpiresOnHeight;
                if (pledgeTx.fInTerm) {
                    pledgeTx.nActualAmount = term.nWeightPercent * pledgeTx.nReceivedAmount / 100;
                } else {
                    pledgeTx.nActualAmount = params.BHDIP009PledgeTerms[0].nWeightPercent * pledgeTx.nReceivedAmount / 100;
                }
                pledgeTx.nExpiresOnHeight = nExpiresOnHeight;
            }
            // save
            pledgeTxs[pledgeTx.txHash] = std::move(pledgeTx);
        }
    }
}

struct Amounts {
    CAmount received;
    CAmount actual;
};

static UniValue queryChainPledgeInfo(JSONRPCRequest const& request) {
    RPCHelpMan("querychainpledgeinfo", "Get the chain pledge information",
        { },
        RPCResult("info"),
        RPCExamples("depinc-cli querychainpledgeinfo")).Check(request);

    auto params = ::Params().GetConsensus();
    LOCK(cs_main);

    PledgeTxSet pledgeTxs;
    for (int nHeight = params.BHDIP009Height; nHeight < ::ChainActive().Height(); ++nHeight) {
        auto pindex = ::ChainActive()[nHeight];
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex, params)) {
            throw std::runtime_error(tinyformat::format("cannot read block(%s) from disk", pindex->GetBlockHash().GetHex()));
        }
        StripPledgeTx(pledgeTxs, block, nHeight, params);
    }

    std::map<CAccountID, Amounts> accountIDAmount;

    UniValue txsVal(UniValue::VARR);
    CAmount nTotalPledge{0}, nActualPledge{0};
    for (auto const& pledgeTx : pledgeTxs) {
        txsVal.push_back(ConvertPledgeTxToUniValue(pledgeTx.second));
        if (pledgeTx.second.fAvailable) {
            nTotalPledge += pledgeTx.second.nReceivedAmount;
            nActualPledge += pledgeTx.second.nActualAmount;
            CAccountID receiver = pledgeTx.second.receiver;
            auto it = accountIDAmount.find(receiver);
            if (it == std::cend(accountIDAmount)) {
                // new
                accountIDAmount.insert(std::make_pair(receiver, Amounts{ pledgeTx.second.nReceivedAmount, pledgeTx.second.nActualAmount }));
            } else {
                auto& amounts = accountIDAmount[receiver];
                amounts.received += pledgeTx.second.nReceivedAmount;
                amounts.actual += pledgeTx.second.nActualAmount;
            }
        }
    }

    UniValue receiverVal(UniValue::VOBJ);
    for (auto const& receiverAmount: accountIDAmount) {
        UniValue amountsVal(UniValue::VOBJ);
        amountsVal.pushKV("received", ValueFromAmount(receiverAmount.second.received));
        amountsVal.pushKV("actual", ValueFromAmount(receiverAmount.second.actual));
        receiverVal.pushKV(GetStrFromAccountID(receiverAmount.first), amountsVal);
    }

    UniValue resVal(UniValue::VOBJ);

    UniValue summaryVal(UniValue::VOBJ);
    summaryVal.pushKV("total", ValueFromAmount(nTotalPledge));
    summaryVal.pushKV("actual", ValueFromAmount(nActualPledge));

    resVal.pushKV("summary", summaryVal);
    resVal.pushKV("receiver", receiverVal);
    resVal.pushKV("txs", txsVal);
    return resVal;
}

optional<CTransaction> create_burn_txouts_transaction(CCoinsViewCache const& coinsView, int nSpendHeight, std::vector<COutPoint> const& outpoints, CAmount& nOutValue)
{
    CMutableTransaction mtx;

    CAmount nTotalAmount { 0 };
    for (auto const& outpoint : outpoints) {
        Coin coin;
        if (!coinsView.GetCoin(outpoint, coin)) {
            throw std::runtime_error(tinyformat::format("cannot find the coin (%s, %d)", outpoint.hash.GetHex(), outpoint.n));
        }
        if (coin.IsSpent()) {
            throw std::runtime_error(tinyformat::format("the coin (%s, %d) has already spent", outpoint.hash.GetHex(), outpoint.n));
        }
        CScript scriptSig;
        scriptSig << OP_0;
        mtx.vin.push_back(CTxIn{ outpoint, scriptSig, CTxIn::SEQUENCE_FINAL });
        nTotalAmount += coin.out.nValue;
    }

    CAmount nTxFee { static_cast<CAmount>(0.01 * COIN) };
    if (mtx.vin.empty() || nTotalAmount < nTxFee) {
        // empty txins or the number of amount isn't enough, no transaction can be created
        return {};
    }

    // Make transaction to burn the txout
    std::vector<std::string> errors;

    auto destBurn = GetBurnToDestination();
    auto scriptBurn = GetScriptForDestination(destBurn);
    auto nBurnAmount = nTotalAmount - nTxFee;
    mtx.vout.push_back(CTxOut(nBurnAmount, scriptBurn));

    nOutValue = nBurnAmount;
    return CTransaction(mtx);
}

bool is_valid_txout(CCoinsView const& coinsView, int nSpendHeight, COutPoint const& outpoint, std::string& strOutReason)
{
    Coin coin;
    if (!coinsView.GetCoin(outpoint, coin)) {
        strOutReason = "coin cannot be found";
        return false;
    }
    if (coin.IsSpent()) {
        strOutReason = "coin is spent";
        return false;
    }
    if (coin.out.nValue == 0) {
        strOutReason = "coin amount is zero";
        return false;
    }
    return true;
}

std::tuple<uint256, CAmount> create_and_broadcast_burn_tx(CCoinsViewCache const& coinsView, int nSpendHeight, std::vector<COutPoint> const& outpoints, CAmount nMaxFee)
{
    std::string strErrorReason;
    CAmount value;
    auto tx_opt = create_burn_txouts_transaction(coinsView, nSpendHeight, outpoints, value);
    if (!tx_opt.has_value()) {
        return std::make_tuple(uint256(), 0);
    }
    auto tx = *tx_opt;
    auto ptx = std::make_shared<CTransaction>(tx);
    auto txError = BroadcastTransaction(ptx, strErrorReason, nMaxFee, true, false);
    if (txError != TransactionError::OK) {
        std::string strBroadcastError = TransactionErrorString(txError);
        LogPrintf("%s: cannot broadcast tx: %s, tx err: %s, because of %s\n", __func__, tx.GetHash().GetHex(), strBroadcastError, strErrorReason);
        return std::make_tuple(ptx->GetHash(), 0);
    }
    return std::make_tuple(ptx->GetHash(), value);
}

struct WrongTxOut {
    UniValue valSource;
    std::string strError;
};

UniValue burntxout(JSONRPCRequest const& request)
{
    RPCHelpMan("burntxout", "Burn a special txout without making signature",
        {
            RPCArg("txid or json filepath", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id of the tx owns the out"),
            RPCArg("txout-n or the number of inputs for burning tx", RPCArg::Type::NUM, RPCArg::Optional::NO,"The number of the txout from the transaction"),
        },
        RPCResult("txid"), RPCExamples("depinc-cli burntxout xxxxx 0")).Check(request);

    constexpr auto nMaxFee = static_cast<CAmount>(0.01 * COIN);

    LOCK(cs_main);
    auto const& coinsView = ::ChainstateActive().CoinsTip();
    int nSpendHeight = ::ChainActive().Height();

    // txout: txid, n
    if (request.params.size() != 2) {
        throw std::runtime_error("invalid number of parameters, the number should be 2 with (txid, n)");
    }

    std::string strFilePath = request.params[0].get_str();
    if (fs::exists(strFilePath) && fs::is_regular_file(strFilePath)) {
        int32_t nMaxTxIns { 0 };
        if (!ParseInt32(request.params[1].get_str(), &nMaxTxIns)) {
            throw std::runtime_error("cannot parse int from argument 2 (max number of txins)");
        }
        // txins should be loaded from json file
        std::ifstream inf(strFilePath);
        if (!inf.is_open()) {
            throw std::runtime_error("cannot open file to read");
        }
        std::string strContent;
        std::copy(std::istream_iterator<char>(inf), std::istream_iterator<char>(), std::back_inserter(strContent));
        UniValue valContent;
        valContent.read(strContent);
        if (!valContent.exists("coins")) {
            throw std::runtime_error("wrong json file, field `coins` doesn't exist");
        }
        std::vector<WrongTxOut> vWrongTxOuts;
        std::map<uint256, CAmount> mSentTxWithAmount;
        std::vector<COutPoint> outpoints;
        // parsed txouts, now read each of the txouts and put them to transaction
        uint32_t nTotalTxOuts { 0 };
        for (auto const& valTxOutFromFile : valContent["coins"].getValues()) {
            ++nTotalTxOuts;
            if (!valTxOutFromFile.exists("txid")) {
                vWrongTxOuts.emplace_back(WrongTxOut{valTxOutFromFile, "json string is missing `txid`"});
                continue;
            }
            if (!valTxOutFromFile.exists("n")) {
                vWrongTxOuts.emplace_back(WrongTxOut{valTxOutFromFile,  "json string is missing `n`"});
                continue;
            }
            if (!valTxOutFromFile.exists("value")) {
                vWrongTxOuts.emplace_back(WrongTxOut{valTxOutFromFile,  "json string is missing `value`"});
                continue;
            }
            uint256 txid;
            try {
                txid = ParseHashV(valTxOutFromFile["txid"], "txid");
            } catch (std::exception const&) {
                vWrongTxOuts.emplace_back(WrongTxOut{valTxOutFromFile,  "`txid` is invalid from json string"});
                continue;
            }
            int32_t n;
            if (!ParseInt32(valTxOutFromFile["n"].getValStr(), &n)) {
                vWrongTxOuts.emplace_back(WrongTxOut{valTxOutFromFile, "`n` cannot be parsed into integer from json string"});
                continue;
            }
            CAmount nValueFromTxOut;
            if (!ParseInt64(valTxOutFromFile["value"].getValStr(), &nValueFromTxOut)) {
                vWrongTxOuts.emplace_back(WrongTxOut{valTxOutFromFile, "`value` cannot be parsed into integer from json string"});
                continue;
            }
            if (nValueFromTxOut > 0) {
                std::string strReason;
                if (!is_valid_txout(coinsView, nSpendHeight, COutPoint(txid, n), strReason)) {
                    vWrongTxOuts.emplace_back(WrongTxOut{valTxOutFromFile, strReason});
                    continue;
                }
                outpoints.emplace_back(txid, n);
                if (outpoints.size() >= nMaxTxIns) {
                    // create transaction and broadcast it to P2P network
                    uint256 txid;
                    CAmount nTotalBurn;
                    std::tie(txid, nTotalBurn) = create_and_broadcast_burn_tx(coinsView, nSpendHeight, outpoints, nMaxFee);
                    if (!txid.IsNull()) {
                        mSentTxWithAmount.insert(std::make_pair(txid, nTotalBurn));
                    }
                    outpoints.clear();
                }
            } else {
                vWrongTxOuts.emplace_back(WrongTxOut{valTxOutFromFile, "value of the coin is zero"});
                continue;
            }
        }
        if (!outpoints.empty()) {
            uint256 txid;
            CAmount nTotalBurn;
            std::tie(txid, nTotalBurn) = create_and_broadcast_burn_tx(coinsView, nSpendHeight, outpoints, nMaxFee);
            if (!txid.IsNull()) {
                mSentTxWithAmount.insert(std::make_pair(txid, nTotalBurn));
            }
        }
        // create outputs
        UniValue valResult(UniValue::VOBJ);
        valResult.pushKV("total", static_cast<int32_t>(nTotalTxOuts));
        // wrong txouts
        UniValue valWrongTxOuts(UniValue::VARR);
        for (auto const& valTxOut : vWrongTxOuts) {
            UniValue valCoin(UniValue::VOBJ);
            valCoin.pushKV("coin", valTxOut.valSource);
            valCoin.pushKV("error", valTxOut.strError);
            valWrongTxOuts.push_back(valCoin);
        }
        valResult.pushKV("wrongTxOuts", valWrongTxOuts);
        // committed transactions
        UniValue valSentTxIDWithAmount(UniValue::VARR);
        for (auto const& pairTxIDAmount : mSentTxWithAmount) {
            UniValue valTxIDAmount(UniValue::VOBJ);
            valTxIDAmount.pushKV("txid", pairTxIDAmount.first.GetHex());
            valTxIDAmount.pushKV("value", pairTxIDAmount.second);
            valSentTxIDWithAmount.push_back(valTxIDAmount);
        }
        valResult.pushKV("commitTxs", valSentTxIDWithAmount);
        return valResult;
    }

    uint256 txid = ParseHashV(request.params[0], "txid");
    int32_t n;
    if (!ParseInt32(request.params[1].get_str(), &n)) {
        throw std::runtime_error("cannot convert argument 2 into integer value");
    }

    std::string strReason;
    if (!is_valid_txout(coinsView, nSpendHeight, COutPoint(txid, n), strReason)) {
        throw std::runtime_error(strReason);
    }
    uint256 retTxid;
    CAmount nTotalBurn;
    std::tie(retTxid, nTotalBurn) = create_and_broadcast_burn_tx(coinsView, nSpendHeight, { {txid, static_cast<uint32_t>(n)} }, nMaxFee);

    return retTxid.GetHex();
}

struct QualityStringGenerateParams {
    uint64_t difficulty;
    int bits_filter;
    int difficulty_constant_factor_bits;
    uint8_t k;
    uint64_t base_iters;
    int num_answers;
    uint32_t iters_sec;
};

uint256 GenerateRandomQualityString(QualityStringGenerateParams const& params, int& num_duplicated)
{
    std::optional<std::pair<uint64_t, uint256>> best;
    std::vector<uint64_t> secs_set;
    secs_set.reserve(params.num_answers);
    for (int i = 0; i < params.num_answers; ++i) {
        uint256 mixed_quality_string;
        RAND_bytes(mixed_quality_string.begin(), static_cast<int>(mixed_quality_string.size()));
        uint64_t iters = CalculateIterationsQuality(mixed_quality_string, params.difficulty, params.bits_filter, params.difficulty_constant_factor_bits, params.k, params.base_iters);
        secs_set.push_back(iters / params.iters_sec);
        if (!best.has_value() || best->first > iters) {
            best = std::make_pair(iters, mixed_quality_string);
        }
    }
    if (best.has_value()) {
        num_duplicated = 0;
        std::sort(std::begin(secs_set), std::end(secs_set));
        auto i = std::cbegin(secs_set);
        auto best_secs = *i;
        ++i;
        for (; i != std::cend(secs_set); ++i) {
            if (*i - best_secs > 5) {
                break;
            }
            ++num_duplicated;
        }
        return best->second;
    }
    throw std::runtime_error("no answer can be found!");
}

UniValue testtargetspacing(JSONRPCRequest const& request)
{
    RPCHelpMan("testargetspacing", "Show block time by generating fake blocks, blocks will not write to local database, just in memory",
        {
            RPCArg("targetspacing", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "The seconds of the block targeting"),
            RPCArg("preallocsecs", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "How many seconds for miner to find answers"),
            RPCArg("numanswers", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "The number of answers found for each pos"),
            RPCArg("numblocks", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "How many fake blocks to be generated"),
            RPCArg("blocks", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Show block entries in result"),
        },
        RPCResult("blocks"), RPCExamples("depinc-cli testtargetspacing 180 60 1000 1000 0")).Check(request);

    int target_duration { 120 };
    if (request.params.size() > 0) {
        if (!ParseInt32(request.params[0].get_str(), &target_duration)) {
            throw std::runtime_error("cannot parse number for param: targetspacing");
        }
    }

    int preallocsecs { 40 };
    if (request.params.size() > 1) {
        if (!ParseInt32(request.params[1].get_str(), &preallocsecs)) {
            throw std::runtime_error("cannot parse parameter on position 0 to a number");
        }
    }

    int num_answers { 1000 };
    if (request.params.size() > 2) {
        if (!ParseInt32(request.params[2].get_str(), &num_answers)) {
            throw std::runtime_error("cannot parse parameter on position 0 to a number");
        }
    }

    int num_blocks { 1000 };
    if (request.params.size() > 3) {
        if (!ParseInt32(request.params[3].get_str(), &num_blocks)) {
            throw std::runtime_error("invalid parameter: number of the blocks to be generated cannot be converted to a number");
        }
    }

    bool show_blocks{false};
    if (request.params.size() > 4) {
        show_blocks = request.params[4].get_bool();
    }

    auto const& params = Params().GetConsensus();

    UniValue blocks_val(UniValue::VARR);

    // prepare the base parameters for the chain
    uint32_t iters_sec = 220000;
    int duration_fix = 0;
    uint64_t current_difficulty = params.BHDIP009StartDifficulty;
    uint64_t base_iters = static_cast<uint64_t>(preallocsecs) * iters_sec;
    uint8_t plot_k = 32;
    int bits_filter = 9;

    // summary
    int last_480_duration{0}, last_480_count{0}, last_480_forks{0};
    arith_uint256 last_480_diff;

    for (int i = 0; i < num_blocks; ++i) {
        UniValue block_val(UniValue::VOBJ);
        block_val.pushKV("difficulty", FormatNumberStr(std::to_string(current_difficulty)));
        block_val.pushKV("height", i);
        // generate a fake pos quality like the block is mined
        int duplicated{0};
        auto mixed_quality_string = GenerateRandomQualityString({ current_difficulty, bits_filter, params.BHDIP009DifficultyConstantFactorBits, plot_k, base_iters, num_answers, iters_sec }, duplicated);
        block_val.pushKV("possible_forks", duplicated);
        block_val.pushKV("quality_str", mixed_quality_string.GetHex());
        uint64_t iters = chiapos::CalculateIterationsQuality(mixed_quality_string, current_difficulty, bits_filter, params.BHDIP009DifficultyConstantFactorBits, plot_k, base_iters);
        block_val.pushKV("iters", iters);
        int duration = static_cast<int>(iters / iters_sec);
        if (num_blocks - i <= 480) {
            last_480_diff += current_difficulty;
            last_480_duration += duration;
            if (duplicated > 0) {
                ++last_480_forks;
            }
            ++last_480_count;
        }
        block_val.pushKV("duration", FormatTime(duration));
        blocks_val.push_back(block_val);
        LogPrintf("%s: duration=%s, iters=%d, difficulty=%s, quality_str=%s, height=%d, forks=%d\n", __func__, FormatTime(duration), iters, FormatNumberStr(std::to_string(current_difficulty)), mixed_quality_string.GetHex(), i, duplicated);
        // next
        current_difficulty = AdjustDifficulty(current_difficulty, duration, target_duration, duration_fix, params.BHDIP009DifficultyChangeMaxFactor, params.BHDIP009StartDifficulty, 1.0);
    }

    UniValue res_val(UniValue::VOBJ);
    if (show_blocks) {
        res_val.pushKV("blocks", blocks_val);
    }
    res_val.pushKV("preallocsecs", preallocsecs);
    res_val.pushKV("num_answers", num_answers);
    res_val.pushKV("last_480_count", last_480_count);
    res_val.pushKV("last_480_avg_blk_time", FormatTime(last_480_duration / last_480_count));
    res_val.pushKV("last_480_diff", FormatNumberStr(std::to_string((last_480_diff / last_480_count).GetLow64())));
    res_val.pushKV("last_480_forks", last_480_forks);

    return res_val;
}

static UniValue queryhalvings(JSONRPCRequest const& request)
{
    auto const& params = Params().GetConsensus();
    CAmount nTotal;
    auto result_map = GenerateBlockSubsidyWithHalvings(&nTotal, params);

    LOCK(cs_main);
    int nHeight = ::ChainActive().Height();

    int nNextHeightForHalvings { 999999999 };
    UniValue halvings_val(UniValue::VARR);
    for (auto const& entry : result_map) {
        UniValue halvings_entry_val(UniValue::VOBJ);
        halvings_entry_val.pushKV("height", entry.first);
        halvings_entry_val.pushKV("amount", entry.second.nAmountPerBlock);
        halvings_entry_val.pushKV("amountHuman", FormatMoney(entry.second.nAmountPerBlock));
        halvings_entry_val.pushKV("halvingsAmount", entry.second.nTotalAmount);
        halvings_entry_val.pushKV("halvingsAmountHuman", FormatMoney(entry.second.nTotalAmount));
        halvings_val.push_back(std::move(halvings_entry_val));
        // find the height for next halvings
        if (entry.first > nHeight) {
            if (nNextHeightForHalvings > entry.first) {
                nNextHeightForHalvings = entry.first;
            }
        }
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("halvings", halvings_val);
    result.pushKV("halvingsHeight", nNextHeightForHalvings);
    result.pushKV("halvingsAmount", result_map[nNextHeightForHalvings].nAmountPerBlock);
    result.pushKV("halvingsDays", (nNextHeightForHalvings - nHeight) * params.BHDIP008TargetSpacing / 60 / 60 / 24);
    result.pushKV("currentHeight", nHeight);
    result.pushKV("currentSubsidy", GetBlockSubsidy(nHeight, params));
    result.pushKV("total", nTotal);
    result.pushKV("totalHuman", FormatMoney(nTotal));

    return result;
}

struct RankInfo {
    int count;
    uint64_t average_difficulty;
    CAmount total_reward;
};

static UniValue queryBlockSummary(JSONRPCRequest const& request)
{
    RPCHelpMan("queryblocksummary", "Query farmers from last N blocks",
        {
            RPCArg("numblocks", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "The number of blocks will be counted"),
        },
        RPCResult("blocks"), RPCExamples("depinc-cli testtargetspacing 180 60 1000 1000 0")).Check(request);

    LOCK(cs_main);
    auto pindex = ::ChainActive().Tip();
    int n {480};
    if (request.params.size() == 1) {
        if (!ParseInt32(request.params[0].get_str(), &n)) {
            throw std::runtime_error("cannot parse number from the first argument");
        }
    }

    auto const& params = ::Params().GetConsensus();

    std::map<std::string, RankInfo> summary;
    while (n > 0) {
        auto const& farmer_pk_data = pindex->GetBlockHeader().chiaposFields.posProof.vchFarmerPk;
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex, params)) {
            throw std::runtime_error(tinyformat::format("cannot read block from disk, block=%s", pindex->GetBlockHash().GetHex()));
        }
        for (auto const& tx : block.vtx) {
            if (tx->IsCoinBase()) {
                auto account_id = ExtractAccountID(tx->vout[0].scriptPubKey);
                CTxDestination dest((ScriptHash)account_id);
                std::string address = EncodeDestination(dest);
                auto it = summary.find(address);
                RankInfo rank_info;
                rank_info.count = 0;
                rank_info.average_difficulty = 0;
                rank_info.total_reward = 0;
                if (it != std::cend(summary)) {
                    rank_info = it->second;
                }
                ++rank_info.count;
                rank_info.average_difficulty += pindex->chiaposFields.nDifficulty;
                rank_info.total_reward += tx->vout[0].nValue;
                summary[address] = rank_info;
            }
        }
        --n;
        if (pindex->pprev == nullptr) {
            break;
        }
        pindex = pindex->pprev;
    }
    UniValue res(UniValue::VARR);
    for (auto entry : summary) {
        UniValue entry_val(UniValue::VOBJ);
        entry_val.pushKV("address", entry.first);
        entry_val.pushKV("count", entry.second.count);
        entry_val.pushKV("average_difficulty", entry.second.average_difficulty / entry.second.count);
        entry_val.pushKV("total_reward", entry.second.total_reward);
        entry_val.pushKV("total_reward_human", FormatMoney(entry.second.total_reward));
        res.push_back(std::move(entry_val));
    }
    return res;
}

struct AccumulateSummary {
    CAmount total;
    int from_height;
    int to_height;
};

static UniValue queryAccumulateAmounts(JSONRPCRequest const& request)
{
    RPCHelpMan("queryaccumulateamounts", "Query accumulate reward records with owners",
        {
            RPCArg("address", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Only find the blocks belong to this address (default=any address)"),
            RPCArg("back_to_height", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "The summary contains blocks since the height (default = BHDIP009Height)"),
        },
        RPCResult("entries"),
        RPCExamples("depinc-cli queryaccumulateamounts")).Check(request);

    auto const& params = Params().GetConsensus();

    // address
    bool retrieve_last_one { false };
    std::string limit_to_address;
    if (request.params.size() >= 1) {
        limit_to_address = request.params[0].get_str();
        if (limit_to_address == "1") {
            limit_to_address.clear();
            retrieve_last_one = true;
        }
    }

    // back_to_height
    int back_to_height = params.BHDIP009Height;
    if (request.params.size() == 2) {
        if (!ParseInt32(request.params[1].get_str(), &back_to_height)) {
            throw std::runtime_error("failed to parse param `back_to_height`");
        }
    }

    AccumulateSummary summary { 0, ::ChainActive().Height(), back_to_height };

    UniValue accumulate_amounts_obj(UniValue::VARR);
    LOCK(cs_main);
    for (auto pindex = ::ChainActive().Tip(); pindex != nullptr && pindex->nHeight > back_to_height; pindex = pindex->pprev) {
        if ((pindex->nStatus & BLOCK_UNCONDITIONAL) == 0) {
            // read the block
            CBlock block;
            if (!ReadBlockFromDisk(block, pindex, params)) {
                throw std::runtime_error(tinyformat::format("failed to read block hash=%s from disk", pindex->GetBlockHash().GetHex()));
            }

            // find the miner
            assert(block.vtx[0]->IsCoinBase());
            auto account_id = ExtractAccountID(block.vtx[0]->vout[0].scriptPubKey);

            CTxDestination dest((ScriptHash)account_id);
            auto address = EncodeDestination(dest);

            // skip the entry we don't care
            if (!limit_to_address.empty() && address != limit_to_address) {
                continue;
            }

            // accumulate value with the block rewards
            CAmount accumulate = block.vtx[0]->vout[0].nValue;
            summary.total += accumulate;

            // ready to collect information and put to json object
            UniValue entry(UniValue::VOBJ);
            entry.pushKV("address", address);
            entry.pushKV("accumulate", accumulate);
            entry.pushKV("accumulate_human", FormatMoney(accumulate));
            entry.pushKV("reward_height", pindex->nHeight);
            entry.pushKV("difficulty", chiapos::FormatNumberStr(std::to_string(pindex->chiaposFields.nDifficulty)));
            entry.pushKV("block_time", FormatISO8601DateTime(pindex->GetBlockTime()));
            auto netspace = chiapos::CalculateNetworkSpace(pindex->chiaposFields.nDifficulty, pindex->chiaposFields.GetTotalIters(), params.BHDIP009DifficultyConstantFactorBits);
            entry.pushKV("netspace", chiapos::FormatNumberStr(std::to_string(netspace.GetLow64())));
            accumulate_amounts_obj.push_back(std::move(entry));

            if (retrieve_last_one) {
                break;
            }
        }
    }

    UniValue res(UniValue::VOBJ);

    UniValue summary_obj(UniValue::VOBJ);
    summary_obj.pushKV("total", FormatMoney(summary.total));
    summary_obj.pushKV("from_height", summary.from_height);
    summary_obj.pushKV("to_height", summary.to_height);
    res.pushKV("summary", summary_obj);
    res.pushKV("amounts", accumulate_amounts_obj);

    return res;
}

static UniValue queryAllPointCoins(JSONRPCRequest const& request)
{
    LOCK(cs_main);

    auto const& view = ::ChainstateActive().CoinsDB();
    auto coins = view.GetAllPointCoins();

    UniValue points_val(UniValue::VARR);

    CAmount total {0};
    std::map<CAccountID, CAmount> amount_by_account_id;

    for (auto const& coin : coins) {
        total += coin.total;
        CTxDestination fromDest((ScriptHash)coin.from);
        CTxDestination toDest((ScriptHash)coin.to);
        amount_by_account_id[coin.to] += coin.total;
        UniValue val(UniValue::VOBJ);
        val.pushKV("from", EncodeDestination(fromDest));
        val.pushKV("to", EncodeDestination(toDest));
        val.pushKV("type", DatacarrierTypeToString(coin.type));
        val.pushKV("amount", FormatMoney(coin.total));
        UniValue outpoint_val(UniValue::VOBJ);
        outpoint_val.pushKV("hash", coin.outpoint.hash.GetHex());
        outpoint_val.pushKV("n", static_cast<int>(coin.outpoint.n));
        val.pushKV("outpoint", outpoint_val);
        points_val.push_back(std::move(val));
    }

    UniValue accounts_val(UniValue::VARR);
    for (auto const& aa : amount_by_account_id) {
        CTxDestination dest((ScriptHash)aa.first);
        UniValue aa_val(UniValue::VOBJ);
        aa_val.pushKV("address", EncodeDestination(dest));
        aa_val.pushKV("amount", FormatMoney(aa.second));
        accounts_val.push_back(aa_val);
    }

    UniValue res_val(UniValue::VOBJ);
    res_val.pushKV("total", FormatMoney(total));
    res_val.pushKV("accounts", accounts_val);
    res_val.pushKV("points", points_val);

    return res_val;
}

struct MinerInfo {
    std::string address;
    CAmount rewards;
};

std::optional<MinerInfo> QueryMinerInfo(CBlockIndex* pindex, Consensus::Params const& params)
{
    CBlock block;
    if (!ReadBlockFromDisk(block, pindex, params)) {
        throw std::runtime_error("block cannot be read from disk");
    }

    std::vector<MinerInfo> details;
    for (auto const& tx : block.vtx) {
        if (tx->IsCoinBase()) {
            MinerInfo miner_info;
            auto account_id = ExtractAccountID(tx->vout[0].scriptPubKey);
            CTxDestination dest((ScriptHash)account_id);
            miner_info.address = EncodeDestination(dest);
            miner_info.rewards = tx->vout[0].nValue;
            return miner_info;
        }
    }

    return {};
}

static UniValue queryownblocks(JSONRPCRequest const& request)
{
    RPCHelpMan("queryownblocks", "query block owners and total rewards since a height", {
        RPCArg("address", RPCArg::Type::STR, RPCArg::Optional::NO, "count for the address"),
        RPCArg("height", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "count the summary since the height"),
    }, RPCResult {"json result"}, RPCExamples{""}).Check(request);

    LOCK(cs_main);
    auto pindex = ::ChainActive().Tip();
    auto const& params = Params().GetConsensus();

    std::string address = request.params[0].get_str();
    int height = params.BHDIP009Height;
    if (request.params.size() == 2) {
        if (!ParseInt32(request.params[1].get_str(), &height)) {
            throw std::runtime_error("cannot parse height");
        }
    }

    int count{0};
    CAmount total_rewards{0};
    int first_height{0};
    int last_height{0};
    UniValue miner_info_vals(UniValue::VARR);
    for (; pindex->nHeight >= height; pindex = pindex->pprev) {
        auto miner_info = QueryMinerInfo(pindex, params);
        if (miner_info.has_value()) {
            if (miner_info->address == address) {
                if (last_height == 0) {
                    last_height = pindex->nHeight;
                }
                first_height = pindex->nHeight;
                total_rewards += miner_info->rewards;
                ++count;
                UniValue miner_info_val(UniValue::VOBJ);
                miner_info_val.pushKV("height", pindex->nHeight);
                miner_info_val.pushKV("address", miner_info->address);
                miner_info_val.pushKV("reward", miner_info->rewards);
                miner_info_vals.push_back(miner_info_val);
            }
        }
    }

    UniValue res(UniValue::VOBJ);
    res.pushKV("details", miner_info_vals);
    res.pushKV("address", address);
    res.pushKV("since", height);
    res.pushKV("count", count);
    res.pushKV("first_height", first_height);
    res.pushKV("last_height", last_height);
    res.pushKV("total", total_rewards);
    res.pushKV("total_human", FormatMoney(total_rewards));
    res.pushKV("rewards_block", (count > 0 ? total_rewards / count : 0));
    res.pushKV("rewards_block_human", FormatMoney(count > 0 ? total_rewards / count : 0));

    return res;
}

static UniValue querypledgeamount(JSONRPCRequest const& request)
{
    RPCHelpMan("querypledgeamount", "Query pledge info for account",
        {
            RPCArg("address", RPCArg::Type::STR, RPCArg::Optional::NO, "Query the pledge details for this address only"),
            RPCArg("detail", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Put 1 here to show more details to exam"),
        },
        RPCResult("json"),
        RPCExamples("depinc-cli querypledgeamount")).Check(request);

    std::string address = request.params[0].get_str();
    bool fShowDetails{false};
    if (request.params.size() == 2) {
        int32_t val;
        if (!ParseInt32(request.params[1].get_str(), &val)) {
            throw std::runtime_error("The second parameter cannot be parsed into type: int32");
        }
        fShowDetails = val != 0;
    }

    auto const& params = Params().GetConsensus();

    CTxDestination dest = DecodeDestination(address);
    CAccountID accountID = ExtractAccountID(dest);

    LOCK(cs_main);
    ::ChainstateActive().ForceFlushStateToDisk();

    CBlockIndex* pindex = ::ChainActive().Tip();

    auto querier = ChainInfoQuerier::CreateQuerier();
    CAmount nTotalSupplied = querier.GetTotalSupplied();
    int nMined, nCounted;
    CAmount nPledgeRequiredAmount = querier.GetMiningRequireBalance(accountID, &nMined, &nCounted);

    auto [nTotalPledgeAmount, nTotalActualAmount, entries] = querier.GetTotalPledgedAmount(accountID);

    UniValue res(UniValue::VOBJ);

    if (fShowDetails) {
        UniValue pledgeVals(UniValue::VARR);

        auto conv_point_to_val = [nHeight = pindex->nHeight, &querier](PointEntry const& entry) -> UniValue {
            UniValue res(UniValue::VOBJ);
            res.pushKV("type", DatacarrierTypeToString(entry.type));
            res.pushKV("from", EncodeDestination(entry.from));
            res.pushKV("to", EncodeDestination(entry.to));
            res.pushKV("amount", entry.nAmount);
            if (entry.type == DATACARRIER_TYPE_CHIA_POINT_RETARGET) {
                res.pushKV("originalType", DatacarrierTypeToString(entry.originalType));
                res.pushKV("originalHeight", entry.nOriginalHeight);
                res.pushKV("actualAmount", querier.GetPledgeActualAmount(entry.originalType, entry.nOriginalHeight, nHeight, entry.nAmount));
                res.pushKV("expired", querier.CheckPledgeIsExpired(entry.originalType, entry.nOriginalHeight, nHeight));
                res.pushKV("remainingBlocks", querier.GetPledgeRemainingBlocks(entry.originalType, entry.nOriginalHeight, nHeight));
            } else {
                res.pushKV("actualAmount", querier.GetPledgeActualAmount(entry.type, entry.nHeight, nHeight, entry.nAmount));
                res.pushKV("expired", querier.CheckPledgeIsExpired(entry.type, entry.nHeight, nHeight));
                res.pushKV("remainingBlocks", querier.GetPledgeRemainingBlocks(entry.type, entry.nHeight, nHeight));
            }
            res.pushKV("txid", entry.txid.GetHex());
            res.pushKV("blockHash", entry.blockHash.GetHex());
            res.pushKV("blockTime", FormatISO8601DateTime(entry.blockTime));
            res.pushKV("height", entry.nHeight);
            return res;
        };

        using PointCIter = std::vector<PointEntry>::const_iterator;
        auto trans = [&conv_point_to_val](PointCIter begin, PointCIter end, UniValue& outVals) {
            for (; begin != end; ++begin) {
                outVals.push_back(conv_point_to_val(*begin));
            }
        };
        trans(std::cbegin(entries[DATACARRIER_TYPE_CHIA_POINT].points), std::cend(entries[DATACARRIER_TYPE_CHIA_POINT].points), pledgeVals);
        trans(std::cbegin(entries[DATACARRIER_TYPE_CHIA_POINT_TERM_1].points), std::cend(entries[DATACARRIER_TYPE_CHIA_POINT_TERM_1].points), pledgeVals);
        trans(std::cbegin(entries[DATACARRIER_TYPE_CHIA_POINT_TERM_2].points), std::cend(entries[DATACARRIER_TYPE_CHIA_POINT_TERM_2].points), pledgeVals);
        trans(std::cbegin(entries[DATACARRIER_TYPE_CHIA_POINT_TERM_3].points), std::cend(entries[DATACARRIER_TYPE_CHIA_POINT_TERM_3].points), pledgeVals);
        trans(std::cbegin(entries[DATACARRIER_TYPE_CHIA_POINT_RETARGET].points), std::cend(entries[DATACARRIER_TYPE_CHIA_POINT_RETARGET].points), pledgeVals);
        res.pushKV("details", pledgeVals);
    }

    res.pushKV("pledgeAmount", nTotalPledgeAmount);
    res.pushKV("pledgeActualAmount", nTotalActualAmount);
    res.pushKV("pledgeRequiredAmount", nPledgeRequiredAmount);
    res.pushKV("totalSupplied", nTotalSupplied);
    res.pushKV("minedBlocks", static_cast<int>(nMined));
    res.pushKV("minedEvalWindow", nCounted);
    res.pushKV("canEarnAccumulates", nTotalActualAmount > nPledgeRequiredAmount);

    return res;
}

UniValue queryFullMortgageInfo(JSONRPCRequest const& request)
{
    RPCHelpMan("queryfullmortgageinfo", "query full mortgage reward and related info. from current chain",
            {
                RPCArg("skip", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "the number of full-mortgage blocks will be skipped"),
                RPCArg("count", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "the number of full-mortgage blocks can be shown to the result"),
            },
            RPCResults({ RPCResult("(json array)", "the json string contains the mortgage info.")}),
            RPCExamples("./cli queryfullmortgageinfo")).Check(request);

    // retrieve the arguments
    int nSkip{0};
    int nCount{0};
    bool fShowAll{true};

    if (request.params.size() >= 1) {
        if (!ParseInt32(request.params[0].get_str(), &nSkip)) {
            throw std::runtime_error("cannot convert `skip` to number");
        }
    }

    if (request.params.size() == 2) {
        if (!ParseInt32(request.params[1].get_str(), &nCount)) {
            throw std::runtime_error("cannot convert `count` to number");
        }
        if (nCount > 0) {
            fShowAll = false;
        }
    }

    LOCK(cs_main);
    auto pindexTip = ::ChainActive().Tip();
    int nTargetHeight = pindexTip->nHeight + 1;

    UniValue resVal(UniValue::VARR);

    auto params = Params().GetConsensus();
    for (auto pcurr = pindexTip; pcurr->nHeight >= params.BHDIP009Height; pcurr = pcurr->pprev) {
        if (CMortgageCalculator::IsFullMortgageBlock(pcurr, params)) {
            if (nSkip > 0) {
                --nSkip;
                continue;
            }
            if (!fShowAll && nCount == 0) {
                break;
            }
            --nCount;
            UniValue fullMortgageVal(UniValue::VOBJ);
            CMortgageCalculator calculator(pindexTip, params);
            fullMortgageVal.pushKV("height", pcurr->nHeight);

            int numOfDistributions = calculator.CalcNumOfDistributions(pcurr->nHeight);
            int numOfDistributed = calculator.CalcNumOfDistributedForTargetHeight(pcurr->nHeight, nTargetHeight);

            fullMortgageVal.pushKV("numOfDistributions", numOfDistributions);
            fullMortgageVal.pushKV("numOfDistributed", std::min(numOfDistributions, numOfDistributed));
            fullMortgageVal.pushKV("noMoreDistribution", numOfDistributions <= numOfDistributed);

            CAmount nBlockSubsidy = GetBlockSubsidy(pcurr->nHeight, params);
            fullMortgageVal.pushKV("subsidy", nBlockSubsidy);
            fullMortgageVal.pushKV("subsidy(human)", FormatMoney(nBlockSubsidy));

            CAmount nOriginalAccumulated = GetBlockAccumulateSubsidy(pcurr->pprev, params);
            fullMortgageVal.pushKV("originalAccumulated", nOriginalAccumulated);
            fullMortgageVal.pushKV("originalAccumulated(human)", FormatMoney(nOriginalAccumulated));

            auto [nActualAccumulated, mapAccumulatedAmounts] = calculator.CalcAccumulatedAmount(pcurr->nHeight);
            fullMortgageVal.pushKV("actualAccumulated", nActualAccumulated);
            fullMortgageVal.pushKV("actualAccumulated(human)", FormatMoney(nActualAccumulated));

            // read the block
            CBlock block;
            if (!ReadBlockFromDisk(block, pcurr, params)) {
                throw std::runtime_error(tinyformat::format("cannot read block from disk (height=%d)", pcurr->nHeight));
            }
            // find coinbase
            CAmount nReward{0};
            for (auto const& tx : block.vtx) {
                if (tx->IsCoinBase()) {
                    auto destination = ExtractDestination(tx->vout[0].scriptPubKey);
                    nReward = tx->vout[0].nValue;
                    fullMortgageVal.pushKV("miner", EncodeDestination(destination));
                    fullMortgageVal.pushKV("reward", nReward);
                    fullMortgageVal.pushKV("reward(human)", FormatMoney(tx->vout[0].nValue));
                    break;
                }
            }
            // need to verify the reward
            CAmount nCalcReward{0};

            // block subsidy
            nCalcReward += nBlockSubsidy;

            // current block accumulated subsidy
            nCalcReward += (nOriginalAccumulated / numOfDistributions);

            // find accumulated amounts from previous full mortgage blocks
            UniValue constributedFromHeightsVal(UniValue::VARR);
            for (auto const& amountEntry : mapAccumulatedAmounts) {
                nCalcReward += amountEntry.second.nAccumulatedAmount;
                UniValue val(UniValue::VOBJ);
                val.pushKV("height", amountEntry.first);
                val.pushKV("amount", amountEntry.second.nAccumulatedAmount);
                val.pushKV("amount(human)", FormatMoney(amountEntry.second.nAccumulatedAmount));
                constributedFromHeightsVal.push_back(val);
            }

            fullMortgageVal.pushKV("calculatedReward", nCalcReward);
            fullMortgageVal.pushKV("calculatedReward(human)", FormatMoney(nCalcReward));
            fullMortgageVal.pushKV("contributedFromBlocks", constributedFromHeightsVal);

            resVal.push_back(fullMortgageVal);
        }
    }

    return resVal;
}

UniValue queryNumOfFullMortgageBlocks(JSONRPCRequest const& request)
{
    LOCK(cs_main);
    auto pindex = ::ChainActive().Tip();
    auto params = Params().GetConsensus();

    int nCount{0};
    for (; pindex->nHeight >= params.BHDIP009Height; pindex = pindex->pprev) {
        if (CMortgageCalculator::IsFullMortgageBlock(pindex, params)) {
            ++nCount;
        }
    }

    return nCount;
}

UniValue queryprofit(JSONRPCRequest const& request)
{
    RPCHelpMan("queryprofit", "get profit summary",
        {
            RPCArg{"days", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "the profit of 1PB netspace for the number of days, default=7"},
            RPCArg{"height", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "summary from the number of height, default=0 (current best height)"},
            RPCArg{"calcNetspaceForEachMiner", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "calculate netspace for each miner, default=false"},
        },
        RPCResults{RPCResult{"(subsidy in json object)", "{...}"}},
        RPCExamples{"queryprofit 7 1000000"}
        ).Check(request);

    LOCK(cs_main);
    int days{7};
    if (request.params.size() >= 1) {
        if (!ParseInt32(request.params[0].get_str(), &days)) {
            throw std::runtime_error("cannot parse `days`");
        }
    }

    int height{0};
    if (request.params.size() >= 2) {
        if (!ParseInt32(request.params[1].get_str(), &height)) {
            throw std::runtime_error("cannot parse `height`");
        }
    }

    bool calc_miner_netspace{false};
    if (request.params.size() == 3) {
        calc_miner_netspace = request.params[2].get_bool();
    }

    // query subsidy info. for last block
    auto const& params = Params().GetConsensus();

    if (height == 0) {
        height = ::ChainActive().Height();
    }
    CAmount subsidy = GetBlockSubsidy(height, params);

    auto querier = ChainInfoQuerier::CreateQuerier();
    auto netspace_avg = querier.GetAverageNetSpace();
    auto netspace_tb = static_cast<CAmount>(MakeNumberTB(netspace_avg).GetLow64());
    auto total_rewards = subsidy * days * 480;
    auto profit_1tb = total_rewards / netspace_tb;

    // query actual profit for days
    constexpr int SECS_DAY = 24 * 60 *60;
    int BLOCKS_DAY = SECS_DAY / params.BHDIP008TargetSpacing;

    auto const& view = ::ChainstateActive().CoinsDB();

    std::map<CAccountID, std::set<CPlotterBindData>> account_bind_farmer_pks;
    auto query_bind_farmer_pks = [&view, &account_bind_farmer_pks](CAccountID const& account_id) -> std::set<CPlotterBindData> {
        auto it = account_bind_farmer_pks.find(account_id);
        if (it == std::cend(account_bind_farmer_pks)) {
            std::set<CPlotterBindData> bindDataSet;
            // query from view
            auto map = view.GetAccountBindPlotterEntries(account_id);
            for (auto const& entry : map) {
                bindDataSet.insert(entry.second.bindData);
            }
            account_bind_farmer_pks.insert(std::make_pair(account_id, bindDataSet));
            return bindDataSet;
        }
        return it->second;
    };

    // the functor query the number of mined blocks within the capacity eval window in order to find the netspace
    auto calc_netspace_blks = [&params, &query_bind_farmer_pks](CBlockIndex* pindex_from, CAccountID const& account_id) -> int {
        auto farmer_pks = query_bind_farmer_pks(account_id);
        int num_blocks{0};
        int target_height = pindex_from->nHeight - params.nCapacityEvalWindow + 1;
        for (auto pindex = pindex_from; pindex->nHeight >= target_height; pindex = pindex->pprev) {
            // find by farmer-pk
            CChiaFarmerPk block_farmer_pk(pindex->chiaposFields.posProof.vchFarmerPk);
            if (find_if(std::cbegin(farmer_pks), std::cend(farmer_pks), [&block_farmer_pk](CPlotterBindData const& bind_data) -> bool {
                return bind_data.GetType() == CPlotterBindData::Type::CHIA && bind_data.GetChiaFarmerPk() == block_farmer_pk;
            }) != std::cend(farmer_pks)) {
                ++num_blocks;
            }
        }
        return num_blocks;
    };

    // loop until meet the required height
    auto pindex = ::ChainActive().Tip();
    while (pindex->nHeight > height) {
        pindex = pindex->pprev;
    }

    struct FMInfo {
        int blocks;
        int latest_height;
    };
    CAmount total_fullmortgage_profit{0};
    int num_fullmortgage_blocks{0};
    int num_checked_blocks{BLOCKS_DAY * days};
    std::map<CAccountID, FMInfo> fullmortgage_prod_blks;
    int low_height = height - num_checked_blocks;
    for (; pindex->nHeight >= low_height; pindex = pindex->pprev) {
        if ((pindex->nStatus & BLOCK_UNCONDITIONAL) == 0) {
            ++num_fullmortgage_blocks;
            // this is a full mortgage block, find the profit
            CBlock block;
            if (!ReadBlockFromDisk(block, pindex, params)) {
                throw std::runtime_error(tinyformat::format("cannot read block %d from disk", pindex->nHeight));
            }
            // find coin base
            for (auto const& tx : block.vtx) {
                if (tx->IsCoinBase()) {
                    total_fullmortgage_profit += tx->GetValueOut();
                    CAccountID account_id = ExtractAccountID(tx->vout[0].scriptPubKey);
                    int blks{0};
                    if (calc_miner_netspace) {
                        calc_netspace_blks(::ChainActive().Tip(), account_id);
                    }
                    if (fullmortgage_prod_blks.find(account_id) == std::cend(fullmortgage_prod_blks)) {
                        fullmortgage_prod_blks[account_id] = { 0, pindex->nHeight };
                    }
                    break;
                }
            }
        }
    }

    std::uint64_t fullmortgage_netspace_tb = num_fullmortgage_blocks * netspace_tb / num_checked_blocks;

    UniValue fullmortgage_details(UniValue::VARR);
    std::uint64_t total_full_mortgage_netspace{0};
    for (auto const& entry : fullmortgage_prod_blks) {
        UniValue entry_val(UniValue::VOBJ);
        CTxDestination dest(static_cast<ScriptHash>(entry.first));
        int blocks = entry.second.blocks;
        int latest_height = entry.second.latest_height;
        std::uint64_t entry_netspace_tb = static_cast<std::uint64_t>(netspace_tb * blocks / params.nCapacityEvalWindow);
        entry_val.pushKV("address", EncodeDestination(dest));
        entry_val.pushKV("latestHeight", FormatNumberStr(std::to_string(latest_height)));
        if (calc_miner_netspace) {
            entry_val.pushKV("netspaceTB", FormatNumberStr(std::to_string(entry_netspace_tb)));
            entry_val.pushKV("producedBlocks", blocks);
        }
        fullmortgage_details.push_back(std::move(entry_val));
        total_full_mortgage_netspace += entry_netspace_tb;
    }

    CAmount fullmortgage_profit_tb = fullmortgage_netspace_tb != 0 ? total_fullmortgage_profit / static_cast<CAmount>(fullmortgage_netspace_tb) : 0;
    CAmount fullmortgage_profit_pb = fullmortgage_profit_tb * 1000;

    UniValue res(UniValue::VOBJ);
    res.pushKV("height", height);
    res.pushKV("subsidy", subsidy);
    res.pushKV("subsidyHuman", FormatMoney(subsidy));
    res.pushKV("netspaceTB", FormatNumberStr(std::to_string(netspace_tb)));
    res.pushKV("profitPerTB", profit_1tb);
    res.pushKV("profitPerTBHuman", FormatMoney(profit_1tb));
    res.pushKV("profitFullmortgage", fullmortgage_details);
    res.pushKV("profitFullmortgagePerTB", fullmortgage_profit_tb);
    res.pushKV("profitFullmortgagePerTBHuman", FormatMoney(fullmortgage_profit_tb));
    res.pushKV("profitFullmortgagePerPB", fullmortgage_profit_pb);
    res.pushKV("profitFullmortgagePerPBHuman", FormatMoney(fullmortgage_profit_pb));
    res.pushKV("days", days);

    return res;
}

static std::vector<CRPCCommand> commands = {
        {"chia", "checkchiapos", &checkChiapos, {}},
        {"chia", "querychallenge", &queryChallenge, {}},
        {"chia", "querynetspace", &queryNetspace, {}},
        {"chia", "querychainvdfinfo", &queryChainVdfInfo, {"height"}},
        {"chia", "queryminingrequirement", &queryMiningRequirement, {"address", "farmer-pk"}},
        {"chia", "submitproof", &submitProof, {"challenge", "quality_string", "pos_proof", "k", "pool_pk", "local_pk", "farmer_pk", "farmer_sk", "plot_id", "vdf_proof_vec", "reward_dest"}},
        {"chia", "generateburstblocks", &generateBurstBlocks, {"count"}},
        {"chia", "queryupdatetiphistory", &queryUpdateTipHistory, {"count"}},
        {"chia", "querysupply", &querySupply, {"height"}},
        {"chia", "querypledgeinfo", &queryPledgeInfo, {}},
        {"chia", "dumpburstcheckpoints", &dumpBurstCheckpoints, {}},
        {"chia", "submitvdfrequest", &submitVdfRequest, {"challenge", "iters"}},
        {"chia", "submitvdfproof", &submitVdfProof, {"challenge", "y", "proof", "witness_type", "iters", "duration"}},
        {"chia", "dumpposproofs", &dumpPosProofs, {"count"}},
        {"chia", "querychainpledgeinfo", &queryChainPledgeInfo, {}},
        {"chia", "burntxout", &burntxout, {"txid","n"} },
        {"chia", "testtargetspacing", &testtargetspacing, {"numblocks"} },
        {"chia", "queryhalvings", &queryhalvings, {}},
        {"chia", "queryblocksummary", &queryBlockSummary, {"numblocks"}},
        {"chia", "queryaccumulateamounts", &queryAccumulateAmounts, {"address", "back_to_height"}},
        {"chia", "queryownblocks", &queryownblocks, {"address", "height"}},
        {"chia", "querypledgeamount", &querypledgeamount, {"address"}},
        {"chia", "queryallpointcoins", &queryAllPointCoins, {}},
        {"chia", "queryfullmortgageinfo", &queryFullMortgageInfo, {}},
        {"chia", "querynumoffullmortgageblocks", &queryNumOfFullMortgageBlocks, {}},
        {"chia", "queryprofit", &queryprofit, {}},
};

void RegisterChiaRPCCommands(CRPCTable& t) {
    for (auto const& command : commands) {
        t.appendCommand(command.name, &command);
    }
}

}  // namespace chiapos
