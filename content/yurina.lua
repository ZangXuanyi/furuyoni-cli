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
          if atk:attacker() == ctx:player() and g ~= "" and g ~= "yurina" then
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

}
