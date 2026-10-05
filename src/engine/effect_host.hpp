#pragma once
// The Lua effect layer. All card *behaviour* lives here (or rather, in the Lua
// modules this host loads); the engine itself never mentions a concrete card.
#include <memory>
#include <string>
#include <vector>

#include "core/types.hpp"
#include "engine/sol_include.hpp"

namespace fy {

struct CardDef;
class Engine;
struct Attack;

// A single attack, fully evaluated (dynamic values already resolved).
struct EvaluatedAttack {
  Range range;
  Damage damage;
  uint32_t keywords = 0;
  int evade = 0;                 // 问答: defender may cover N to take no damage
  bool attackerChooses = false;  // 畏掠: attacker picks the damage side
};

// Opaque per-card trigger spec (defined in the .cpp).
struct TriggerSpec;

// How (and whether) a special card resets itself.
//   kind: 0 = none, 1 = end-of-owner's-turn, 2 = immediate
//   lifeThreshold >= 0  -> declarative 即再起: reset when owner loses that much
//                          life in a single instance.
//   hasCond             -> a Lua predicate to evaluate instead.
struct ResetInfo {
  int kind = 0;                   // 0 none, 1 end-of-turn, 2 immediate
  int lifeThreshold = -1;
  bool hasCond = false;
  std::string trigger;            // event-based immediate reset (e.g. "weapon_switched")
};

class EffectHost {
 public:
  EffectHost();
  ~EffectHost();

  // Load a Lua module that returns a list of card tables; append the resulting
  // CardDefs to `defs`.
  void load_file(const std::string& path, std::vector<CardDef>& defs);

  bool has(int defId, const char* hook) const;

  // Invoke a per-card hook (on_play / on_enter / on_discard / on_attack_after /
  // on_use_after). `who` is the controller, `inst` the resolving instance.
  void call(Engine& e, int defId, const char* hook, Player who, int inst);

  // Evaluate the card's `attack` field (numbers, a table, or a Lua function).
  EvaluatedAttack eval_attack(Engine& e, int defId, Player who, int inst, bool asResponse);

  // Run this card's `continuous` hooks whose query == "attack".
  void run_continuous_attack(Engine& e, int defId, Player who, int inst, Attack& a);

  // Apply the attacker's pending "next attack" modifiers and all active
  // continuous attack modifiers. `consumePending` removes matched modifiers.
  void finalize_attack(Engine& e, Player attacker, Attack& a, bool consumePending);

  bool has_continuous(int defId) const;
  bool has_hook(int defId, const char* hook) const;

  // Shinra 计略 (the 神算/鬼谋 effect of a card), executed with a forced branch.
  bool has_keiryo(int defId) const;
  void call_keiryo(Engine& e, int defId, Player who, int inst, int branch);

  // Dynamic 切札 cost / playability / response capability.
  int eval_cost(Engine& e, int defId, Player who, int inst);
  bool eval_pred(Engine& e, int defId, const char* hook, Player who, int inst);

  ResetInfo reset_info(int defId) const;
  bool eval_reset_cond(Engine& e, int defId, Player who, int inst);

  // Clear pending "next attack" modifiers; endOfTurnOnly keeps non-expiring ones.
  void clear_pending_mods(bool endOfTurnOnly);

  // Fire a named event; runs matching `triggers` on every active card (APNAP).
  void fire(Engine& e, const std::string& event, Player subject, Attack* atk, int card, bool first);

  // Run callbacks registered via ctx:on_resolve() for an attack that just resolved.
  void run_after_attack(Engine& e, Attack* a);

  // Apply a CP part effect: hook is "apply" (immediate) or "after" (attack-after);
  // n is the number of selected additional parts.
  void apply_part(Engine& e, int defId, Player who, Attack& a, int n, const char* hook);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fy
