#pragma once
// Lua 绑定的核心值类型与共享工具（ctx / attack / cost / event），供 effect_host
// 与各机制模块（mechanics/*.cpp）共享——机制框架进 C++（what.md 第 2 条），
// 各自的 Lua API 面也由自己的源文件注册（mechanics/blocks.cpp 汇总）。
#include <string>

#include "core/types.hpp"
#include "engine/sol_include.hpp"

namespace fy {

class Engine;
struct Attack;

struct LuaCtx {
  Engine* e = nullptr;
  Player who = P0;
  int source = -1;
};

struct LuaAttack {
  Attack* a = nullptr;
  Engine* e = nullptr;
};

// A mutable cost value handed to `continuous` query="cost" auras.
struct LuaCost {
  int value = 0;
};

struct LuaEvent {
  Engine* e = nullptr;
  std::string type;
  Player subject = P0;
  Attack* atk = nullptr;
  int card = -1;
  bool first = false;
};

// 机制模块注册的 ctx/attack 方法块。EffectHost 构造完通用绑定后统一调用
// （见 mechanics/blocks.cpp）。
struct CtxTypes {
  sol::usertype<LuaCtx>& ctx;
  sol::usertype<LuaAttack>& attack;
};
using CtxBlock = void (*)(CtxTypes&);
void run_mechanic_ctx_blocks(CtxTypes& t);

// ---- Lua 字符串 → 引擎值 的共享解析 ---------------------------------------

inline AreaRef area_of(const std::string& s, Player p) {
  if (s == "life") return AreaRef::life(p);
  if (s == "aura") return AreaRef::aura(p);
  if (s == "flare") return AreaRef::flare(p);
  if (s == "distance") return AreaRef::distance();
  if (s == "dust") return AreaRef::dust();
  if (s == "market") return AreaRef::market(p);  // 23-Akina 股市
  if (s == "waku") return AreaRef::waku(p);      // 26-Innealra 惑
  return AreaRef::dust();
}

// 24-Shisui 裂伤指示物所在的区域（0=装 / 1=气 / 2=命；-1 = 非法）。
inline int wound_area_of(const std::string& s) {
  if (s == "aura") return kWoundAura;
  if (s == "flare") return kWoundFlare;
  if (s == "life") return kWoundLife;
  return -1;
}

inline BasicAction parse_basic(const std::string& s) {
  if (s == "retreat") return BasicAction::Retreat;
  if (s == "aura") return BasicAction::Aura;
  if (s == "flare") return BasicAction::Flare;
  if (s == "escape") return BasicAction::Escape;
  return BasicAction::Advance;
}

}  // namespace fy
