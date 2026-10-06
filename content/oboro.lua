-- 05-Oboro 胧
-- 机制：设置 / 组装零件 / 电子设置。
-- 本格 O(忍) / 变格 A1(战略) / 变格 A2(电子)。
-- 零件 MP1-3(核心) / CP1-4(附加) 不属于 7+3 构筑，而是固定的公开零件池。

return {

  ---------------------------------------------------------------------------
  -- 本格 O 忍
  ---------------------------------------------------------------------------
  { set = "oboro", form = "O", num = 1, name = "钢丝", kind = "normal", type = "attack",
    setup = true,
    attack = { range = {3, 4}, damage = { aura = 2, life = 2 } } },

  { set = "oboro", form = "O", num = 2, name = "影菱", kind = "normal", type = "attack",
    setup = true,
    attack = { range = {2, 2}, damage = { aura = 2, life = 1 }, keywords = { "unrespondable" } },
    on_attack_after = function(ctx)
      if not ctx:from_cover() then return end
      local opp = ctx:opp()
      local hand = ctx:hand(opp)
      if #hand == 0 then return end
      local sel = ctx:choose_cards_for(ctx:player(), "检视对手手牌并盖伏一张", hand, 1, 1)
      for _, inst in ipairs(sel) do ctx:cover_card(inst) end
    end },

  { set = "oboro", form = "O", num = 3, name = "斩击乱舞", kind = "normal", type = "attack",
    full_power = true,
    attack = function(ctx)
      if ctx:aura_damaged_this_turn(ctx:opp()) then
        return { range = {2, 4}, damage = { aura = 4, life = 3 } }
      end
      return { range = {2, 4}, damage = { aura = 3, life = 2 } }
    end },

  { set = "oboro", form = "O", num = 4, name = "忍步", kind = "normal", type = "action",
    setup = true,
    on_play = function(ctx) ctx:move("dust", "distance", 1) end },

  { set = "oboro", form = "O", num = 5, name = "诱导", kind = "normal", type = "action",
    setup = true, response = true,
    on_play = function(ctx)
      if ctx:choose("诱导", { "1 距 -> 敌装", "1 敌装 -> 敌气" }) == 1 then
        ctx:move("distance", "aura", 1, ctx:player(), ctx:opp())
      else
        ctx:move("aura", "flare", 1, ctx:opp(), ctx:opp())
      end
    end },

  { set = "oboro", form = "O", num = 6, name = "分身", kind = "normal", type = "action",
    full_power = true,
    on_play = function(ctx)
      local pool = {}
      for _, inst in ipairs(ctx:cover_cards(ctx:player())) do
        if not ctx:is_full_power(inst) then pool[#pool + 1] = inst end
      end
      if #pool == 0 then return end
      local sel = ctx:choose_cards("分身：从盖牌堆选一张非全力牌使用", pool, 1, 1)
      if #sel == 0 then return end
      local inst = sel[1]
      local atk = ctx:is_attack(inst)
      if atk then ctx:force_unrespondable() end
      ctx:use_from_cover(inst, false)
      if ctx:card_zone(inst) == "discard" then
        if atk then ctx:force_unrespondable() end
        ctx:use_from_cover(inst, false)
      end
    end },

  { set = "oboro", form = "O", num = 7, name = "生物活性", kind = "normal", type = "enhance",
    nagi = 4, breakable = true, setup = true,
    on_discard = function(ctx)
      local pool = {}
      for _, inst in ipairs(ctx:used_specials(ctx:player())) do pool[#pool + 1] = inst end
      if #pool == 0 then return end
      local sel = ctx:choose_cards("生物活性：将一张已使用切牌改未使用", pool, 1, 1)
      for _, inst in ipairs(sel) do ctx:reset_special(inst) end
    end },

  { set = "oboro", form = "O", num = 1, name = "熊介", kind = "special", type = "attack",
    cost = 4, full_power = true,
    attack = { range = {4, 4}, damage = { aura = 2, life = 2 } },
    on_attack_after = function(ctx)
      local x = ctx:cover_count(ctx:player())
      for _ = 1, x do
        ctx:attack { range = {4, 4}, damage = { aura = 2, life = 2 } }
      end
    end },

  { set = "oboro", form = "O", num = 2, name = "鸢影", kind = "special", type = "action",
    cost = 4, response = true,
    on_play = function(ctx)
      local pool = {}
      for _, inst in ipairs(ctx:cover_cards(ctx:player())) do
        if not ctx:is_full_power(inst) then pool[#pool + 1] = inst end
      end
      if #pool == 0 then return end
      local sel = ctx:choose_cards("鸢影：选一张非全力牌使用", pool, 1, 1)
      if #sel == 0 then return end
      local inst = sel[1]
      local asResp = ctx:responding_attack() ~= nil
      ctx:use_from_cover(inst, asResp)
    end },

  { set = "oboro", form = "O", num = 3, name = "虚鱼", kind = "special", type = "enhance",
    nagi = 3,
    on_enter = function(ctx)
      local pile = ctx:discard_pile(ctx:player())
      if #pile == 0 then return end
      local sel = ctx:choose_cards("虚鱼：从弃牌堆选任意数量盖伏", pile, 0, #pile)
      for _, inst in ipairs(sel) do ctx:cover_card(inst) end
    end },

  { set = "oboro", form = "O", num = 4, name = "壬蔓", kind = "special", type = "attack",
    cost = 0,
    attack = { range = {3, 7}, damage = { aura = 1, life = 1 } },
    on_attack_after = function(ctx) ctx:move("dust", "flare", 1, ctx:player(), ctx:player()) end,
    reset = { kind = "end_turn", cond = function(ctx) return ctx:flare(ctx:player()) == 0 end } },

  ---------------------------------------------------------------------------
  -- 变格 A1 战略
  ---------------------------------------------------------------------------
  { set = "oboro.A1", form = "A1", num = 2, name = "手里剑", kind = "normal", type = "attack",
    attack = { range = {3, 5}, damage = { aura = 2, life = 1 } },
    triggers = {
      { event = "turn_end", zone = "discard",
        cond = function(ctx, ev)
          return ev:subject() == ctx:player() and (ctx:cover_count(0) + ctx:cover_count(1)) >= 5
        end,
        run = function(ctx, ev) ctx:to_hand(ctx:source_inst()) end },
    } },

  { set = "oboro.A1", form = "A1", num = 3, name = "突袭", kind = "normal", type = "attack",
    full_power = true,
    attack = function(ctx)
      local x = ctx:cover_count(ctx:opp())
      local a, l = 4 - x, 3 - x
      if a < 0 then a = 0 end
      if l < 0 then l = 0 end
      return { range = {1, 3}, damage = { aura = a, life = l }, keywords = { "no_normal_response" } }
    end },

  { set = "oboro.A1", form = "A1", num = 4, name = "神代枝", kind = "special", type = "action",
    cost = 0, full_power = true,
    on_play = function(ctx)
      ctx:gain_external("aura", 1, ctx:player())   -- 1 游戏外 -> 自装
      ctx:gain_external("flare", 1, ctx:player())  -- 1 游戏外 -> 自气
      ctx:gain_extra("最后的结晶")
      ctx:remove_card(ctx:source_inst())
    end },

  { set = "oboro.A1", form = "A1", num = 999, name = "最后的结晶", kind = "special",
    type = "action", cost = 3, extra = true,
    -- 仅能在死亡窗口使用；主阶段/对应窗口不提供。
    playable = function(ctx) return false end },

  ---------------------------------------------------------------------------
  -- 变格 A2 电子
  ---------------------------------------------------------------------------
  { set = "oboro.A2", form = "A2", num = 1, name = "全息苦无", kind = "normal", type = "attack",
    setup = true,
    attack = { range = { {1, 1}, {3, 3}, {5, 5} }, damage = { aura = 1, life = 1 } },
    on_attack_after = function(ctx)
      ctx:assemble_one(ctx:player())
      if ctx:choose("全息苦无：盖伏此牌（而非弃置）？", { "盖伏", "弃置" }) == 1 then
        ctx:cover_card(ctx:source_inst())
      end
    end },

  { set = "oboro.A2", form = "A2", num = 1, name = "千兆介", kind = "special", type = "attack",
    cost = function(ctx)
      local x = ctx:cover_count(ctx:player()) + ctx:assembled_count(ctx:player())
      return math.max(0, 16 - x)
    end,
    attack = { range = {3, 4}, damage = { aura = 2, life = 1 } },
    on_attack_after = function(ctx)
      for _ = 1, 3 do ctx:attack { range = {3, 4}, damage = { aura = 2, life = 1 } } end
    end },

  { set = "oboro.A2", form = "A2", num = 3, name = "胧文书·电子神涉", kind = "special",
    type = "action", cost = 0, full_power = true,
    on_play = function(ctx)
      ctx:assemble_one(ctx:player())
      ctx:free_basics(ctx:player(), 1)
    end,
    triggers = {
      { event = "before_rebuild",
        cond = function(ctx, ev) return ev:subject() == ctx:player() end,
        run = function(ctx, ev)
          if ctx:choose("胧文书：拆除已组装零件到至多 1 个？", { "拆除", "不拆除" }) == 1 then
            ctx:disassemble_to(ctx:player(), 1)
          end
          local x = math.ceil(ctx:cover_count(ctx:player()) / 2)
          ctx:assemble_many(ctx:player(), x)
        end },
    } },

  -- 核心零件 (电子设置)
  { set = "oboro.A2", form = "A2", num = 201, name = "核心零件X", kind = "special",
    type = "attack", part = true, core_part = true, electronic = true,
    attack = { range = {4, 5}, damage = { aura = 2, life = 2 } } },

  { set = "oboro.A2", form = "A2", num = 202, name = "核心零件Y", kind = "special",
    type = "attack", part = true, core_part = true, electronic = true,
    attack = { range = { {3, 3}, {6, 6} }, damage = { aura = 2, life = 1 } } },

  { set = "oboro.A2", form = "A2", num = 203, name = "核心零件Z", kind = "special",
    type = "attack", part = true, core_part = true, electronic = true,
    attack = { range = {0, 2}, damage = { aura = 1, life = 0 } },
    on_attack_after = function(ctx)
      if ctx:last_damage_side() == 2 then ctx:cower(ctx:opp()) end
    end },

  -- 附加零件 (按附加数量 n 取 1/2/3/4 档)
  { set = "oboro.A2", form = "A2", num = 211, name = "附加零件A", kind = "special",
    type = "action", part = true,
    apply = function(ctx, atk, n)
      if n <= 1 then atk:keyword("no_enhance_response")
      elseif n == 2 then atk:keyword("no_attack_response")
      elseif n == 3 then atk:keyword("no_action_response")
      else atk:keyword("no_normal_response") end
    end },

  { set = "oboro.A2", form = "A2", num = 212, name = "附加零件B", kind = "special",
    type = "action", part = true,
    apply = function(ctx, atk, n)
      if n == 2 then atk:add { life = 1 }
      elseif n == 3 then atk:add { aura = 1 }
      elseif n >= 4 then atk:add { aura = 1, life = 1 } end
    end },

  { set = "oboro.A2", form = "A2", num = 213, name = "附加零件C", kind = "special",
    type = "action", part = true,
    after = function(ctx, atk, n)
      ctx:gain_vigor(ctx:player(), 1)
      if n <= 1 then
        ctx:move("dust", "distance", 1)
      elseif ctx:choose("附加零件C", { "1 虚到距", "1 距到虚" }) == 1 then
        ctx:move("dust", "distance", 1)
      else
        ctx:move("distance", "dust", 1)
      end
    end },

  { set = "oboro.A2", form = "A2", num = 214, name = "附加零件D", kind = "special",
    type = "action", part = true,
    after = function(ctx, atk, n)
      if n <= 1 then
        local pile = ctx:discard_pile(ctx:player())
        local sel = ctx:choose_cards("D：从弃牌选至多1张盖伏", pile, 0, 1)
        for _, i in ipairs(sel) do ctx:cover_card(i) end
      elseif n == 2 then
        local cv = ctx:cover_cards(ctx:player())
        local sel = ctx:choose_cards("D：从盖牌选至多1张放牌库底", cv, 0, 1)
        for _, i in ipairs(sel) do ctx:to_deck_bottom(i) end
      elseif n == 3 then
        local cv = ctx:cover_cards(ctx:player())
        local sel = ctx:choose_cards("D：从盖牌选至多2张放牌库底", cv, 0, 2)
        for _, i in ipairs(sel) do ctx:to_deck_bottom(i) end
      else
        local pool = {}
        for _, i in ipairs(ctx:discard_pile(ctx:player())) do pool[#pool + 1] = i end
        for _, i in ipairs(ctx:cover_cards(ctx:player())) do pool[#pool + 1] = i end
        local sel = ctx:choose_cards("D：从弃牌/盖牌选至多2张放牌库底", pool, 0, 2)
        for _, i in ipairs(sel) do ctx:to_deck_bottom(i) end
      end
    end },

}
