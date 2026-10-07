// 05-Oboro 缕: 零件 / 组装 / 电子设置
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "engine/engine_internal.hpp"
#include "engine/effect_host.hpp"
#include "engine/effect_ctx.hpp"

namespace fy {
using namespace detail;  // NOLINT

void Engine::do_electronic_setup(Player p) {
  std::vector<int> cores, adds;
  for (int inst : assembled_parts(p)) {
    if (def_of(inst).corePart)
      cores.push_back(inst);
    else
      adds.push_back(inst);
  }
  if (cores.empty()) return;

  Request r;
  r.kind = "option";
  r.prompt = "电子设置：选择核心零件";
  for (int inst : cores) r.options.push_back({def_of(inst).name, true, {}});
  int c0 = ask_one(p, std::move(r));
  c0 = std::min(c0, static_cast<int>(cores.size()) - 1);
  int core = cores[static_cast<size_t>(c0)];

  std::vector<int> chosenAdds;
  if (!adds.empty()) {
    Request ar;
    ar.kind = "cards";
    ar.prompt = "电子设置：选择任意数量的附加零件";
    for (int inst : adds) {
      Option o;
      o.label = def_of(inst).name;
      o.data = {{"inst", inst}};
      ar.options.push_back(o);
    }
    ar.minSel = 0;
    ar.maxSel = static_cast<int>(adds.size());
    Decision d = decide(p, std::move(ar));
    for (int i : d.indices)
      if (i >= 0 && i < static_cast<int>(adds.size())) chosenAdds.push_back(adds[static_cast<size_t>(i)]);
  }

  Attack a = make_attack(p, core, false, true);
  int n = static_cast<int>(chosenAdds.size());
  for (int cp : chosenAdds) effects_->apply_part(*this, ci(cp).def, p, a, n, "apply");
  resolve_attack(a);
  if (a.hit) {
    for (int cp : chosenAdds) effects_->apply_part(*this, ci(cp).def, p, a, n, "after");
    // The core part's own 攻击后 text (e.g. 核心零件Z) must resolve too.
    if (effects_->has(ci(core).def, "on_attack_after"))
      effects_->call(*this, ci(core).def, "on_attack_after", p, core);
  }
  disassemble_part(p, core);
  for (int cp : chosenAdds) disassemble_part(p, cp);
  check_win();
}

int Engine::assemble_one(Player p) {
  auto u = unassembled_parts(p);
  if (u.empty()) return -1;
  Request r;
  r.kind = "option";
  r.prompt = "组装一个零件";
  for (int inst : u) r.options.push_back({def_of(inst).name, true, {}});
  int idx = ask_one(p, std::move(r));
  idx = std::min(idx, static_cast<int>(u.size()) - 1);
  int inst = u[static_cast<size_t>(idx)];
  assemble_part(p, inst);
  return inst;
}

void Engine::disassemble_to(Player p, int maxCount) {
  while (assembled_count(p) > maxCount && !st.over) {
    auto a = assembled_parts(p);
    if (a.empty()) break;
    Request r;
    r.kind = "option";
    r.prompt = "拆除一个零件";
    for (int inst : a) r.options.push_back({def_of(inst).name, true, {}});
    int idx = ask_one(p, std::move(r));
    idx = std::min(idx, static_cast<int>(a.size()) - 1);
    disassemble_part(p, a[static_cast<size_t>(idx)]);
  }
}

void Engine::assemble_many(Player p, int x) {
  for (int i = 0; i < x; ++i) {
    if (unassembled_parts(p).empty()) break;
    assemble_one(p);
  }
}

void Engine::init_parts(Player p) {
  ps(p).parts.clear();
  for (const CardDef& d : defs)
    if (d.isPart && d.goddess == "oboro") {
      int inst = add_instance(d.id, p);
      move_card(inst, Zone::Parts);
      ci(inst).assembled = false;
    }
}

int Engine::assembled_count(Player p) const {
  int c = 0;
  for (int inst : ps(p).parts)
    if (ci(inst).assembled) c++;
  return c;
}

std::vector<int> Engine::unassembled_parts(Player p) const {
  std::vector<int> out;
  for (int inst : ps(p).parts)
    if (!ci(inst).assembled) out.push_back(inst);
  return out;
}

std::vector<int> Engine::assembled_parts(Player p) const {
  std::vector<int> out;
  for (int inst : ps(p).parts)
    if (ci(inst).assembled) out.push_back(inst);
  return out;
}

void Engine::set_assembled(int inst, bool v) {
  if (ci(inst).zone == Zone::Parts) ci(inst).assembled = v;
}

int Engine::part_by_def(Player p, int def) const {
  for (int inst : ps(p).parts)
    if (ci(inst).def == def) return inst;
  return -1;
}

void Engine::disassemble_part(Player p, int inst) {
  (void)p;
  set_assembled(inst, false);
}

void Engine::assemble_part(Player p, int inst) {
  set_assembled(inst, true);
  // Hard cap 5: must immediately disassemble until <= 5.
  while (assembled_count(p) > 5 && !st.over) {
    auto as = assembled_parts(p);
    if (as.empty()) break;
    Request r;
    r.kind = "option";
    r.prompt = "组装超过 5 个，必须拆除一个零件";
    for (int i : as) r.options.push_back({def_of(i).name, true, {}});
    int idx = ask_one(p, std::move(r));
    idx = std::min(idx, static_cast<int>(as.size()) - 1);
    disassemble_part(p, as[static_cast<size_t>(idx)]);
  }
}

namespace mechanics {

void parts_ctx(CtxTypes& t) {
  auto& ctx = t.ctx;

  ctx["assemble_one"] = [](LuaCtx& c, int p) { return c.e->assemble_one(static_cast<Player>(p)); };
  ctx["disassemble_to"] = [](LuaCtx& c, int p, int m) {
    c.e->disassemble_to(static_cast<Player>(p), m);
  };
  ctx["assemble_many"] = [](LuaCtx& c, int p, int x) {
    c.e->assemble_many(static_cast<Player>(p), x);
  };
  ctx["assembled_count"] = [](LuaCtx& c, int p) {
    return c.e->assembled_count(static_cast<Player>(p));
  };

}

}  // namespace mechanics
}  // namespace fy
