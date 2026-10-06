#pragma once
// Shared helpers for the test translation units. Header-only so that several
// test .cpp files can link into one doctest binary.
#include <cstdio>
#include <string>
#include <vector>

#include "engine/engine.hpp"

namespace fy {
namespace test {

inline std::string find_file(const std::string& rel) {
  for (const char* pre : {"", "../", "../../"}) {
    std::string p = std::string(pre) + rel;
    if (FILE* f = std::fopen(p.c_str(), "rb")) {
      std::fclose(f);
      return p;
    }
  }
  return rel;
}

inline const std::vector<const char*>& standard_files() {
  static const std::vector<const char*> files = {
      "content/yurina.lua", "content/saine.lua",  "content/himika.lua",
      "content/tokoyo.lua", "content/oboro.lua",  "content/yukihi.lua",
      "content/shinra.lua", "content/hagane.lua", "content/chikage.lua",
      "content/kururu.lua", "content/thallya.lua", "content/raira.lua"};
  return files;
}

inline void load_hajimari(Engine& e) { e.load_content(find_file("content/hajimari.lua")); }

inline void load_standard(Engine& e) {
  for (const char* f : standard_files()) e.load_content(find_file(f));
}

// First def matching a set-id or goddess-id plus a card name.
inline int find_def(const Engine& e, const std::string& setOrGoddess, const std::string& name) {
  for (const auto& d : e.defs)
    if ((d.set == setOrGoddess || d.goddess == setOrGoddess) && d.name == name) return d.id;
  return -1;
}

// 36 crystals exist unless a card explicitly takes some from outside the game.
inline long long crystals_total(const Engine& e) {
  long long t = 0;
  for (int i = 0; i < 2; ++i) t += e.st.p[i].life + e.st.p[i].aura + e.st.p[i].flare;
  t += e.st.distance + e.st.dust;
  for (const auto& ci : e.st.insts) t += ci.crystals;
  return t;
}

}  // namespace test
}  // namespace fy
