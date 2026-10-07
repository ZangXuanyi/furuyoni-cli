-- 07-Shinra 森罗
-- 机制 策略：始终持有一个计策（开局神算且公开）。带神算～/鬼谋～的牌结算时使用当前策略，
-- 随后玩家秘密重新准备下一个（强制，对手不再知晓）。
-- 牌上 神算/鬼谋 的部分写在 keiryo(ctx, s) 中（s: 0=神算, 1=鬼谋），供 A1-S3 强制执行计略。

return {

  ---------------------------------------------------------------------------
  -- 本格 O 书
  ---------------------------------------------------------------------------
  { set = "shinra", form = "O", num = 1, name = "立论", kind = "normal", type = "attack",
    -- 立论没有独立的"攻击后"段落 => 条件在结算时重判（裁定），而不是声明时锁定。
    attack = function(ctx) return { range = {2, 7} } end,
    on_attack_after = function(ctx)
      if ctx:deck_size(ctx:opp()) >= 2 then
        ctx:cover_top(ctx:opp())
        ctx:cover_top(ctx:opp())
      else
        ctx:deal_damage(ctx:opp(), 2, nil)
      end
    end },

  { set = "shinra", form = "O", num = 2, name = "驳论", kind = "normal", type = "attack",
    response = true,
    attack = { range = {2, 7}, damage = { aura = 1 } },
    on_attack_after = function(ctx)
      local a = ctx:responding_attack()
      if a then a:negate_damage() end
      ctx:draw(ctx:opp(), 1)
    end },

  { set = "shinra", form = "O", num = 3, name = "诡辩", kind = "normal", type = "attack",
    full_power = true,
    attack = { range = {3, 8}, damage = { life = 1 } },
    keiryo = function(ctx, s)
      if s == 0 then
        ctx:cover_top(ctx:opp())
        ctx:cover_top(ctx:opp())
        ctx:cover_top(ctx:opp())
      else
        local pile = {}
        for _, i in ipairs(ctx:discard_pile(ctx:opp())) do
          if ctx:opponent_pickable(i) then pile[#pile + 1] = i end
        end
        if #pile > 0 then
          local sel = ctx:choose_cards("诡辩：从对手弃牌选一张使用", pile, 1, 1)
          for _, i in ipairs(sel) do ctx:use_foreign_card(i) end
        end
      end
    end,
    on_attack_after = function(ctx)
      ctx:execute_keiryo(ctx:source_inst(), ctx:strategy(ctx:player()))
      ctx:prepare_strategy(ctx:player())
    end },

  { set = "shinra", form = "O", num = 4, name = "引用", kind = "normal", type = "action",
    on_play = function(ctx)
      local opp = ctx:opp()
      local pool = {}
      for _, i in ipairs(ctx:hand(opp)) do
        if ctx:is_attack(i) and ctx:opponent_pickable(i) then pool[#pool + 1] = i end
      end
      if #pool == 0 then return end
      local sel = ctx:choose_cards("引用：选择对手一张攻击牌", pool, 1, 1)
      if #sel == 0 then return end
      local card = sel[1]
      local c = ctx:choose("引用", { "使用之", "盖伏之", "什么都不做" })
      if c == 1 then
        ctx:use_foreign_card(card)
      elseif c == 2 then
        ctx:cover_card(card)
      end
      if ctx:is_full_power(card) then ctx:end_current_main() end
    end },

  { set = "shinra", form = "O", num = 5, name = "煽动", kind = "normal", type = "action",
    response = true,
    keiryo = function(ctx, s)
      if s == 0 then
        ctx:move("dust", "distance", 1)
      else
        ctx:move("distance", "aura", 1, ctx:player(), ctx:opp())
      end
    end,
    on_play = function(ctx)
      ctx:execute_keiryo(ctx:source_inst(), ctx:strategy(ctx:player()))
      ctx:prepare_strategy(ctx:player())
    end },

  { set = "shinra", form = "O", num = 6, name = "壮语", kind = "normal", type = "enhance",
    nagi = 2,
    keiryo = function(ctx, s)
      if s == 0 then
        ctx:gain_vigor(ctx:player(), 1)
        ctx:to_deck_top(ctx:source_inst())
      else
        if ctx:hand_size(ctx:opp()) <= 1 then
          ctx:cower(ctx:opp())
          ctx:draw(ctx:opp(), 3)
          local hand = ctx:hand(ctx:opp())
          local n = math.min(2, #hand)
          if n > 0 then
            local sel = ctx:choose_cards_for(ctx:opp(), "壮语：弃 2 张", hand, n, n)
            for _, i in ipairs(sel) do ctx:discard_card(i) end
          end
        end
      end
    end,
    on_discard = function(ctx)
      ctx:execute_keiryo(ctx:source_inst(), ctx:strategy(ctx:player()))
      ctx:prepare_strategy(ctx:player())
    end },

  { set = "shinra", form = "O", num = 7, name = "论破", kind = "normal", type = "enhance",
    nagi = 4,
    on_expand = function(ctx)
      local pile = {}
      for _, i in ipairs(ctx:discard_pile(ctx:opp())) do
        if ctx:opponent_pickable(i) then pile[#pile + 1] = i end
      end
      if #pile == 0 then return end
      local sel = ctx:choose_cards("论破：从对手弃牌封印一张", pile, 1, 1)
      for _, i in ipairs(sel) do ctx:seal_card(ctx:source_inst(), i) end
    end,
    on_discard = function(ctx) ctx:return_sealed(ctx:source_inst()) end },

  { set = "shinra", form = "O", num = 1, name = "完全论破", kind = "special", type = "action",
    cost = 2,
    on_play = function(ctx)
      local pool = {}
      for _, i in ipairs(ctx:discard_pile(ctx:opp())) do
        if ctx:is_normal_card(i) and ctx:opponent_pickable(i) then pool[#pool + 1] = i end
      end
      if #pool == 0 then return end
      local sel = ctx:choose_cards("完全论破：封印对手弃牌一张常规牌", pool, 1, 1)
      for _, i in ipairs(sel) do ctx:seal_card(ctx:source_inst(), i) end
    end },

  { set = "shinra", form = "O", num = 2, name = "诸式理解", kind = "special", type = "action",
    cost = 2,
    keiryo = function(ctx, s)
      if s == 0 then
        local pool = {}
        for _, i in ipairs(ctx:discard_pile(ctx:player())) do
          if ctx:is_enhance(i) then pool[#pool + 1] = i end
        end
        for _, i in ipairs(ctx:used_specials(ctx:player())) do
          if ctx:is_enhance(i) then pool[#pool + 1] = i end
        end
        if #pool == 0 then return end
        local sel = ctx:choose_cards("诸式理解：选一张付与牌免费使用", pool, 1, 1)
        if #sel == 0 then return end
        local card = sel[1]
        local fp = ctx:is_full_power(card)
        if ctx:card_zone(card) == "special" then
          ctx:reuse_special(card)
        else
          ctx:use_card(card, false)
        end
        if fp then ctx:end_current_main() end
      else
        local enh = {}
        for _, i in ipairs(ctx:enhances(ctx:opp())) do enh[#enh + 1] = i end
        if #enh == 0 then return end
        local sel = ctx:choose_cards("诸式理解：选对手一张付与", enh, 1, 1)
        for _, i in ipairs(sel) do
          local c = ctx:crystals(i)
          if c > 0 then ctx:drain_card_crystals(i, c) end
        end
      end
    end,
    on_play = function(ctx)
      ctx:execute_keiryo(ctx:source_inst(), ctx:strategy(ctx:player()))
      ctx:prepare_strategy(ctx:player())
    end },

  { set = "shinra", form = "O", num = 3, name = "天地反驳", kind = "special", type = "enhance",
    nagi = 5, full_power = true,
    continuous = {
      -- 官方 QA：数值替换先于任何数值增减，故标记 replace=true（先于 next_attack_mod）。
      { when = "expanded", query = "attack", replace = true,
        apply = function(ctx, atk)
          if atk:attacker() == ctx:player() then atk:swap_damage() end
        end },
    } },

  { set = "shinra", form = "O", num = 4, name = "森罗判证", kind = "special", type = "enhance",
    nagi = 6,
    on_expand = function(ctx) ctx:move("dust", "life", 2, ctx:player(), ctx:player()) end,
    triggers = {
      { event = "enhance_left",
        cond = function(ctx, ev)
          -- 只有"你的"其他付与牌离场才触发。
          return ev:subject() == ctx:player() and ev:card() ~= ctx:source_inst()
        end,
        run = function(ctx, ev) ctx:lose_life(ctx:opp(), 1) end },
    },
    on_discard = function(ctx) ctx:die(ctx:player()) end },

  ---------------------------------------------------------------------------
  -- 变格 A1 经
  ---------------------------------------------------------------------------
  { set = "shinra.A1", form = "A1", num = 2, name = "真言", kind = "normal", type = "action",
    response = true,
    keiryo = function(ctx, s)
      if s == 0 then
        if ctx:deck_size(ctx:opp()) >= 3 then ctx:lose_life(ctx:opp(), 1) end
      else
        if ctx:deck_size(ctx:opp()) <= 3 then ctx:deal_damage(ctx:opp(), 2, nil) end
      end
    end,
    on_play = function(ctx)
      if ctx:responding_attack() == nil then return end
      ctx:execute_keiryo(ctx:source_inst(), ctx:strategy(ctx:player()))
      ctx:prepare_strategy(ctx:player())
    end },

  { set = "shinra.A1", form = "A1", num = 7, name = "使徒", kind = "normal", type = "enhance",
    nagi = 2, full_power = true,
    keiryo = function(ctx, s)
      local rng = { {1, 1}, {3, 3}, {5, 5} }
      if s == 0 then
        ctx:attack { range = rng, damage = { aura = 2, life = 2 }, keywords = { "lock" },
          after = function(c2, a)
            local opp = c2:opp()
            local hand = c2:hand(opp)
            if #hand > 0 then
              local sel = c2:choose_cards_for(opp, "使徒：从手牌选 1 张置牌库底", hand, 1, 1)
              for _, i in ipairs(sel) do c2:to_deck_bottom(i) end
            end
          end }
      else
        ctx:attack { range = rng, damage = { aura = 2, life = 2 }, keywords = { "lock" },
          after = function(c2, a)
            local opp = c2:opp()
            c2:discard_top(opp)
            c2:cover_top(opp)
          end }
      end
    end,
    on_expand = function(ctx)
      if ctx:vigor(ctx:player()) >= 1 then
        if ctx:choose("使徒：支付 1 集中力并重新准备计策？", { "是", "否" }) == 1 then
          ctx:set_vigor(ctx:player(), ctx:vigor(ctx:player()) - 1)
          ctx:prepare_strategy(ctx:player())
        end
      end
      ctx:execute_keiryo(ctx:source_inst(), ctx:strategy(ctx:player()))
      ctx:prepare_strategy(ctx:player())
    end,
    on_discard = function(ctx)
      ctx:execute_keiryo(ctx:source_inst(), ctx:strategy(ctx:player()))
      ctx:prepare_strategy(ctx:player())
    end },

  { set = "shinra.A1", form = "A1", num = 3, name = "全知全能", kind = "special", type = "attack",
    cost = 4, full_power = true,
    attack = { range = {0, 5}, damage = { aura = 2, life = 2 }, keywords = { "unrespondable" } },
    on_attack_after = function(ctx)
      local pool = {}
      for _, i in ipairs(ctx:hand(ctx:player())) do pool[#pool + 1] = i end
      for _, i in ipairs(ctx:cover_cards(ctx:player())) do pool[#pool + 1] = i end
      if #pool > 0 then
        local sel = ctx:choose_cards("全知全能：将任意多张手牌/盖牌置入弃牌区", pool, 0, #pool)
        for _, i in ipairs(sel) do ctx:discard_card(i) end
      end
      local s = ctx:strategy(ctx:player())
      local kei = {}
      for _, i in ipairs(ctx:discard_pile(ctx:player())) do
        if ctx:has_keiryo(i) then kei[#kei + 1] = i end
      end
      if #kei == 0 then return end
      local sel = ctx:choose_cards("全知全能：选择执行其计略的牌", kei, 0, #kei)
      for _, i in ipairs(sel) do ctx:execute_keiryo(i, s) end
    end },

}
