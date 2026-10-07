#pragma once
// Internal helpers shared by the engine translation units. Not part of the
// public engine API.
#include <algorithm>
#include <string>
#include <vector>

#include "engine/engine.hpp"

namespace fy {
namespace detail {

inline std::string dmg_str(const Damage& d) {
  auto s = [](const std::optional<int>& o) { return o ? std::to_string(*o) : std::string("-"); };
  return s(d.aura) + "/" + s(d.life);
}

inline std::string card_label(const CardDef& d) {
  std::string s = d.name;
  if (d.kind == CardKind::Special && d.cost >= 0) s += "(" + std::to_string(d.cost) + ")";
  if (d.hasAttack)
    s += " [" + d.attack.range.to_string() + " " + dmg_str(d.attack.damage) + "]";
  else if (d.nagi >= 0)
    s += " [纳" + std::to_string(d.nagi) + "]";
  return s;
}

inline nlohmann::json card_json(const CardDef& d) {
  nlohmann::json j;
  j["def"] = d.id;
  j["name"] = d.name;
  j["set"] = d.set;
  j["kind"] = d.kind == CardKind::Normal ? "normal" : "special";
  j["type"] = d.type == CardType::Attack   ? "attack"
              : d.type == CardType::Enhance ? "enhance"
                                            : "action";
  j["cost"] = d.cost;
  j["nagi"] = d.nagi;
  j["full_power"] = (d.flags & CF_FullPower) != 0;
  j["response"] = (d.flags & CF_Response) != 0;
  j["break"] = (d.flags & CF_Break) != 0;
  j["terminal"] = (d.flags & CF_Terminal) != 0;
  if (d.hasAttack) {
    j["range"] = d.attack.range.to_string();
    j["damage"] = dmg_str(d.attack.damage);
    j["unrespondable"] = (d.attack.keywords & AF_Unrespondable) != 0;
    j["lock"] = (d.attack.keywords & AF_Lock) != 0;
    j["overwhelm"] = (d.attack.keywords & AF_Overwhelm) != 0;
  }
  j["text"] = d.text;
  return j;
}

inline std::vector<int>* zone_ptr(GameState& st, Player owner, Zone z) {
  switch (z) {
    case Zone::Deck:    return &st.p[owner].deck;
    case Zone::Hand:    return &st.p[owner].hand;
    case Zone::Discard: return &st.p[owner].discard;
    case Zone::Cover:   return &st.p[owner].cover;
    case Zone::Enhance: return &st.p[owner].enhance;
    case Zone::Special: return &st.p[owner].special;
    case Zone::Parts:   return &st.p[owner].parts;
    case Zone::Sealed:  return nullptr;
    case Zone::Bag:     return &st.p[owner].bag;
    case Zone::Memory:  return &st.p[owner].memory;
    case Zone::Removed: return nullptr;
    case Zone::Limbo:   return nullptr;
  }
  return nullptr;
}

inline bool vec_has(const std::vector<int>& v, int x) {
  return std::find(v.begin(), v.end(), x) != v.end();
}

// 女神 + 形态 -> 显示名（刀/古/心 ...），用于结果串。
inline std::string goddess_display(const std::string& g, const std::string& form) {
  if (g == "yurina") return form == "A1" ? "古" : form == "A2" ? "心" : "刀";
  if (g == "saine") return form == "A1" ? "琵" : form == "A2" ? "拒" : "薙";
  if (g == "himika") return form == "A1" ? "炎" : "铳";
  if (g == "tokoyo") return form == "A1" ? "笛" : form == "A2" ? "恐" : "扇";
  if (g == "oboro") return form == "A1" ? "战" : form == "A2" ? "电" : "忍";
  if (g == "yukihi") return form == "A1" ? "社" : "雪";
  if (g == "shinra") return form == "A1" ? "经" : "书";
  if (g == "hagane") return form == "A1" ? "金" : "锤";
  if (g == "chikage") return form == "A1" ? "绊" : "毒";
  if (g == "kururu") return form == "A1" ? "机" : form == "A2" ? "友" : "络";
  if (g == "raira") return form == "A1" ? "岚" : "爪";
  if (g == "thallya") return form == "A1" ? "新" : "骑";
  if (g == "utsuro") return form == "A1" ? "尘" : "镰";
  if (g == "honoka") return form == "A1" ? "勾" : "旗";
  if (g == "konuru") return form == "A1" ? "炼" : "橇";
  if (g == "yatsuha") return form == "AA1" ? "魂" : (form == "A1" ? "花" : "镜");
  if (g == "kamuwi") return "剑";
  if (g == "hatsumi") return form == "A1" ? "信" : "桨";
  if (g == "mizuki") return "兜";
  if (g == "megumi") return form == "A1" ? "端" : "棹";
  if (g == "kanawe") return "面";
  if (g == "renri") return form == "A1" ? "遗" : "衣";
  if (g == "misora") return "弓";
  if (g == "shisui") return "锯";
  if (g == "akina") return "算";
  if (g == "innealra") return form == "A1" ? "现" : (form == "A2" ? "未" : "过");
  return g;
}

// 结算栈帧守卫：构造时压栈（快照结算语境），析构时弹栈并恢复语境。
struct PlayFrameGuard {
  Engine& e;
  ~PlayFrameGuard() { e.pop_play_frame(); }
};

}  // namespace detail
}  // namespace fy
