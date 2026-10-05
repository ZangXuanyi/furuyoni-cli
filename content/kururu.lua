-- 10-Kururu 枢
-- 机制 机巧：我方正面向上的牌（弃牌堆+已使用切牌+生效中付与）中若存在某颜色搭配，牌获得特殊效果。
-- 颜色：攻击=R 行动=B 付与=G 对应=P 全力=Y。机巧串如 "BBBPP" = 至少3蓝且至少2紫。
-- O=络缲；A1=机器；A2=友谊。

return {

  ---------------------------------------------------------------------------
  -- 本格 O 络缲
  ---------------------------------------------------------------------------
  { set = "kururu", form = "O", num = 1, name = "电气疗法", kind = "normal", type = "action",
    on_play = function(ctx)
      if ctx:keisou("BBBPP") then ctx:lose_life(ctx:opp(), ctx:keisou_amount(1)) end
    end },

  { set = "kururu", form = "O", num = 2, name = "加速效应", kind = "normal", type = "action",
    on_play = function(ctx)
      if not ctx:keisou("BBG") then return end
      local pool = {}
      for _, i in ipairs(ctx:hand(ctx:player())) do
        if ctx:is_full_power(i) and not ctx:is_zenkai(i) then pool[#pool + 1] = i end
      end
      if #pool == 0 then return end
      local sel = ctx:choose_cards("加速效应：选择一张全力牌打出（不结束回合）", pool, 1, 1)
      for _, i in ipairs(sel) do ctx:play_hand_card(i) end
    end },

  { set = "kururu", form = "O", num = 3, name = "枢噜噜～", kind = "normal", type = "action",
    response = true,
    on_play = function(ctx)
      if ctx:responding_attack() == nil then return end
      local sel = ctx:choose_options("枢噜噜～：三选二",
        { "抽一张牌", "将一张弃牌放进抽牌堆底", "对手弃一张牌" }, 2, 2)
      for _, idx in ipairs(sel) do
        if idx == 0 then
          ctx:draw(ctx:player(), 1)
        elseif idx == 1 then
          local pile = ctx:discard_pile(ctx:player())
          if #pile > 0 then
            local s2 = ctx:choose_cards("放回牌库底", pile, 0, 1)
            for _, i in ipairs(s2) do ctx:to_deck_bottom(i) end
          end
        else
          local hand = ctx:hand(ctx:opp())
          if #hand > 0 then
            local s2 = ctx:choose_cards_for(ctx:opp(), "对手弃一张牌", hand, 1, 1)
            for _, i in ipairs(s2) do ctx:discard_card(i) end
          end
        end
      end
    end },

  { set = "kururu", form = "O", num = 4, name = "大龙卷风", kind = "normal", type = "action",
    full_power = true,
    on_play = function(ctx)
      if ctx:keisou("RR") then ctx:deal_damage(ctx:opp(), ctx:keisou_amount(5), nil) end
      if ctx:keisou("GG") then ctx:lose_life(ctx:opp(), ctx:keisou_amount(1)) end
    end },

  { set = "kururu", form = "O", num = 5, name = "回收利用", kind = "normal", type = "action",
    full_power = true, from_cover = true,
    on_play = function(ctx)
      if not ctx:keisou("GP") then return end
      local pool = {}
      for _, i in ipairs(ctx:used_specials(ctx:player())) do
        if not ctx:is_full_power(i) and not ctx:card_is_goddess(i, "kururu") then pool[#pool + 1] = i end
      end
      if #pool == 0 then return end
      local sel = ctx:choose_cards("回收利用：选择另一柱女神的已使用非全力切牌", pool, 1, 1)
      if #sel == 0 then return end
      local c = sel[1]
      local stat = ctx:choose("回收利用：调整一项 ±1",
        { "距离+1", "距离-1", "装伤+1", "装伤-1", "命伤+1", "命伤-1", "纳+1", "纳-1" })
      if stat == 1 then ctx:next_attack_mod { apply = function(c2, a) a:extend_far(1) end }
      elseif stat == 2 then ctx:next_attack_mod { apply = function(c2, a) a:shrink_far(1) end }
      elseif stat == 3 then ctx:next_attack_mod { apply = function(c2, a) a:add { aura = 1 } end }
      elseif stat == 4 then ctx:next_attack_mod { apply = function(c2, a) a:add { aura = -1 } end }
      elseif stat == 5 then ctx:next_attack_mod { apply = function(c2, a) a:add { life = 1 } end }
      elseif stat == 6 then ctx:next_attack_mod { apply = function(c2, a) a:add { life = -1 } end }
      elseif stat == 7 then ctx:set_nagi_adjust(1)
      elseif stat == 8 then ctx:set_nagi_adjust(-1) end
      ctx:reuse_special(c)
    end },

  { set = "kururu", form = "O", num = 6, name = "模块化", kind = "normal", type = "enhance",
    nagi = 3,
    triggers = {
      { event = "action_resolved",
        cond = function(ctx, ev) return ev:card() ~= ctx:source_inst() end,
        run = function(ctx, ev) ctx:free_basics(ctx:player(), 1) end },
    } },

  { set = "kururu", form = "O", num = 7, name = "反射装置", kind = "normal", type = "enhance",
    nagi = 0,
    on_enter = function(ctx)
      if ctx:keisou("RP") then ctx:dust_to_card(ctx:source_inst(), 4) end
    end,
    triggers = {
      { event = "attack_counted",
        cond = function(ctx, ev) return ev:first() and ev:subject() == ctx:opp() end,
        run = function(ctx, ev)
          local a = ev:attack()
          if a then a:negate() end
        end },
    } },

  { set = "kururu", form = "O", num = 1, name = "魔能吸收", kind = "special", type = "action",
    cost = 2, response = true,
    on_play = function(ctx) ctx:move("aura", "aura", 1, ctx:opp(), ctx:player()) end,
    triggers = {
      { event = "special_reset",
        cond = function(ctx, ev) return ev:card() ~= ctx:source_inst() end,
        run = function(ctx, ev) ctx:move("aura", "aura", 1, ctx:opp(), ctx:player()) end },
    } },

  { set = "kururu", form = "O", num = 2, name = "大～魔像", kind = "special", type = "action",
    cost = 4,
    triggers = {
      { event = "fullpower_used",
        cond = function(ctx, ev) return ev:subject() == ctx:player() end,
        run = function(ctx, ev) ctx:free_basics(ctx:player(), 1) end },
      { event = "turn_end",
        cond = function(ctx, ev) return ev:subject() == ctx:player() and ctx:keisou("YYP") end,
        run = function(ctx, ev)
          ctx:lose_life(ctx:opp(), 1)
          ctx:rebuild(ctx:player(), false)
        end },
    } },

  { set = "kururu", form = "O", num = 3, name = "复制粘贴", kind = "special", type = "action",
    cost = 1,
    on_play = function(ctx)
      local pool = {}
      for _, i in ipairs(ctx:hand(ctx:player())) do
        if ctx:is_normal_card(i) and ctx:card_name(i) ~= "复制品" then pool[#pool + 1] = i end
      end
      if #pool > 0 then
        local sel = ctx:choose_cards("复制粘贴：选择自己一张常规牌封印复制", pool, 1, 1)
        for _, i in ipairs(sel) do ctx:seal_card(ctx:source_inst(), i) end
      end
      local e = ctx:gain_extra("复制品")
      if e >= 0 then ctx:to_deck_bottom(e) end
    end,
    reset = { kind = "end_turn", cond = function(ctx) return ctx:rebuilt_this_turn(ctx:player()) end } },

  { set = "kururu", form = "O", num = 901, name = "复制品", kind = "normal", type = "attack",
    extra = true,
    attack = function(ctx)
      local host = ctx:find_named(ctx:player(), "复制粘贴")
      local sealed = (host >= 0) and ctx:sealed_card(host) or -1
      if sealed < 0 or ctx:card_name(sealed) == "复制品" then
        return { range = { { 0, 0 } }, damage = {} }
      end
      return ctx:card_attack_spec(sealed)
    end },

  { set = "kururu", form = "O", num = 4, name = "枢的神涉装置", kind = "special", type = "action",
    cost = 3,
    on_play = function(ctx)
      local pool = {}
      for _, i in ipairs(ctx:used_specials(ctx:opp())) do pool[#pool + 1] = i end
      if #pool > 0 then
        local sel = ctx:choose_cards("神涉装置：选择对手一张已使用切牌免费使用", pool, 1, 1)
        for _, i in ipairs(sel) do ctx:use_foreign_card(i) end
      end
      ctx:remove_card(ctx:source_inst())
    end },

  ---------------------------------------------------------------------------
  -- 变格 A1 机器
  ---------------------------------------------------------------------------
  { set = "kururu.A1", form = "A1", num = 1, name = "调查分析", kind = "normal", type = "action",
    on_play = function(ctx)
      if not ctx:keisou("GRP") then return end
      local pick = ctx:choose("调查分析", { "从你的盖牌选1张置弃牌区", "从对手盖牌随机1张置弃牌堆" })
      local chosen = -1
      if pick == 1 then
        local cv = ctx:cover_cards(ctx:player())
        if #cv > 0 then
          local sel = ctx:choose_cards("选一张你的盖牌置弃牌区", cv, 1, 1)
          if #sel > 0 then chosen = sel[1] end
          for _, i in ipairs(sel) do ctx:discard_card(i) end
        end
      else
        local cv = ctx:cover_cards(ctx:opp())
        if #cv > 0 then
          local idx = ctx:random_index(#cv)
          chosen = cv[idx]
          ctx:discard_card(chosen)
        end
      end
      if chosen >= 0 and ctx:is_attack(chosen) then
        ctx:lose_life(ctx:opp(), ctx:keisou_amount(1))
      else
        local hand = ctx:hand(ctx:opp())
        if #hand > 0 then
          local sel = ctx:choose_cards_for(ctx:opp(), "对手盖伏1张手牌", hand, 1, 1)
          for _, i in ipairs(sel) do ctx:cover_card(i) end
        end
      end
    end },

  { set = "kururu.A1", form = "A1", num = 3, name = "探查测度", kind = "normal", type = "action",
    on_play = function(ctx)
      ctx:discard_top(ctx:opp())
      local pile = {}
      for _, i in ipairs(ctx:discard_pile(ctx:opp())) do
        if ctx:is_normal_card(i) then pile[#pile + 1] = i end
      end
      if #pile == 0 then return end
      -- 先把选牌放在机巧之外，再按“红 + 该牌类别色 + 副类别色”着色判定
      local sel = ctx:choose_cards("探查测度：从对手弃牌选择一张正面向上的常规牌", pile, 0, 1)
      if #sel == 0 then return end
      local card = sel[1]
      local combo = "R" .. ctx:card_colors_str(card)
      if ctx:keisou(combo) then ctx:use_foreign_card(card) end
    end },

  { set = "kururu.A1", form = "A1", num = 3, name = "最终搜寻", kind = "special", type = "action",
    cost = 2,
    on_play = function(ctx)
      ctx:reveal_opponent_cuts(ctx:player())  -- 先查看对手切牌（可偷取）
      local opp = ctx:opp()
      local target = -1
      local cov = ctx:cover_cards(opp)
      if #cov > 0 then
        local sel = ctx:choose_cards_for(opp, "最终搜寻：从你的盖牌堆选择一张", cov, 1, 1)
        if #sel > 0 then target = sel[1] end
      else
        ctx:cover_top(opp)
        local c2 = ctx:cover_cards(opp)
        if #c2 > 0 then target = c2[#c2] end
      end
      if target < 0 then return end
      if ctx:guess(target) then
        local c = ctx:load_int("count", 0) + 1
        ctx:store_int("count", c)
        if c >= 2 then
          local who = ctx:choose("最终搜寻：加入哪一方的未选用切牌？", { "你的", "对手的" })
          ctx:add_unused_cuts(who == 1 and ctx:player() or ctx:opp())
          local e = ctx:gain_extra("壮绝旅程")
          if e >= 0 then ctx:reset_special(e) end
          ctx:remove_card(ctx:source_inst())
        end
      end
    end,
    triggers = {
      { event = "turn_end",
        cond = function(ctx, ev)
          return ev:subject() == ctx:player() and
                 (ctx:rebuilt_this_turn(ctx:player()) or ctx:used_fullpower_this_turn(ctx:player()))
        end,
        run = function(ctx, ev) ctx:reuse_special(ctx:source_inst()) end },
    } },

  { set = "kururu.A1", form = "A1", num = 902, name = "壮绝旅程", kind = "special",
    type = "action", cost = 0, extra = true,
    on_play = function(ctx) ctx:move("flare", "dust", ctx:flare(ctx:player()), ctx:player(), ctx:player()) end,
    continuous = {
      { when = "used", query = "cost",
        apply = function(ctx, atk) end },
    } },

  ---------------------------------------------------------------------------
  -- 变格 A2 友谊
  ---------------------------------------------------------------------------
  { set = "kururu.A2", form = "A2", num = 1, name = "激光枪", kind = "normal", type = "attack",
    attack = { range = {2, 6}, damage = { aura = 0, life = 0 }, keywords = { "unrespondable" } },
    on_attack_after = function(ctx)
      local g = ctx:keisou_other("G")
      local bp = ctx:keisou_other("BP")
      if g then ctx:attack { range = {0, 6}, damage = { aura = ctx:keisou_amount(1), life = 1 } } end
      if bp then ctx:attack { range = {0, 6}, damage = { aura = ctx:keisou_amount(1), life = 1 } } end
    end },

  { set = "kururu.A2", form = "A2", num = 2, name = "电磁炮", kind = "normal", type = "attack",
    attack = function(ctx)
      local a, l = 1, 2
      local kw = {}
      if ctx:keisou("RR") then a = a + ctx:keisou_amount(1) end
      if ctx:keisou("Y") then
        a = a + ctx:keisou_amount(1)
        l = l + ctx:keisou_amount(1)
        kw[#kw + 1] = "unrespondable"
      end
      return { range = {2, 6}, damage = { aura = a, life = l }, keywords = kw }
    end },

  { set = "kururu.A2", form = "A2", num = 4, name = "枢的骇客装置", kind = "special",
    type = "action", cost = 1,
    on_play = function(ctx)
      if ctx:keisou_other("GBP") then ctx:dust_to_card(ctx:source_inst(), 1) end
    end,
    triggers = {
      { event = "turn_end",
        cond = function(ctx, ev) return ev:subject() == ctx:player() and ctx:keisou_other("GBP") end,
        run = function(ctx, ev) ctx:dust_to_card(ctx:source_inst(), 1) end },
    } },

}
