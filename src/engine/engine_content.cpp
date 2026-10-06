// Content module loading and ruleset ("rules pack") resolution.
//
// Modules are plain Lua files (see content/*.lua). Each module is tagged with a
// *pack* so that a preset can select which content is in play:
//   tatsujin  达人包 01-12 (bundled)
//   official  further official goddesses (全扩)
//   custom    user-authored goddesses
// Presets combine a pack selection with whether 异相 (A1/A2 forms) are allowed:
//   kigen-tatsujin   起源战达人 : tatsujin,          no 异相
//   kigen-full       起源战全扩 : tatsujin+official, no 异相   (recommended)
//   gachi-tatsujin   完全战达人 : tatsujin,          异相
//   gachi-full       完全战全扩 : tatsujin+official, 异相
// Chinese aliases (达人/全扩/起源/完全) are accepted as well, and `allowCustom`
// adds the custom pack to any preset.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/card_names.hpp"
#include "engine/effect_host.hpp"
#include "engine/engine.hpp"
#include "engine/engine_internal.hpp"

namespace fy {
using namespace detail;  // NOLINT

void Engine::load_content(const std::string& f) { load_content(f, "tatsujin"); }

void Engine::load_content(const std::string& f, const std::string& pack) {
  const size_t first = defs.size();
  effects_->load_file(f, defs);  // throws on a Lua error, before defs grow
  defPack_.resize(defs.size(), pack);
  ContentModule m;
  m.path = f;
  m.pack = pack;
  m.firstDef = static_cast<int>(first);
  m.defCount = static_cast<int>(defs.size() - first);
  for (size_t i = first; i < defs.size(); ++i) {
    const std::string& g = defs[i].goddess;
    if (!g.empty() && std::find(m.goddesses.begin(), m.goddesses.end(), g) == m.goddesses.end())
      m.goddesses.push_back(g);
  }
  modules_.push_back(std::move(m));
}

int Engine::load_content_dir(const std::string& dir, const std::string& pack) {
  namespace fs = std::filesystem;
  std::error_code ec;
  std::vector<std::string> files;
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != ".lua") continue;
    files.push_back(entry.path().string());
  }
  if (ec) return 0;
  std::sort(files.begin(), files.end());
  for (const std::string& f : files) load_content(f, pack);
  return static_cast<int>(files.size());
}

int Engine::load_manifest(const std::string& jsonFile) {
  std::ifstream in(jsonFile);
  if (!in) return 0;
  nlohmann::json j;
  try {
    in >> j;
  } catch (const std::exception&) {
    return 0;
  }
  if (!j.is_object()) return 0;
  std::string base;
  const auto slash = jsonFile.find_last_of('/');
  if (slash != std::string::npos) base = jsonFile.substr(0, slash + 1);
  int n = 0;
  for (auto it = j.begin(); it != j.end(); ++it) {
    if (!it.value().is_array()) continue;
    const std::string pack = it.key();
    for (const auto& mod : it.value()) {
      if (!mod.is_string()) continue;
      std::string rel = mod.get<std::string>();
      if (rel.size() < 4 || rel.compare(rel.size() - 4, 4, ".lua") != 0) rel += ".lua";
      load_content(base + rel, pack);
      n += 1;
    }
  }
  return n;
}

int Engine::load_combo_bans(const std::string& jsonFile) {
  std::ifstream in(jsonFile);
  if (!in) return 0;
  nlohmann::json j;
  try {
    in >> j;
  } catch (const std::exception&) {
    return 0;
  }
  if (!j.is_array()) return 0;
  int n = 0;
  for (const auto& e : j) {
    if (!e.is_object()) continue;
    ComboBan ban;
    ban.goddess_a = e.value("a", std::string());
    ban.goddess_b = e.value("b", std::string());
    ban.card = e.value("card", std::string());
    if (ban.card.empty()) continue;
    cfg.comboBans.push_back(ban);
    n += 1;
  }
  return n;
}

bool Engine::pack_allowed(const std::string& pack) const {
  if (!cfg.allowedPacks.empty())
    return std::find(cfg.allowedPacks.begin(), cfg.allowedPacks.end(), pack) !=
           cfg.allowedPacks.end();
  if (pack == "tatsujin") return true;
  const bool full = cfg.preset.find("full") != std::string::npos ||
                    cfg.preset.find("全扩") != std::string::npos;
  if (pack == "official") return full;
  if (pack == "custom") return cfg.allowCustom;
  return cfg.allowCustom;  // user-defined packs need allowCustom (or allowedPacks)
}

std::vector<std::string> Engine::allowed_packs() const {
  if (!cfg.allowedPacks.empty()) return cfg.allowedPacks;
  std::vector<std::string> packs{"tatsujin"};
  const bool full = cfg.preset.find("full") != std::string::npos ||
                    cfg.preset.find("全扩") != std::string::npos;
  if (full) packs.push_back("official");
  if (cfg.allowCustom) packs.push_back("custom");
  return packs;
}

bool Engine::variants_allowed() const {
  if (cfg.variantsOverride >= 0) return cfg.variantsOverride != 0;
  return cfg.preset.find("gachi") != std::string::npos ||
         cfg.preset.find("完全") != std::string::npos;
}

bool Engine::goddess_enabled(const std::string& g) const {
  if (cfg.enabledGoddesses.empty()) return true;
  return std::find(cfg.enabledGoddesses.begin(), cfg.enabledGoddesses.end(), g) !=
         cfg.enabledGoddesses.end();
}

std::vector<std::string> Engine::goddess_pool() const {
  std::vector<std::string> out;
  for (size_t i = 0; i < defs.size(); ++i) {
    const CardDef& d = defs[i];
    const std::string pack = i < defPack_.size() ? defPack_[i] : std::string("tatsujin");
    if (!pack_allowed(pack)) continue;
    if (d.goddess.empty() || d.form != "O") continue;
    if (!goddess_enabled(d.goddess)) continue;
    if (std::find(out.begin(), out.end(), d.goddess) == out.end()) out.push_back(d.goddess);
  }
  return out;
}

std::vector<std::string> Engine::available_forms(const std::string& g) const {
  std::vector<std::string> forms;
  if (!goddess_enabled(g)) return forms;
  auto def_ok = [&](size_t i) {
    const CardDef& d = defs[i];
    if (d.goddess != g) return false;
    const std::string pack = i < defPack_.size() ? defPack_[i] : std::string("tatsujin");
    return pack_allowed(pack);
  };
  bool has_o = false;
  for (size_t i = 0; i < defs.size(); ++i) {
    if (!def_ok(i)) continue;
    if (defs[i].form == "O") {
      has_o = true;
      break;
    }
  }
  if (!has_o) return forms;
  forms.push_back("O");
  if (!variants_allowed()) return forms;  // 起源战: 禁用异相
  for (size_t i = 0; i < defs.size(); ++i) {
    if (!def_ok(i) || defs[i].form == "O") continue;
    if (std::find(forms.begin(), forms.end(), defs[i].form) == forms.end())
      forms.push_back(defs[i].form);
  }
  return forms;
}

std::string Engine::ruleset_summary() const {
  std::string s = "preset=" + cfg.preset;
  s += " variants=";
  s += variants_allowed() ? "on" : "off";
  s += " packs=";
  const auto packs = allowed_packs();
  for (size_t i = 0; i < packs.size(); ++i) {
    if (i) s += "+";
    s += packs[i];
  }
  const auto pool = goddess_pool();
  s += " goddesses=" + std::to_string(pool.size());
  s += " modules=" + std::to_string(modules_.size());
  return s;
}

}  // namespace fy
