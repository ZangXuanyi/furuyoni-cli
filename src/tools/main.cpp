#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/engine.hpp"
#include "protocol/agent.hpp"
#include "tools/web_replay.hpp"

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

static std::vector<std::string> split_csv(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  std::istringstream in(s);
  while (std::getline(in, cur, ','))
    if (!cur.empty()) out.push_back(cur);
  return out;
}

static bool write_file(const std::string& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out << content;
  return static_cast<bool>(out);
}

int main(int argc, char** argv) {
  Config cfg;
  std::string content = "content/hajimari.lua";
  std::string p0cmd, p1cmd, recordPath, replayPath, tracePath, webPath;
  bool useRandom = false;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : std::string(); };
    if (a == "--seed") cfg.seed = std::strtoull(next().c_str(), nullptr, 10);
    else if (a == "--limit") cfg.turnLimit = std::atoi(next().c_str());
    else if (a == "--random") useRandom = true;
    else if (a == "--standard") cfg.mode = "standard";
    else if (a == "--preset") cfg.preset = next();
    else if (a == "--variants") {
      std::string v = next();
      cfg.variantsOverride = (v == "on" || v == "1" || v == "true") ? 1 : 0;
    } else if (a == "--packs") cfg.allowedPacks = split_csv(next());
    else if (a == "--allow-custom") cfg.allowCustom = true;
    else if (a == "--content-dir") cfg.contentDirs.push_back(next());
    else if (a == "--goddesses") cfg.enabledGoddesses = split_csv(next());
    else if (a == "--bans") cfg.comboBansFile = next();
    else if (a == "--p0-cmd") p0cmd = next();
    else if (a == "--p1-cmd") p1cmd = next();
    else if (a == "--record") recordPath = next();
    else if (a == "--replay") replayPath = next();
    else if (a == "--trace") tracePath = next();
    else if (a == "--web") webPath = next();
    else content = a;
  }

  const bool tracing = !tracePath.empty() || !webPath.empty();
  auto load_all = [&](Engine& e) {
    if (cfg.mode == "standard") {
      // Rules packs come from the manifest; extra module dirs use "dir:pack".
      if (!cfg.packsFile.empty()) e.load_manifest(resolve(cfg.packsFile));
      if (!cfg.comboBansFile.empty()) e.load_combo_bans(resolve(cfg.comboBansFile));
      for (const std::string& d : cfg.contentDirs) {
        auto c = d.find(':');
        std::string dir = c == std::string::npos ? d : d.substr(0, c);
        std::string pack = c == std::string::npos ? "custom" : d.substr(c + 1);
        e.load_content_dir(resolve(dir), pack);
      }
    } else {
      e.load_content(resolve(content));
    }
  };

  auto emit = [&](Engine& e) {
    if (!tracePath.empty()) {
      if (!write_file(tracePath, e.trace_json().dump(2))) {
        std::fprintf(stderr, "cannot write trace %s\n", tracePath.c_str());
      } else {
        std::printf("trace written: %s (%zu frames)\n", tracePath.c_str(),
                    e.trace_json()["frames"].size());
      }
    }
    if (!webPath.empty()) {
      if (!write_file(webPath, render_replay_html(e.trace_json()))) {
        std::fprintf(stderr, "cannot write web %s\n", webPath.c_str());
      } else {
        std::printf("replay viewer written: %s\n", webPath.c_str());
      }
    }
  };

  if (!replayPath.empty()) {
    std::ifstream rf(replayPath);
    if (!rf) {
      std::fprintf(stderr, "cannot open replay %s\n", replayPath.c_str());
      return 1;
    }
    nlohmann::json j;
    rf >> j;
    cfg.seed = j.value("seed", cfg.seed);
    cfg.mode = j.value("mode", cfg.mode);
    Engine e(cfg);
    load_all(e);
    e.load_journal(j);
    if (tracing) e.start_trace();
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
    if (tracing) emit(e);
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
  if (tracing) e.start_trace();

  try {
    e.run();
  } catch (const std::exception& ex) {
    std::fprintf(stderr, "game failed: %s\n", ex.what());
    return 1;
  }

  std::printf("ruleset: %s\n", e.ruleset_summary().c_str());
  std::printf("seed=%llu turns=%d winner=%d hash=%llu\n",
              static_cast<unsigned long long>(cfg.seed), e.st.turn, e.st.winner,
              static_cast<unsigned long long>(e.state_hash()));
  std::printf("%s\n", e.result_text().c_str());
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
  if (tracing) emit(e);
  return 0;
}
