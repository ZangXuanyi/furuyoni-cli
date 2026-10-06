-- 24-Shisui 桑畑志水
-- 【象征武器】锯（O）。
-- 【机制】裂伤～当你的准备阶段开始时，把双方场上所有裂伤指示物以任意顺序伤害化。
--   裂伤指示物不占据位置，可以出现在任意一方（敌我）的装/气/命；每个指示物记录
--   来源玩家。同一「目标 + 区域 + 来源」的裂伤合并为 1 次伤害：
--   装→虚 / 气→虚 / 命→气。多个区域的结算顺序由造成这些裂伤的玩家决定。
--   某玩家命区域中来自同一玩家的裂伤数 > 其命时，塞入的那一刻立即伤害化该组。
--   造成裂伤 ≠ 造成伤害；只有裂伤伤害化时才算造成伤害，并计入「本回合受到伤害
--   的次数」。
-- 引擎侧支持：ctx:wound / wound_count / resolve_wounds / resolve_wound /
--   wound_attack / damage_taken_this_turn / no_death，
--   Attack:wound，CardDef.woundCost（Lua wound_cost {X} = 向自气放 X 个裂伤）,
--   CardDef.noDeath（埋骨地），Attack.wound（Lua wounds = true / {aura=,life=}）。

return {

  ---------------------------------------------------------------------------
  -- 锯 O 常规牌
  ---------------------------------------------------------------------------
  -- 【2-3 3/1】白板。
  { set = "shisui", form = "O", num = 1, name = "锯", kind = "normal", type = "attack",
    attack = { range = { 2, 3 }, damage = { aura = 3, life = 1 } } },

  -- 【2-3 {1/1}】攻击后：进行攻击"【2-3 {1/2}】若敌装中的裂伤大于敌装，
  -- 则对手不能用装承受该伤害"。
  { set = "shisui", form = "O", num = 2, name = "利刃", kind = "normal", type = "attack",
    attack = { range = { 2, 3 }, damage = { aura = 1, life = 1 }, wounds = true },
    on_attack_after = function(ctx)
      ctx:attack {
        range = { 2, 3 },
        wounds = function(c)
          -- 「敌装中的裂伤大于敌装」→ 对手不能用装承受该伤害（只能吃 2 命裂伤）。
          if c:wound_count(c:opp(), "aura") > c:aura(c:opp()) then
            return { life = 2 }
          end
          return { aura = 1, life = 2 }
        end,
      }
    end },

  -- 对应【2-4 1/1】若本回合内你受到过伤害，则此攻击获得 +1/+1；
  -- 若本回合内你受到过至少两次伤害，则 1 虚到装。
  { set = "shisui", form = "O", num = 3, name = "叛乱", kind = "normal", type = "attack",
    response = true,
    attack = { range = { 2, 4 }, damage = { aura = 1, life = 1 } },
    on_play = function(ctx)
      local me = ctx:player()
      local n = ctx:damage_taken_this_turn(me)
      if n >= 1 then
        local src = ctx:source_inst()
        ctx:next_attack_mod {
          match = function(c, atk) return atk:source_inst() == src end,
          apply = function(c, atk) atk:add { aura = 1, life = 1 } end,
          this_turn = true,
        }
      end
      if n >= 2 then
        ctx:move("dust", "aura", 1, me, me)
      end
    end },

  -- 全力【2-5 {2/3}】攻击后：对手畏缩。攻击后：对自装、自气或自命造成 1 裂伤（你选）。
  { set = "shisui", form = "O", num = 4, name = "彻底抗战", kind = "normal", type = "attack",
    full_power = true,
    attack = { range = { 2, 5 }, damage = { aura = 2, life = 3 }, wounds = true },
    on_attack_after = function(ctx)
      ctx:cower(ctx:opp())
      local me = ctx:player()
      local pick = ctx:choose("彻底抗战：对哪个区域造成 1 裂伤？", { "自装", "自气", "自命" })
      local area = (pick == 2 and "flare") or (pick == 3 and "life") or "aura"
      ctx:wound(me, area, 1)
    end },

  -- 2距到虚。对自装或自气造成 1 裂伤；若当前距离等于 0 则改为对自命造成 1 裂伤。
  { set = "shisui", form = "O", num = 5, name = "荆棘之路", kind = "normal", type = "action",
    on_play = function(ctx)
      ctx:move("distance", "dust", 2)
      local me = ctx:player()
      if ctx:distance() == 0 then
        ctx:wound(me, "life", 1)
      else
        local pick = ctx:choose("荆棘之路：对哪个区域造成 1 裂伤？", { "自装", "自气" })
        ctx:wound(me, pick == 1 and "aura" or "flare", 1)
      end
    end },

  -- 限制距离0-4。执行两次装附，然后你从自装或自气中选择一项、对手从敌装或敌气中
  -- 选择一项，选定的所有区域各受到 1 裂伤。
  { set = "shisui", form = "O", num = 6, name = "旌旗护身", kind = "normal", type = "action",
    limit_distance = { 0, 4 },
    on_play = function(ctx)
      local me = ctx:player()
      local opp = ctx:opp()
      ctx:free_basics_of(me, 2, { "aura" })
      local mine = ctx:choose("旌旗护身：自装或自气？", { "自装", "自气" })
      ctx:wound(me, mine == 1 and "aura" or "flare", 1)
      local theirs = ctx:choose_for(opp, "旌旗护身：敌装或敌气？", { "敌装", "敌气" })
      ctx:wound(opp, theirs == 0 and "aura" or "flare", 1)
    end },

  -- 对应。选择至多 X 项，X = 本回合内你受到伤害的次数：
  -- 被对应的攻击 +0/-1；对手畏缩；对手从装/气/命中选一项，你对选定区域造成 1 裂伤。
  { set = "shisui", form = "O", num = 7, name = "青色的羁绊", kind = "normal", type = "action",
    response = true,
    on_play = function(ctx)
      local me = ctx:player()
      local opp = ctx:opp()
      local x = ctx:damage_taken_this_turn(me)
      if x <= 0 then return end
      local atk = ctx:responding_attack()
      local opts, keys = {}, {}
      if atk then
        opts[#opts + 1] = "被对应的攻击获得 +0/-1"
        keys[#keys + 1] = "attack"
      end
      opts[#opts + 1] = "对手畏缩"
      keys[#keys + 1] = "cower"
      opts[#opts + 1] = "对手选择区域，你对其造成 1 裂伤"
      keys[#keys + 1] = "wound"
      local n = math.min(x, #opts)
      local picks = ctx:choose_options("青色的羁绊：选择至多 " .. n .. " 项", opts, 0, n)
      for _, i in ipairs(picks) do
        local k = keys[i + 1]
        if k == "attack" then
          atk:add { aura = 0, life = -1 }
        elseif k == "cower" then
          ctx:cower(opp)
        elseif k == "wound" then
          local sel = ctx:choose_for(opp, "青色的羁绊：选择区域", { "装", "气", "命" })
          local area = (sel == 0 and "aura") or (sel == 1 and "flare") or "life"
          ctx:wound(opp, area, 1)
        end
      end
    end },

  ---------------------------------------------------------------------------
  -- 锯 O 切札
  ---------------------------------------------------------------------------
  -- 对应。若此牌对应了攻击，则先结算攻击（而不是这张牌）。你选择任意多的区域，
  -- 把选定区域内的裂伤指示物伤害化。然后进行攻击"【1-4 2/(1+X)】通常牌不可对，
  -- X = 本回合内你受到伤害次数的一半（向上取整）"。
  { set = "shisui", form = "O", num = 1, name = "红莲钻心", kind = "special", type = "action",
    cost = 3, response = true,
    on_play = function(ctx)
      -- 先结算攻击（on_resolve 在攻击结算完毕后回调），再执行本牌的效果。
      local function effect(c)
        local me = c:player()
        local areas, names = {}, {}
        for _, t in ipairs({ me, c:opp() }) do
          for _, a in ipairs({ "aura", "flare", "life" }) do
            if c:wound_count(t, a) > 0 then
              areas[#areas + 1] = { t, a }
              names[#names + 1] = ((t == me) and "自" or "敌") ..
                  (a == "aura" and "装" or (a == "flare" and "气" or "命"))
            end
          end
        end
        if #areas > 0 then
          local picks = c:choose_options("红莲钻心：选择要伤害化的区域", names, 0, #areas)
          for _, i in ipairs(picks) do
            c:resolve_wound(areas[i + 1][1], areas[i + 1][2])
          end
        end
        c:attack {
          range = { 1, 4 },
          damage = { aura = 2, life = function(cc)
            return 1 + math.ceil(cc:damage_taken_this_turn(cc:player()) / 2)
          end },
          keywords = { "no_normal_response" },
        }
      end
      if ctx:responding_attack() then
        ctx:on_resolve(function(c, atk) effect(c) end)
      else
        effect(ctx)
      end
    end },

  -- 【3 {2/1}】攻击后：若对手选择用命承伤，则你的下一次对装伤害不大于 2 的攻击
  -- 伤害改为造成裂伤。再起：你回合结束时装+气不大于 6。
  { set = "shisui", form = "O", num = 2, name = "青莲裂肤", kind = "special", type = "attack",
    cost = 2, wound_cost = 2,
    attack = { range = { 3, 3 }, damage = { aura = 2, life = 1 }, wounds = true },
    on_attack_after = function(ctx)
      if ctx:last_attack_side() == 2 then  -- 对手选择了用命承伤
        ctx:next_attack_mod {
          match = function(c, atk)
            if atk:attacker() ~= c:player() then return false end
            local a = atk:aura_damage()
            return a ~= nil and a <= 2
          end,
          apply = function(c, atk) atk:wound() end,
          this_turn = false,
        }
      end
    end,
    reset = { kind = "end_turn",
      cond = function(ctx)
        return ctx:aura(ctx:player()) + ctx:flare(ctx:player()) <= 6
      end } },

  -- 对应。若此牌对应了一个攻击，则将该攻击裂伤化（该攻击的 X/Y 伤害变为
  -- {X/Y} 裂伤）。即再起：你在一回合内受到了第三次伤害。
  { set = "shisui", form = "O", num = 3, name = "寒疮噬身", kind = "special", type = "action",
    cost = 2, response = true,
    on_play = function(ctx)
      local atk = ctx:responding_attack()
      if atk then atk:wound() end
    end,
    reset = { kind = "immediate",
      cond = function(ctx) return ctx:damage_taken_this_turn(ctx:player()) >= 3 end } },

  -- 全力【纳(2+X)】展开时：X = 使用这张牌时敌气比自气多的数量（敌气不多于自气
  -- 则 0）。展开中：你不会死亡；当你的命为 0 时，对手的集中力视为 0。
  { set = "shisui", form = "O", num = 4, name = "桑畑志水的埋骨地", kind = "special",
    type = "enhance",
    cost = 2, wound_cost = 2, full_power = true, no_death = true,
    nagi = function(ctx)
      local x = ctx:flare(ctx:opp()) - ctx:flare(ctx:player())
      if x < 0 then x = 0 end
      return 2 + x
    end },

}
