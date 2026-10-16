#pragma once
#include <cstdint>

namespace core {

// Small deterministic PRNG (PCG32). Same seed -> same sequence on every machine.
class Rng {
public:
    explicit Rng(uint64_t seed = 1) { reseed(seed); }

    void reseed(uint64_t seed) {
        state_ = 0;
        next_u32();
        state_ += seed;
        next_u32();
    }

    uint32_t next_u32() {
        uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + 1442695040888963407ULL;
        uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
    }

    // [0, 1)
    float uniform() { return static_cast<float>(next_u32() >> 8) * (1.0f / 16777216.0f); }
    // [lo, hi)
    float range(float lo, float hi) { return lo + (hi - lo) * uniform(); }
    // [lo, hi] inclusive
    int range(int lo, int hi) {
        return lo + static_cast<int>(next_u32() % static_cast<uint32_t>(hi - lo + 1));
    }
    bool chance(float p) { return uniform() < p; }

private:
    uint64_t state_ = 0;
};

} // namespace core
