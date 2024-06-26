#include "calc_diff.h"

#include <arith_uint256.h>
#include <crypto/sha256.h>

#include <cstdint>
#include <limits>

#include "pos.h"
#include "utils.h"

namespace chiapos {

namespace {

arith_uint256 lower_bits(uint256 const& quality_string, int bits) {
    return UintToArith256(quality_string) & (Pow2(bits) - 1);
}

}  // namespace

using QualityBaseType = uint32_t;
constexpr int QUALITY_BASE_BITS = sizeof(QualityBaseType) * 8;

arith_uint256 Pow2(int bits) { return arith_uint256(1) << bits; }

uint64_t AdjustDifficulty(uint64_t prev_block_difficulty, int64_t curr_block_duration, uint64_t target_duration,
                          int duration_fix, double max_factor, uint64_t network_min_difficulty,
                          double target_mul_factor) {
    assert(target_duration > 0);
    int64_t total_fix = curr_block_duration + duration_fix;
    if (total_fix <= 0) {
        total_fix = 1;
    }

    uint64_t n = std::max<uint64_t>(prev_block_difficulty / total_fix, 1);
    uint64_t new_difficulty =
            std::max(n * static_cast<uint64_t>(static_cast<double>(target_duration) * target_mul_factor + duration_fix),
                     network_min_difficulty);
    assert(max_factor != 0);
    if (new_difficulty > prev_block_difficulty) {
        auto max_difficulty = static_cast<uint64_t>(static_cast<double>(prev_block_difficulty) * max_factor);
        new_difficulty = std::min(new_difficulty, max_difficulty);
    } else {
        auto min_difficulty = static_cast<uint64_t>(static_cast<double>(prev_block_difficulty) / max_factor);
        new_difficulty = std::max(new_difficulty, min_difficulty);
    }
    return std::max<uint64_t>(new_difficulty, 1);
}

int QueryDurationFix(int curr_height, std::map<int, int> const& fixes) {
    int height{0};
    int duration_fix{0};
    for (auto const& fix : fixes) {
        if (fix.first < curr_height && fix.first > height) {
            height = fix.first;
            duration_fix = fix.second;
        }
    }
    return duration_fix;
}

uint256 GenerateMixedQualityString(CPosProof const& posProof) {
    PubKeyOrHash poolPkOrHash =
            MakePubKeyOrHash(static_cast<PlotPubKeyType>(posProof.nPlotType), posProof.vchPoolPkOrHash);
    return chiapos::MakeMixedQualityString(MakeArray<PK_LEN>(posProof.vchLocalPk),
                                           MakeArray<PK_LEN>(posProof.vchFarmerPk), poolPkOrHash, posProof.nPlotK,
                                           posProof.challenge, posProof.vchProof);
}

uint64_t CalculateIterationsQuality(uint256 const& mixed_quality_string, uint64_t difficulty, int bits_filter,
                                    int difficulty_constant_factor_bits, uint8_t k, uint64_t base_iters,
                                    double* quality_in_plot, arith_uint256* quality) {
    assert(difficulty > 0);
    auto h = Pow2(QUALITY_BASE_BITS);
    auto l = lower_bits(mixed_quality_string, QUALITY_BASE_BITS);
    if (l == 0) {
        // impossible lower value from quality string, and it will cause `division by zero`
        l = h;
    }
    auto size = expected_plot_size<arith_uint256>(k);
    if (size <= 0) {
        // impossible value of the plot size
        return std::numeric_limits<uint64_t>::max();
    }
    auto iters = difficulty * Pow2(difficulty_constant_factor_bits) * l / Pow2(bits_filter) / (size * h) + base_iters;
    if (quality_in_plot) {
        *quality_in_plot = static_cast<double>(l.GetLow64()) / static_cast<double>(h.GetLow64());
    }
    if (quality) {
        *quality = size * h / l;
    }
    if (iters > Pow2(64)) {
        return std::numeric_limits<uint64_t>::max();
    }
    return std::max<uint64_t>(iters.GetLow64(), 1);
}

arith_uint256 CalculateNetworkSpace(uint64_t difficulty, uint64_t iters, int difficulty_constant_factor_bits) {
    if (iters == 0) {
        return 0;
    }
    arith_uint256 additional_difficulty_constant = Pow2(difficulty_constant_factor_bits);
    return arith_uint256(difficulty) / iters * additional_difficulty_constant * UI_ACTUAL_SPACE_CONSTANT_FACTOR /
           UI_ACTUAL_SPACE_CONSTANT_FACTOR_BASE;
}

}  // namespace chiapos
