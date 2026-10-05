-- 02-Saine 冰雨细音  【机制：八相 ~ 自装<=1 时强化】
-- Phase 2 只实现本格 O；变格 A1(琵琶)/A2(拒绝) 留待 Phase 3。

return {

  -- 常
  { set = "saine", form = "O", num = 1, name = "八面斩", kind = "normal", type = "attack",
    attack = { range = {4, 5}, damage = { aura = 2, life = 1 } },
    on_attack_after = function(ctx)
      if ctx:hasso(ctx:player()) then
        ctx:attack { range = {4, 5}, damage = { aura = 2, life = 1 } }
      end
    end },

  { set = "saine", form = "O", num = 2, name = "薙刀斩", kind = "normal", type = "attack",
    response = true,
    attack = { range = {4, 5}, damage = { aura = 3, life = 1 } } },

  { set = "saine", form = "O", num = 3, name = "墩击", kind = "normal", type = "attack",
    response = true,
    attack = { range = {2, 3}, damage = { aura = 2, life = 1 } },
    on_attack_after = function(ctx)
      if ctx:hasso(ctx:player()) then ctx:move("dust", "distance", 1) end
    end },

  -- 非对应牌，但八相时可当对应打出。
  { set = "saine", form = "O", num = 4, name = "识破", kind = "normal", type = "action",
    respond = function(ctx) return ctx:hasso(ctx:player()) end,
    on_play = function(ctx) ctx:move("distance", "dust", 1) end },

  -- 达人距离（我方攻击距离远+1）；其结晶按正常规则移除时进入距。
  { set = "saine", form = "O", num = 5, name = "圈域", kind = "normal", type = "enhance",
    nagi = 2, decay_to = "distance",
    continuous = {
      { when = "expanded", query = "attack",
        apply = function(ctx, atk)
          if atk:attacker() == ctx:player() then atk:extend_far(1) end
        end },
    } },

  { set = "saine", form = "O", num = 6, name = "冲音晶", kind = "normal", type = "enhance",
    nagi = 1, response = true,
    on_enter = function(ctx)
      local a = ctx:responding_attack()
      if a then a:add { aura = -1 } end
    end,
    on_discard = function(ctx)
      ctx:attack { range = {0, 10}, damage = { aura = 1 }, keywords = { "unrespondable" } }
      ctx:move("dust", "distance", 1)
    end },

  -- 结算伤害时卡上结晶视作装（会优先被消耗）。
  { set = "saine", form = "O", num = 7, name = "无音壁", kind = "normal", type = "enhance",
    nagi = 5, full_power = true, armor_as_crystals = true },

  -- 切
  { set = "saine", form = "O", num = 1, name = "律动弧戟", kind = "special", type = "action",
    cost = 6,
    on_play = function(ctx)
      ctx:attack { range = {3, 4}, damage = { aura = 1, life = 1 } }
      ctx:attack { range = {4, 5}, damage = { aura = 1, life = 1 } }
      ctx:attack { range = {3, 5}, damage = { aura = 2, life = 2 } }
    end },

  { set = "saine", form = "O", num = 2, name = "响鸣共振", kind = "special", type = "action",
    cost = function(ctx) return math.max(0, 8 - ctx:aura(ctx:opp())) end,
    on_play = function(ctx) ctx:move("aura", "distance", 2, ctx:opp(), ctx:opp()) end },

  { set = "saine", form = "O", num = 3, name = "音无碎冰", kind = "special", type = "attack",
    cost = 2, response = true,
    attack = { range = {0, 10}, damage = { aura = 1, life = 1 } },
    on_attack_after = function(ctx)
      local a = ctx:responding_attack()
      if a then a:add { aura = -1, life = -1 } end
    end,
    reset = { kind = "end_turn", cond = function(ctx) return ctx:hasso(ctx:player()) end } },

  -- 只能对应切牌打出。
  { set = "saine", form = "O", num = 4, name = "冰雨细音的终焉", kind = "special", type = "attack",
    cost = 5, response = true,
    respond = function(ctx)
      local a = ctx:responding_attack()
      return a ~= nil and a:from_special()
    end,
    attack = { range = {1, 5}, damage = { aura = 5, life = 5 } } },

}
