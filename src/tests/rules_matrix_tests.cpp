// Data audit: every statically declared attack in rules/0*.md must match the
// corresponding content/*.lua CardDef (range + damage). Dynamic attacks,
// generated attacks and non-attack cards are skipped. This turns "the rules doc
// and the content drifted apart" into a test failure.
#include <doctest/doctest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "engine/card_names.hpp"
#include "engine/engine.hpp"
#include "tests/test_util.hpp"

using namespace fy;
using namespace fy::test;

namespace {

// UTF-8 punctuation used by the rules docs.
const char* kColonFull = "\uFF1A";   // ：
const char* kOpenFull = "\uFF08";    // （
const char* kCloseFull = "\uFF09";   // ）
const char* kOpenBracket = "\u3010"; // 【
const char* kCloseBracket = "\u3011";// 】
const char* kStopMark = "\u3002";    // 。

std::vector<std::string> read_lines(const std::string& path) {
  std::vector<std::string> out;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) out.push_back(line);
  return out;
}

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r");
  return s.substr(a, b - a + 1);
}

std::string ends_with(const std::string& s, const char* suffix) {
  size_t n = std::strlen(suffix);
  return s.size() >= n ? s.substr(s.size() - n) : std::string();
}

// "01-yurina.md" -> "content/yurina.lua"; "00-hajimari.md" -> content/hajimari.lua
std::string content_for_rules(const std::string& fname) {
  auto dash = fname.find('-');
  auto md = fname.rfind(".md");
  if (dash == std::string::npos || md == std::string::npos || md <= dash) return "";
  return "content/" + fname.substr(dash + 1, md - dash - 1) + ".lua";
}

// Extract the card name of a "- <ID> <name>..." rules line ("" if none).
std::string card_name_of(const std::string& line) {
  if (line.rfind("- ", 0) != 0) return "";
  size_t i = 2;
  size_t idStart = i;
  while (i < line.size() &&
         (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '-'))
    ++i;
  if (i == idStart) return "";
  while (i < line.size() && line[i] == ' ') ++i;
  size_t end = line.size();
  for (const char* sep : {kColonFull, ":", kOpenBracket}) {
    size_t p = line.find(sep, i);
    if (p != std::string::npos && p < end) end = p;
  }
  std::string name = trim(line.substr(i, end - i));
  // strip a trailing （...）
  auto lp = name.rfind(kOpenFull);
  if (lp != std::string::npos && ends_with(name, kCloseFull) == kCloseFull)
    name = trim(name.substr(0, lp));
  return name;
}

struct RuleStat {
  std::set<int> range;
  std::optional<int> aura, life;
};

// Parse the card's own 【range damage】 block. Returns false when the entry is not
// a plainly-stated own attack (generated attack, 纳N, dynamic "X" damage, ...).
bool parse_stat(const std::string& line, RuleStat& out) {
  std::string name = card_name_of(line);
  if (name.empty()) return false;
  // restart from the name to find the stat block
  size_t namePos = line.find(name);
  if (namePos == std::string::npos) return false;
  std::string rest = line.substr(namePos + name.size());
  static const char* kStops[] = {"\u653B\u51FB\u540E", "\u5C55\u5F00\u65F6",
                                 "\u5C55\u5F00\u4E2D", "\u5F03\u7F6E\u65F6",
                                 "\u4F7F\u7528\u540E", "\u518D\u8D77",
                                 "\u5373\u518D\u8D77", kStopMark};
  size_t stop = rest.size();
  for (const char* k : kStops) {
    size_t p = rest.find(k);
    if (p != std::string::npos && p < stop) stop = p;
  }
  std::string head = rest.substr(0, stop);
  size_t lb = head.find(kOpenBracket);
  size_t rb = head.find(kCloseBracket);
  if (lb == std::string::npos || rb == std::string::npos || rb < lb) return false;
  std::string block = head.substr(lb + 3, rb - lb - 3);
  if (block.find('/') == std::string::npos) return false;  // 纳N / no damage side

  std::istringstream iss(block);
  std::vector<std::string> toks;
  std::string t;
  while (iss >> t) toks.push_back(t);
  if (toks.size() < 2) return false;

  std::string rs;
  for (char c : toks.front())
    if (std::isdigit(static_cast<unsigned char>(c)) || c == ',' || c == '-') rs += c;
  if (rs.empty()) return false;
  if (rs.find('-') != std::string::npos) {
    auto d = rs.find('-');
    int lo = std::atoi(rs.substr(0, d).c_str());
    int hi = std::atoi(rs.substr(d + 1).c_str());
    if (hi < lo) return false;
    for (int v = lo; v <= hi; ++v) out.range.insert(v);
  } else if (rs.find(',') != std::string::npos) {
    std::string cur;
    std::istringstream cs(rs);
    while (std::getline(cs, cur, ','))
      if (!cur.empty()) out.range.insert(std::atoi(cur.c_str()));
  } else {
    out.range.insert(std::atoi(rs.c_str()));
  }

  std::string dmg = toks.back();
  auto slash = dmg.find('/');
  if (slash == std::string::npos) return false;
  auto side = [](const std::string& s) -> std::optional<std::optional<int>> {
    if (s == "-") return std::optional<std::optional<int>>(std::nullopt);
    bool digits = !s.empty();
    for (char c : s)
      if (!std::isdigit(static_cast<unsigned char>(c))) digits = false;
    if (!digits) return std::nullopt;  // "X", "(8-X)" ... not comparable
    return std::optional<std::optional<int>>(std::atoi(s.c_str()));
  };
  auto a = side(dmg.substr(0, slash));
  auto l = side(dmg.substr(slash + 1));
  if (!a || !l) return false;
  out.aura = *a;
  out.life = *l;
  return true;
}

std::set<int> def_range_points(const CardDef& d) {
  std::set<int> s;
  for (auto& sp : d.attack.range.spans)
    for (int i = sp.first; i <= sp.second; ++i) s.insert(i);
  return s;
}

}  // namespace

TEST_CASE("rules docs and content agree on every static attack") {
  const char* files[] = {"rules/00-hajimari.md", "rules/01-yurina.md", "rules/02-saine.md",
                         "rules/03-himika.md",   "rules/04-tokoyo.md", "rules/05-oboro.md",
                         "rules/06-yukihi.md",   "rules/07-shinra.md", "rules/08-hagane.md",
                         "rules/09-chikage.md",  "rules/10-kururu.md", "rules/11-thallya.md",
                         "rules/12-raira.md",    "rules/13-utsuro.md", "rules/14-honoka.md",
                         "rules/15-konuru.md",   "rules/16-yatsuha.md", "rules/17-hatsumi.md",
                         "rules/18-mizuki.md",   "rules/19-megumi.md", "rules/20-kanawe.md",
                         "rules/21-kamuwi.md",   "rules/22-renri.md",  "rules/23-akina.md",
                         "rules/24-shisui.md",   "rules/25-misora.md", "rules/26-innealra.md"};
  int checked = 0, skipped = 0;
  for (const char* rf : files) {
    std::string rpath = find_file(rf);
    std::string cpath = find_file(content_for_rules(rf));
    if (cpath.empty() || rpath.empty()) continue;
    {  // 尚未实现的柱（有规则文件但还没内容）跳过
      std::ifstream probe(cpath);
      if (!probe) continue;
    }
    Engine e;
    e.load_content(cpath);
    for (const std::string& raw : read_lines(rpath)) {
      std::string line = trim(raw);
      RuleStat stat;
      if (!parse_stat(line, stat)) {
        skipped++;
        continue;
      }
      std::string name = card_name_of(line);
      std::vector<const CardDef*> cands;
      for (const CardDef& d : e.defs)
        if (d.name == name) cands.push_back(&d);
      if (cands.empty()) {
        skipped++;
        continue;
      }
      bool matched = false, comparable = false;
      for (const CardDef* d : cands) {
        if (!d->hasAttack) continue;
        bool dynamic =
            d->attack.range.spans.empty() && !d->attack.damage.aura && !d->attack.damage.life;
        if (dynamic) continue;
        comparable = true;
        if (def_range_points(*d) == stat.range && d->attack.damage.aura == stat.aura &&
            d->attack.damage.life == stat.life) {
          matched = true;
          break;
        }
      }
      if (!comparable) {
        skipped++;
        continue;
      }
      checked++;
      INFO("rules file: ", rf, " card: ", name);
      std::string msg = "rules stat does not match any content CardDef for " + name;
      CHECK_MESSAGE(matched, msg);
    }
  }
  INFO("checked=", checked, " skipped=", skipped);
  CHECK(checked > 40);  // the audit must actually be examining cards
}

TEST_CASE("content names referenced by the engine all exist") {
  Engine e;
  load_standard(e);
  const char* names[] = {cards::kXuYu,       cards::kNinbu,        cards::kSaigoNoKesshou,
                         cards::kYasha,      cards::kAshura,       cards::kSariaNoKessaku,
                         cards::kSokaiKaisou, cards::kRenseiKougeki, cards::kMetsutouDoku,
                         cards::kChikanDoku, cards::kMud,          cards::kHackDevice};
  for (const char* n : names) {
    bool found = false;
    for (const CardDef& d : e.defs)
      if (d.name == n) found = true;
    INFO("name: ", n);
    std::string msg = std::string("engine special-cases a card name that no content defines: ") + n;
    CHECK_MESSAGE(found, msg);
  }
}

// ---------------------------------------------------------------------------
// 内容 API 审计：content/*.lua 中调用的每个 ctx/atk/ev 方法必须已注册。
// 防止「删除绑定后内容残留调用」只在未测路径上炸（strictLua 才发现）。
// ---------------------------------------------------------------------------
TEST_CASE("content api audit: all called methods are bound") {
  // 从 effect_host.cpp + mechanics/*.cpp 收集已注册方法名。
  std::set<std::string> bound;
  auto collect = [&](const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return;
    std::string src;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) src.append(buf, n);
    std::fclose(f);
    size_t pos = 0;
    while ((pos = src.find("[\"", pos)) != std::string::npos) {
      size_t e = src.find('"', pos + 2);
      if (e == std::string::npos) break;
      std::string name = src.substr(pos + 2, e - pos - 2);
      if (!name.empty() && name.find(' ') == std::string::npos) bound.insert(name);
      pos = e;
    }
  };
  collect("src/engine/effect_host.cpp");
  for (const char* m : {"blocks", "steam", "ice", "wound", "market", "soil", "fate", "drama",
                        "dive", "barracks", "curse", "poison", "keiryo", "slots", "parts", "memory"}) {
    collect(std::string("src/engine/mechanics/") + m + ".cpp");
  }
  // Attack/Event 的 usertype 名不同（atkutil/ev），单独并入同一集合审计（宽松）。
  // 扫描内容侧所有 :method( 调用（排除 Lua 标准库）。
  std::set<std::string> lua_std = {"ipairs", "pairs", "tostring", "tonumber", "type",
                                   "table", "math", "string", "select", "next", "setmetatable"};
  for (const auto& entry : std::filesystem::directory_iterator("content")) {
    if (entry.path().extension() != ".lua") continue;
    std::FILE* f = std::fopen(entry.path().c_str(), "rb");
    if (!f) continue;
    std::string src;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) src.append(buf, n);
    std::fclose(f);
    size_t pos = 0;
    while ((pos = src.find(":", pos)) != std::string::npos) {
      size_t b = pos + 1;
      if (b < src.size() && std::isalpha(static_cast<unsigned char>(src[b]))) {
        size_t e = b;
        while (e < src.size() && (std::isalnum(static_cast<unsigned char>(src[e])) || src[e] == '_')) ++e;
        std::string name = src.substr(b, e - b);
        bool called = e < src.size() && (src[e] == '(' || src[e] == '{');
        if (called && pos > 0) {
          size_t vs = pos - 1;
          while (vs > 0 && (std::isalnum(static_cast<unsigned char>(src[vs])) || src[vs] == '_')) --vs;
          std::string var = src.substr(vs + 1, pos - vs - 1);
          if ((var == "ctx" || var == "c2" || var == "c" || var == "atk" || var == "a" ||
               var == "ev") &&
              !lua_std.count(name) && !bound.count(name)) {
            const std::string msg = std::string("unbound method ") + name + " in " + entry.path().string();
            CHECK_MESSAGE(false, msg);
          }
        }
        pos = e;
        continue;
      }
      ++pos;
    }
  }
}
