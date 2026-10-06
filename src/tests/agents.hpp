#pragma once
// Test agents. `CheckingAgent` validates the engine's structural invariants at
// every decision point and can also deliberately violate the decision protocol
// (illegal indices, wrong selection counts) to prove the engine never depends on
// untrusted agent input.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "engine/engine.hpp"
#include "protocol/agent.hpp"
#include "tests/invariants.hpp"

namespace fy {
namespace test {

enum class AgentMode {
  FirstLegal,      // first enabled option(s), minimum count
  LastLegal,       // last enabled option(s)
  MaxSelectLegal,  // fill up to maxSelect with enabled options
  RandomLegal,     // random legal selection within [minSelect, maxSelect]
  // --- protocol-violating modes (the engine must tolerate or reject these) ---
  RandomAny,
  OutOfRange,
  Negative,
  Empty,
  OverSelect,
  Duplicate,
};

inline const char* agent_mode_name(AgentMode m) {
  switch (m) {
    case AgentMode::FirstLegal: return "FirstLegal";
    case AgentMode::LastLegal: return "LastLegal";
    case AgentMode::MaxSelectLegal: return "MaxSelectLegal";
    case AgentMode::RandomLegal: return "RandomLegal";
    case AgentMode::RandomAny: return "RandomAny";
    case AgentMode::OutOfRange: return "OutOfRange";
    case AgentMode::Negative: return "Negative";
    case AgentMode::Empty: return "Empty";
    case AgentMode::OverSelect: return "OverSelect";
    case AgentMode::Duplicate: return "Duplicate";
  }
  return "?";
}

class CheckingAgent : public Agent {
 public:
  CheckingAgent(Engine* e, AgentMode m, uint64_t seed = 1)
      : e_(e), mode_(m), rng_(seed ? seed : 1) {}

  Decision decide(const Request& req) override {
    ++decisions_;
    if (e_ && violations_.size() < 8) {
      for (const std::string& v : check_invariants(*e_)) {
        if (violations_.size() >= 8) break;
        violations_.push_back(v);
      }
    }
    std::vector<int> enabled;
    for (int i = 0; i < static_cast<int>(req.options.size()); ++i)
      if (req.options[static_cast<size_t>(i)].enabled) enabled.push_back(i);

    Decision d;
    const int nopts = static_cast<int>(req.options.size());
    switch (mode_) {
      case AgentMode::FirstLegal:
        take(d, enabled, 0, req.minSel);
        break;
      case AgentMode::LastLegal:
        take(d, enabled, std::max(0, static_cast<int>(enabled.size()) - req.minSel), req.minSel);
        break;
      case AgentMode::MaxSelectLegal:
        take(d, enabled, 0, std::max(req.maxSel, req.minSel));
        break;
      case AgentMode::RandomLegal: {
        int lo = std::max(0, req.minSel);
        int hi = std::max(lo, req.maxSel);
        if (hi > static_cast<int>(enabled.size())) hi = static_cast<int>(enabled.size());
        if (lo > hi) lo = hi;
        int want =
            lo + (hi > lo ? static_cast<int>(rng_() % static_cast<uint64_t>(hi - lo + 1)) : 0);
        std::shuffle(enabled.begin(), enabled.end(), rng_);
        take(d, enabled, 0, want);
        break;
      }
      case AgentMode::RandomAny: {
        int k = static_cast<int>(rng_() % 4);
        for (int i = 0; i < k; ++i) {
          long long v = static_cast<long long>(rng_() % 17) - 5;  // -5 .. 11
          d.indices.push_back(static_cast<int>(v));
        }
        break;
      }
      case AgentMode::OutOfRange:
        if (nopts > 0) {
          d.indices.push_back(nopts);
          d.indices.push_back(nopts + 7);
        }
        d.indices.push_back(std::numeric_limits<int>::max());
        break;
      case AgentMode::Negative:
        d.indices.push_back(-1);
        d.indices.push_back(-1000);
        break;
      case AgentMode::Empty:
        break;
      case AgentMode::OverSelect:
        for (int i : enabled) d.indices.push_back(i);
        if (enabled.empty() && nopts > 0) d.indices.push_back(0);
        break;
      case AgentMode::Duplicate:
        if (!enabled.empty()) {
          int k = std::max(1, req.minSel);
          for (int i = 0; i < k; ++i) d.indices.push_back(enabled.front());
        }
        break;
    }
    return d;
  }

  const std::vector<std::string>& violations() const { return violations_; }
  int decisions() const { return decisions_; }

 private:
  static void take(Decision& d, const std::vector<int>& enabled, int from, int count) {
    for (int i = 0; i < count && from + i < static_cast<int>(enabled.size()); ++i)
      d.indices.push_back(enabled[static_cast<size_t>(from + i)]);
  }

  Engine* e_ = nullptr;
  AgentMode mode_;
  std::mt19937_64 rng_;
  std::vector<std::string> violations_;
  int decisions_ = 0;
};

}  // namespace test
}  // namespace fy
