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
    on_expand = function(ctx) ctx:move("distance", "aura", 2, ctx:player(), ctx:player()) end,
    on_discard = function(ctx) ctx:move("aura", "distance", 2, ctx:player(), ctx:player()) end },

  -- 终端（结束主要阶段）；展开时集中力变 2；弃置时生成攻击。
  { set = "tokoyo", form = "O", num = 7, name = "晴舞台", kind = "normal", type = "enhance",
    nagi = 2, terminal = true,
    on_expand = function(ctx) ctx:set_vigor(ctx:player(), 2) end,
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

  ---------------------------------------------------------------------------
  -- 变格 A1 笛
  ---------------------------------------------------------------------------
  { set = "tokoyo.A1", form = "A1", num = 1, name = "奏流", kind = "normal", type = "attack",
    attack = function(ctx)
      local kw = {}
      if ctx:used_special_count(ctx:player(), "tokoyo") > 0 then kw[#kw + 1] = "unrespondable" end
      return { range = {5, 5}, damage = { life = 1 }, keywords = kw }
    end,
    on_attack_after = function(ctx)
      local me = ctx:player()
      local other = false
      for _, inst in ipairs(ctx:used_specials(me)) do
        if not ctx:card_is_goddess(inst, "tokoyo") then other = true end
      end
      if ctx:shinkyou(me) or other then
        local c = ctx:choose("奏流：置于牌库顶或牌库底", { "牌库顶", "牌库底" })
        if c == 1 then ctx:to_deck_top(ctx:source_inst()) else ctx:to_deck_bottom(ctx:source_inst()) end
      end
    end },

  { set = "tokoyo.A1", form = "A1", num = 4, name = "合奏", kind = "normal", type = "attack",
    response = true, goddess2 = "saine",
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

  { set = "tokoyo.A1", form = "A1", num = 3, name = "二重奏·吹弹阳明", kind = "special",
    type = "action", cost = function(ctx) return ctx:shinkyou(ctx:player()) and 0 or 1 end,
    on_play = function(ctx) ctx:set_cannot_basic(ctx:player()) end,
    triggers = {
      { event = "responded_with",
        cond = function(ctx, ev)
          return ev:subject() == ctx:player() and not ctx:card_is_goddess(ev:card(), "tokoyo")
        end,
        run = function(ctx, ev)
          local pile = ctx:discard_pile(ctx:player())
          if #pile == 0 then return end
          local sel = ctx:choose_cards("放回牌库底", pile, 0, 1)
          for _, inst in ipairs(sel) do ctx:to_deck_bottom(inst) end
        end },
    },
    reset = { kind = "immediate", cond = function(ctx)
      return ctx:last_damage_from_attack() and ctx:last_damage_side() == 2
    end } },

  ---------------------------------------------------------------------------
  -- 变格 A2 恐怖
  ---------------------------------------------------------------------------
  { set = "tokoyo.A2", form = "A2", num = 2, name = "畏掠", kind = "normal", type = "attack",
    response = true,
    attack = function(ctx)
      local spec = { range = {2, 3}, damage = { aura = 2, life = 1 } }
      if ctx:vigor(ctx:opp()) == 0 then spec.attacker_chooses_damage = true end
      return spec
    end,
    on_attack_after = function(ctx)
      local me = ctx:player()
      if not ctx:shinkyou(me) then return end
      local a = ctx:responding_attack()
      if not a then return end
      local x, y = 0, 0
      if ctx:last_damage_side() == 1 then
        x = ctx:last_damage_amount()
      elseif ctx:last_damage_side() == 2 then
        y = ctx:last_damage_amount()
      end
      a:add { aura = -x, life = -y }
    end },

  { set = "tokoyo.A2", form = "A2", num = 2, name = "悠久之雪", kind = "special", type = "attack",
    cost = 1,
    attack = { range = {3, 5}, damage = { aura = 1, life = 1 } },
    on_attack_after = function(ctx)
      if ctx:last_damage_side() == 1 then
        ctx:move("flare", "aura", 1, ctx:opp(), ctx:player())
      end
    end,
    reset = { kind = "end_turn", cond = function(ctx) return ctx:vigor(ctx:opp()) == 1 end } },

  { set = "tokoyo.A2", form = "A2", num = 3, name = "徒寄之八重樱", kind = "special",
    type = "action", cost = 4, aura_max = 8,
    on_play = function(ctx) ctx:move("dust", "aura", 5, ctx:player(), ctx:player()) end,
    triggers = {
      { event = "turn_start",
        cond = function(ctx, ev)
          return ev:subject() == ctx:player() and ctx:aura(ctx:player()) >= 6
        end,
        run = function(ctx, ev) ctx:attack { range = {0, 8}, damage = { life = 1 } } end },
    } },

}
