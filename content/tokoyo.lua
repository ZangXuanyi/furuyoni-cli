-- 04-Tokoyo 常世  【机制：心境 ~ 集中力等于 2 时强化】
-- Phase 2 只实现本格 O；变格 A1(笛)/A2(恐怖) 留待 Phase 3。

return {

  -- 常
  { set = "tokoyo", form = "O", num = 1, name = "梳洗", kind = "normal", type = "attack",
    attack = { range = {4}, damage = { life = 1 } },
    on_attack_after = function(ctx)
      -- 心境时回库顶（而非弃牌堆）。
      if ctx:shinkyou(ctx:player()) then ctx:to_deck_top(ctx:source_inst()) end
    end },

  { set = "tokoyo", form = "O", num = 2, name = "雅击", kind = "normal", type = "attack",
    response = true,
    attack = { range = {2, 4}, damage = { aura = 2, life = 1 } },
    on_attack_after = function(ctx)
      if ctx:shinkyou(ctx:player()) then
        local a = ctx:responding_attack()
        if a and not a:from_special() then a:negate() end
      end
    end },

  { set = "tokoyo", form = "O", num = 3, name = "脱兔", kind = "normal", type = "action",
    on_play = function(ctx)
      if ctx:distance() <= 3 then ctx:move("dust", "distance", 2) end
    end },

  { set = "tokoyo", form = "O", num = 4, name = "诗舞", kind = "normal", type = "action",
    response = true,
    on_play = function(ctx)
      ctx:gain_vigor(ctx:player(), 1)
      if ctx:choose("诗舞", { "1 气 -> 装", "1 装 -> 距" }) == 1 then
        ctx:move("flare", "aura", 1, ctx:player(), ctx:player())
      else
        ctx:move("aura", "distance", 1, ctx:player(), ctx:player())
      end
    end },

  { set = "tokoyo", form = "O", num = 5, name = "扇回旋", kind = "normal", type = "action",
    full_power = true,
    on_play = function(ctx)
      local pool = {}
      for _, inst in ipairs(ctx:discard_pile(ctx:player())) do
        if inst ~= ctx:source_inst() then pool[#pool + 1] = inst end
      end
      local sel = ctx:choose_cards("选至多 2 张放回牌库底", pool, 0, 2)
      for _, inst in ipairs(sel) do ctx:to_deck_bottom(inst) end
      ctx:move("dust", "aura", 2, ctx:player(), ctx:player())
    end },

  { set = "tokoyo", form = "O", num = 6, name = "风舞台", kind = "normal", type = "enhance",
    nagi = 2,
    on_enter = function(ctx) ctx:move("distance", "aura", 2, ctx:player(), ctx:player()) end,
    on_discard = function(ctx) ctx:move("aura", "distance", 2, ctx:player(), ctx:player()) end },

  -- 终端（结束主要阶段）；展开时集中力变 2；弃置时生成攻击。
  { set = "tokoyo", form = "O", num = 7, name = "晴舞台", kind = "normal", type = "enhance",
    nagi = 2, terminal = true,
    on_enter = function(ctx) ctx:set_vigor(ctx:player(), 2) end,
    on_discard = function(ctx) ctx:attack { range = {3, 6}, damage = { life = 1 } } end },

  -- 切
  { set = "tokoyo", form = "O", num = 1, name = "久远之花", kind = "special", type = "attack",
    cost = 5, response = true,
    attack = { range = {0, 10}, damage = { life = 1 } },
    on_attack_after = function(ctx)
      local a = ctx:responding_attack()
      if a then a:negate() end
    end },

  { set = "tokoyo", form = "O", num = 2, name = "千岁之鸟", kind = "special", type = "attack",
    cost = 2,
    attack = { range = {3, 4}, damage = { aura = 2, life = 2 } },
    on_attack_after = function(ctx) ctx:rebuild(ctx:player(), false) end },

  { set = "tokoyo", form = "O", num = 3, name = "无穷之风", kind = "special", type = "attack",
    cost = 1,
    attack = { range = {3, 8}, damage = { aura = 1, life = 1 } },
    on_attack_after = function(ctx)
      local opp = ctx:opp()
      local pool = {}
      for _, inst in ipairs(ctx:hand(opp)) do
        if not ctx:is_attack(inst) then pool[#pool + 1] = inst end
      end
      if #pool == 0 then
        ctx:reveal_hand(opp)
      else
        local sel = ctx:choose_cards_for(opp, "弃 1 张非攻击牌", pool, 1, 1)
        for _, inst in ipairs(sel) do ctx:discard_card(inst) end
      end
    end,
    reset = { kind = "end_turn", cond = function(ctx) return ctx:shinkyou(ctx:player()) end } },

  { set = "tokoyo", form = "O", num = 4, name = "常世之月", kind = "special", type = "action",
    cost = 2,
    on_play = function(ctx)
      ctx:set_vigor(ctx:player(), 2)
      ctx:set_vigor(ctx:opp(), 0)
      ctx:cower(ctx:opp())
    end },

}
