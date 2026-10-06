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

  -- 达人距离 +1（影响双方的前进/离脱阈值）；其结晶按正常规则移除时进入距。
  { set = "saine", form = "O", num = 5, name = "圈域", kind = "normal", type = "enhance",
    nagi = 2, decay_to = "distance", near_distance_mod = 1 },

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

  ---------------------------------------------------------------------------
  -- 变格 A1 琵琶
  ---------------------------------------------------------------------------
  { set = "saine.A1", form = "A1", num = 1, name = "合奏", kind = "normal", type = "attack",
    response = true, goddess2 = "tokoyo",
    attack = { range = {2, 5}, damage = { aura = 3, life = 0 } },
    on_play = function(ctx)
      local me = ctx:player()
      if ctx:used_special_count(me, "saine") > 0 then
        ctx:move("aura", "dust", 1, ctx:opp(), ctx:opp())
      end
      if ctx:hasso(me) or ctx:shinkyou(me) then ctx:move("dust", "distance", 1) end
      if ctx:used_special_count(me, "tokoyo") > 0 then
        ctx:move("dust", "aura", 1, me, me)
      end
    end },

  { set = "saine.A1", form = "A1", num = 6, name = "伴奏", kind = "normal", type = "enhance",
    nagi = 3,
    triggers = {
      { event = "attack_declared",
        cond = function(ctx, ev)
          -- 对手"其回合内"的第一次攻击：对手在我方回合作为对应打出的攻击不算。
          if ev:subject() == ctx:player() or not ev:first() or ctx:is_my_turn() then return false end
          if ctx:hasso(ctx:player()) then return true end
          for _, inst in ipairs(ctx:used_specials(ctx:player())) do
            if not ctx:card_is_goddess(inst, "saine") then return true end
          end
          return false
        end,
        run = function(ctx, ev)
          local a = ev:attack()
          if a then
            a:remove_unrespondable()
            a:add { aura = -1 }
          end
        end },
    },
    on_enter = function(ctx)
      if ctx:used_special_count(ctx:player(), "saine") > 0 then
        ctx:add_cut_cost_delta(ctx:player(), -1)
      end
    end,
    on_discard = function(ctx)
      if ctx:used_special_count(ctx:player(), "saine") > 0 then
        ctx:add_cut_cost_delta(ctx:player(), -1)
      end
    end },

  { set = "saine.A1", form = "A1", num = 2, name = "二重奏·弹奏冰瞑", kind = "special",
    type = "action", cost = function(ctx) return ctx:hasso(ctx:player()) and 1 or 2 end,
    on_play = function(ctx) ctx:set_cannot_attack(ctx:player()) end,
    continuous = {
      { when = "used", query = "attack",
        apply = function(ctx, atk)
          if atk:attacker() == ctx:player() and not atk:source_is_goddess("saine") then
            atk:add { life = 1 }
          end
        end },
    },
    reset = { kind = "immediate", cond = function(ctx)
      return ctx:last_damage_from_attack() and ctx:last_damage_side() == 2
    end } },

  ---------------------------------------------------------------------------
  -- 变格 A2 拒绝
  ---------------------------------------------------------------------------
  { set = "saine.A2", form = "A2", num = 2, name = "里斩", kind = "normal", type = "attack",
    attack = function(ctx)
      local a, l = 3, 1
      if ctx:aura(ctx:opp()) <= 1 then a, l = 1, 3 end
      return { range = {4, 5}, damage = { aura = a, life = l }, keywords = { "no_special_response" } }
    end },

  { set = "saine.A2", form = "A2", num = 7, name = "遗响壁", kind = "normal", type = "enhance",
    nagi = 2, armor_as_crystals = true,
    on_discard = function(ctx)
      if not ctx:hasso(ctx:player()) then return end
      ctx:attack { range = {0, 5}, damage = { aura = 2 }, after = function(ctx2, atk)
        ctx2:next_attack_mod { apply = function(ctx3, a) a:add { life = 1 } end }
      end }
    end },

  { set = "saine.A2", form = "A2", num = 3, name = "绝唱绝华", kind = "special", type = "attack",
    cost = 1, response = true, terminal = true,
    attack = { range = {0, 10}, damage = { aura = 2 } },
    on_attack_after = function(ctx)
      local me = ctx:player()
      ctx:on_resolve(function(ctx2)
        if ctx2:last_damage_side() == 1 and ctx2:aura(me) == 0 then
          ctx2:end_current_main()
        end
      end)
    end },

}
