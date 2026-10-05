#pragma once
// Deterministic xorshift64 RNG. All randomness in a match flows through this so
// that "seed + action log" replays reproduce exactly.
#include <cstdint>
#include <utility>
#include <vector>

namespace fy {

struct Rng {
  uint64_t s = 0x9e3779b97f4a7c15ull;

  Rng() = default;
  explicit Rng(uint64_t seed) : s(seed ? seed : 0xdeadbeefcafef00dull) {}

  uint64_t next() {
    uint64_t x = s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    s = x;
    return x;
  }

  int below(int n) {
    if (n <= 0) return 0;
    return static_cast<int>(next() % static_cast<uint64_t>(n));
  }

  void shuffle(std::vector<int>& v) {
    for (size_t i = v.size(); i > 1; --i) {
      int j = below(static_cast<int>(i));
      std::swap(v[i - 1], v[static_cast<size_t>(j)]);
    }
  }
};

}  // namespace fy
