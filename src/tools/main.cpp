#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "engine/engine.hpp"
#include "protocol/agent.hpp"

using namespace fy;

static std::string resolve(const std::string& given) {
  const char* pre[] = {"", "../", "../../"};
  for (const char* p : pre) {
    std::string full = std::string(p) + given;
    if (FILE* f = std::fopen(full.c_str(), "rb")) {
      std::fclose(f);
      return full;
    }
  }
  return given;
}

int main(int argc, char** argv) {
  Config cfg;
  std::string content = "content/hajimari.lua";
  std::string p0cmd, p1cmd, recordPath, replayPath;
  bool useRandom = false;
  bool standard = false;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : std::string(); };
    if (a == "--seed") cfg.seed = std::strtoull(next().c_str(), nullptr, 10);
    else if (a == "--limit") cfg.turnLimit = std::atoi(next().c_str());
    else if (a == "--random") useRandom = true;
    else if (a == "--standard") standard = true;
    else if (a == "--p0-cmd") p0cmd = next();
    else if (a == "--p1-cmd") p1cmd = next();
    else if (a == "--record") recordPath = next();
    else if (a == "--replay") replayPath = next();
    else content = a;
  }
  if (standard) cfg.mode = "standard";

  auto load_all = [&](Engine& e) {
    if (standard) {
      for (const char* f : {"content/yurina.lua", "content/saine.lua", "content/himika.lua",
                            "content/tokoyo.lua"})
        e.load_content(resolve(f));
    } else {
      e.load_content(resolve(content));
    }
  };

  std::ifstream rf(replayPath);
  if (!replayPath.empty()) {
    if (!rf) {
      std::fprintf(stderr, "cannot open replay %s\n", replayPath.c_str());
      return 1;
    }
    nlohmann::json j;
    rf >> j;
    cfg.seed = j.value("seed", cfg.seed);
    Engine e(cfg);
    load_all(e);
    e.load_journal(j);
    try {
      e.run();
    } catch (const std::exception& ex) {
      std::fprintf(stderr, "replay failed: %s\n", ex.what());
      return 2;
    }
    uint64_t expected = j.value("hash", 0ull);
    std::printf("replay hash=%llu expected=%llu %s (winner=%d)\n",
                static_cast<unsigned long long>(e.state_hash()),
                static_cast<unsigned long long>(expected),
                e.state_hash() == expected ? "OK" : "MISMATCH", e.st.winner);
    return e.state_hash() == expected ? 0 : 3;
  }

  Engine e(cfg);
  try {
    load_all(e);
  } catch (const std::exception& ex) {
    std::fprintf(stderr, "failed to load content: %s\n", ex.what());
    return 1;
  }

  std::unique_ptr<SubprocessAgent> sp0, sp1;
  FirstAgent fa0, fa1;
  RandomAgent ra0(cfg.seed + 101), ra1(cfg.seed + 202);
  Agent* a0;
  Agent* a1;
  if (!p0cmd.empty()) {
    sp0 = std::make_unique<SubprocessAgent>(p0cmd);
    a0 = sp0.get();
  } else {
    a0 = useRandom ? static_cast<Agent*>(&ra0) : static_cast<Agent*>(&fa0);
  }
  if (!p1cmd.empty()) {
    sp1 = std::make_unique<SubprocessAgent>(p1cmd);
    a1 = sp1.get();
  } else {
    a1 = useRandom ? static_cast<Agent*>(&ra1) : static_cast<Agent*>(&fa1);
  }
  e.set_agent(P0, a0);
  e.set_agent(P1, a1);
  if (!recordPath.empty()) e.start_recording();

  e.run();

  std::printf("seed=%llu turns=%d winner=%d hash=%llu\n",
              static_cast<unsigned long long>(cfg.seed), e.st.turn, e.st.winner,
              static_cast<unsigned long long>(e.state_hash()));
  for (int i = 0; i < 2; ++i) {
    const PlayerState& s = e.st.p[i];
    std::printf("P%d life=%d aura=%d flare=%d vigor=%d hand=%zu deck=%zu cover=%zu enhance=%zu\n", i,
                s.life, s.aura, s.flare, s.vigor, s.hand.size(), s.deck.size(), s.cover.size(),
                s.enhance.size());
  }
  std::printf("dust=%d distance=%d\n", e.st.dust, e.st.distance);

  if (!recordPath.empty()) {
    std::ofstream out(recordPath);
    out << e.journal_json().dump(2) << "\n";
    std::printf("recorded %zu decisions to %s\n", e.journal_json()["entries"].size(),
                recordPath.c_str());
  }
  return 0;
}
