-- 01-Yurina 天音摇波  【机制：决死 ~ 自命<=3 时强化】
-- Phase 2 只实现本格 O；变格 A1(古刀)/A2(心) 留待 Phase 3。
-- 变格规则：选定变格后，用其所有同编号牌替代 O 的同编号牌；不能同时选多个变格。

return {

  -- 常
  { set = "yurina", form = "O", num = 1, name = "斩", kind = "normal", type = "attack",
    attack = { range = {3, 4}, damage = { aura = 3, life = 1 } } },

  -- 决死强化在打出（声明）那一刻判定并锁定。
  { set = "yurina", form = "O", num = 2, name = "一闪", kind = "normal", type = "attack",
    attack = function(ctx)
      local a = 2
      if ctx:desperation(ctx:player()) then a = a + 1 end
      return { range = {3, 3}, damage = { aura = a, life = 2 } }
    end },

  { set = "yurina", form = "O", num = 3, name = "柄击", kind = "normal", type = "attack",
    attack = { range = {1, 2}, damage = { aura = 2, life = 1 } },
    on_attack_after = function(ctx)
      if ctx:desperation(ctx:player()) then
        ctx:next_attack_mod { apply = function(ctx2, atk) atk:add { aura = 1 } end }
      end
    end },

  { set = "yurina", form = "O", num = 4, name = "居合", kind = "normal", type = "attack",
    full_power = true,
    attack = function(ctx)
      local a, l = 4, 3
      if ctx:distance() <= 2 then a, l = a - 1, l - 1 end
      return { range = {2, 4}, damage = { aura = a, life = l } }
    end },

  { set = "yurina", form = "O", num = 5, name = "身法", kind = "normal", type = "action",
    on_play = function(ctx)
      ctx:next_attack_mod { apply = function(ctx2, atk) atk:extend_far(1) end }
      if ctx:life(ctx:player()) < ctx:life(ctx:opp()) then
        ctx:free_basics(ctx:player(), 2)
      end
    end },

  { set = "yurina", form = "O", num = 6, name = "气合斩", kind = "normal", type = "enhance",
    nagi = 2, breakable = true,
    on_discard = function(ctx)
      ctx:attack { range = {1, 4}, damage = { aura = 3 }, keywords = { "unrespondable" } }
    end },

  { set = "yurina", form = "O", num = 7, name = "气焰万丈", kind = "normal", type = "enhance",
    nagi = 4, full_power = true,
    continuous = {
      { when = "expanded", query = "attack",
        apply = function(ctx, atk)
          local g = atk:source_goddess()
          if atk:attacker() == ctx:player() and ctx:desperation(ctx:player())
             and g ~= "" and g ~= "yurina" then
            atk:add { aura = 1, life = 1 }
            atk:keyword("overwhelm")
          end
        end },
    } },

  -- 切
  { set = "yurina", form = "O", num = 1, name = "月影落", kind = "special", type = "attack",
    cost = 7,
    attack = { range = {3, 4}, damage = { aura = 4, life = 4 } } },

  { set = "yurina", form = "O", num = 2, name = "浦波岚", kind = "special", type = "attack",
    cost = 3, response = true,
    attack = { range = {0, 10}, damage = { aura = 2 } },
    on_play = function(ctx)
      local a = ctx:responding_attack()
      if a then a:add { aura = -2 } end
    end },

  { set = "yurina", form = "O", num = 3, name = "浮舟宿", kind = "special", type = "action",
    cost = 2,
    on_play = function(ctx) ctx:move("dust", "aura", 5, ctx:player(), ctx:player()) end,
    -- 即再起：命从 >=4 降到 <=3 的那一刻
    reset = { kind = "immediate", cond = function(ctx)
      local p = ctx:player()
      return ctx:life(p) <= 3 and ctx:life(p) + ctx:last_life_lost(p) >= 4
    end } },

  { set = "yurina", form = "O", num = 4, name = "天音摇波的潜力", kind = "special", type = "attack",
    cost = 5, full_power = true,
    playable = function(ctx) return ctx:desperation(ctx:player()) end,
    attack = { range = {1, 4}, damage = { aura = 5, life = 5 } } },

  ---------------------------------------------------------------------------
  -- 变格 A1 古刀  (num 必须与 O 同编号对应，用于替代)
  ---------------------------------------------------------------------------
  { set = "yurina.A1", form = "A1", num = 1, name = "乱打", kind = "normal", type = "attack",
    attack = function(ctx)
      if ctx:desperation(ctx:player()) then
        return { range = {2, 2}, damage = { aura = 2, life = 3 }, keywords = { "unrespondable" } }
      end
      return { range = {2, 2}, damage = { aura = 2, life = 1 } }
    end },

  { set = "yurina.A1", form = "A1", num = 6, name = "暴沙雷", kind = "normal", type = "enhance",
    nagi = 1, response = true,
    on_discard = function(ctx)
      ctx:attack { range = {0, 4}, damage = { aura = 1 }, keywords = { "unrespondable" } }
      ctx:cower(ctx:opp())
    end },

  { set = "yurina.A1", form = "A1", num = 2, name = "浦波岚·不完全体", kind = "special",
    type = "attack", cost = 5, response = true,
    attack = { range = {0, 10}, damage = { aura = 3 } },
    on_play = function(ctx)
      local a = ctx:responding_attack()
      if a then a:add { aura = -3 } end
    end },

  ---------------------------------------------------------------------------
  -- 变格 A2 心
  ---------------------------------------------------------------------------
  { set = "yurina.A2", form = "A2", num = 3, name = "问答", kind = "normal", type = "attack",
    attack = { range = { 2, 5 }, damage = { aura = 3, life = 0 } },
    on_attack_after = function(ctx)
      -- 若对手选择由命承受伤害，则盖伏对手牌库顶三张牌。
      if ctx:last_damage_side() == 2 then
        for _ = 1, 3 do ctx:cover_top(ctx:opp()) end
      end
      -- 你执行一次基本动作，对手执行一次相同的基本动作。
      local me = ctx:player()
      local opts = ctx:legal_basics(me)
      if #opts == 0 then return end
      local pick = ctx:choose("问答：执行一次基本动作", opts)
      local name = opts[pick]
      ctx:do_basic(me, name)
      ctx:do_basic(ctx:opp(), name)
    end },

  { set = "yurina.A2", form = "A2", num = 7, name = "终始", kind = "normal", type = "enhance",
    nagi = 3,
    triggers = {
      { event = "aura_changed",
        cond = function(ctx, ev) return ev:subject() ~= ctx:player() end,
        run = function(ctx, ev)
          local c = ctx:choose("终始", { "1虚到自装", "1虚到自气", "进行攻击【3-5 2/1】", "不执行" })
          if c == 4 then return end
          if c == 1 then
            ctx:move("dust", "aura", 1, ctx:player(), ctx:player())
          elseif c == 2 then
            ctx:move("dust", "flare", 1, ctx:player(), ctx:player())
          else
            ctx:attack { range = {3, 5}, damage = { aura = 2, life = 1 } }
          end
        end },
    } },

  { set = "yurina.A2", form = "A2", num = 1, name = "神座渡", kind = "special", type = "attack",
    full_power = true,
    cost = function(ctx)
      local x = ctx:flare(ctx:player())
      ctx:store_int("X", x)  -- X 在付费用前锁定
      return x
    end,
    attack = function(ctx)
      local x = ctx:load_int("X", 0)
      return { range = {0, 5}, damage = { aura = x, life = 2 } }
    end,
    on_attack_after = function(ctx)
      local x = ctx:load_int("X", 0)
      ctx:free_basics_of(ctx:player(), x, { "aura", "flare" })
      -- 选择 X 张使用过的切牌（神座渡除外）设为未使用
      local pool = {}
      for _, inst in ipairs(ctx:used_specials(ctx:player())) do
        if inst ~= ctx:source_inst() then pool[#pool + 1] = inst end
      end
      local want = math.min(x, #pool)
      local sel = ctx:choose_cards("神座渡：重置切牌", pool, want, want)
      for _, inst in ipairs(sel) do ctx:reset_special(inst) end
      ctx:add_hand_limit(ctx:player(), x)
    end },

}
