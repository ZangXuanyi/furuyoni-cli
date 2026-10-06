-- 09-Chikage 暗昏千影
-- 机制 毒：毒袋有 5 张毒牌（公开、可指定）。毒牌不能被弃置/盖伏，只能被打出或特殊移除；
-- 洗进抽牌堆时自动上浮到顶；对手手牌>=3 毒时盖伏阶段保留毒牌、盖伏所有其他常规牌。
-- O=毒；A1=绊。

return {

  ---------------------------------------------------------------------------
  -- 本格 O 毒
  ---------------------------------------------------------------------------
  { set = "chikage", form = "O", num = 1, name = "飞苦无", kind = "normal", type = "attack",
    attack = { range = {4, 5}, damage = { aura = 2, life = 2 } } },

  { set = "chikage", form = "O", num = 2, name = "毒针", kind = "normal", type = "attack",
    attack = { range = {4, 4}, damage = { aura = 1, life = 1 } },
    on_play = function(ctx)
      local pool = {}
      for _, i in ipairs(ctx:poison_bag(ctx:player())) do
        local n = ctx:card_name(i)
        if n == "麻痹毒" or n == "迟缓毒" or n == "幻觉毒" then pool[#pool + 1] = i end
      end
      if #pool == 0 then return end
      local sel = ctx:choose_cards("毒针：选择一张毒置于对手牌库顶", pool, 1, 1)
      for _, i in ipairs(sel) do ctx:place_poison(i, ctx:opp(), "deck_top") end
    end },

  { set = "chikage", form = "O", num = 3, name = "遁术", kind = "normal", type = "attack",
    response = true,
    attack = { range = {1, 3}, damage = { aura = 1 } },
    on_attack_after = function(ctx)
      ctx:move("aura", "distance", 1, ctx:player(), ctx:player())
      ctx:move("dust", "distance", 1)
      ctx:set_cannot_advance(ctx:opp())
    end },

  { set = "chikage", form = "O", num = 4, name = "暗器", kind = "normal", type = "attack",
    response = true, zenkai = true,
    attack = function(ctx)
      if ctx:zenkai() then return { range = {1, 5}, damage = { aura = 2, life = 3 } } end
      return { range = {1, 5}, damage = { aura = 1, life = 1 } }
    end,
    on_attack_after = function(ctx)
      local opp = ctx:opp()
      for _, i in ipairs(ctx:hand(opp)) do
        if ctx:is_poison(i) then
          ctx:move("dust", "aura", 1, ctx:player(), ctx:player())
          break
        end
      end
      if ctx:zenkai() then
        local bag = ctx:poison_bag(ctx:player())
        if #bag > 0 then
          local sel = ctx:choose_cards_for(opp, "暗器：从毒袋选一张加入手牌", bag, 1, 1)
          for _, i in ipairs(sel) do ctx:place_poison(i, opp, "hand") end
        end
      end
    end },

  { set = "chikage", form = "O", num = 5, name = "毒雾", kind = "normal", type = "action",
    on_play = function(ctx)
      local pool = {}
      for _, i in ipairs(ctx:poison_bag(ctx:player())) do
        local n = ctx:card_name(i)
        if n == "麻痹毒" or n == "迟缓毒" or n == "幻觉毒" then pool[#pool + 1] = i end
      end
      if #pool == 0 then return end
      local sel = ctx:choose_cards("毒雾：选择一张毒加入对手手牌", pool, 1, 1)
      for _, i in ipairs(sel) do ctx:place_poison(i, ctx:opp(), "hand") end
    end },

  { set = "chikage", form = "O", num = 6, name = "蹑足", kind = "normal", type = "enhance",
    nagi = 4, breakable = true, distance_mod = -2 },

  { set = "chikage", form = "O", num = 7, name = "泥泞", kind = "normal", type = "enhance",
    nagi = 2 },

  { set = "chikage", form = "O", num = 1, name = "魂毒渐灭灯", kind = "special", type = "action",
    cost = 3,
    on_play = function(ctx)
      local pool = {}
      for _, i in ipairs(ctx:poison_bag(ctx:player())) do
        if ctx:card_name(i) == "灭灯毒" then pool[#pool + 1] = i end
      end
      if #pool == 0 then return end
      local sel = ctx:choose_cards("魂毒渐灭灯：灭灯毒置于对手牌库顶", pool, 1, 1)
      for _, i in ipairs(sel) do ctx:place_poison(i, ctx:opp(), "deck_top") end
    end },

  { set = "chikage", form = "O", num = 2, name = "缠毒揭叛旗", kind = "special", type = "enhance",
    nagi = 5, response = true,
    on_enter = function(ctx)
      -- 打出即打消"被对应的这次攻击"（若它属于 X/- 或 -/Y 类）。
      local a = ctx:responding_attack()
      if a then
        local ad = a:aura_damage()
        local ld = a:life_damage()
        if (ad ~= nil and ld == nil) or (ld ~= nil and ad == nil) then a:negate() end
      end
    end,
    continuous = {
      { when = "expanded", query = "attack",
        apply = function(ctx, atk)
          if atk:attacker() == ctx:opp() then
            local a = atk:aura_damage()
            local l = atk:life_damage()
            if (a ~= nil and l == nil) or (l ~= nil and a == nil) then atk:negate() end
          end
        end },
    } },

  { set = "chikage", form = "O", num = 3, name = "霞毒空流转", kind = "special", type = "attack",
    cost = 1,
    attack = { range = {3, 7}, damage = { aura = 1, life = 2 } },
    reset = { kind = "end_turn", cond = function(ctx) return ctx:hand_size(ctx:opp()) >= 2 end } },

  { set = "chikage", form = "O", num = 4, name = "暗昏千影的信条", kind = "special",
    type = "enhance", nagi = 4, full_power = true, breakable = true,
    on_discard = function(ctx)
      local me = ctx:player()
      local allUsed = true
      for _, i in ipairs(ctx:special_cards(me)) do
        if i ~= ctx:source_inst() and not ctx:is_used(i) then allUsed = false end
      end
      if allUsed then ctx:die(ctx:opp()) end
    end },

  ---------------------------------------------------------------------------
  -- 毒牌（毒袋，不属于构筑）
  ---------------------------------------------------------------------------
  { set = "chikage", form = "O", num = 901, name = "麻痹毒", kind = "normal", type = "action",
    poison = true, terminal = true,
    playable = function(ctx) return not ctx:did_basic_this_turn(ctx:player()) end },

  { set = "chikage", form = "O", num = 902, name = "幻觉毒", kind = "normal", type = "action",
    poison = true,
    on_play = function(ctx) ctx:move("flare", "dust", 2, ctx:player(), ctx:player()) end },

  { set = "chikage", form = "O", num = 903, name = "灭灯毒", kind = "normal", type = "action",
    poison = true, copies = 2,
    on_play = function(ctx) ctx:move("aura", "dust", 3, ctx:player(), ctx:player()) end },

  { set = "chikage", form = "O", num = 904, name = "迟缓毒", kind = "normal", type = "enhance",
    poison = true, nagi = 3,
    on_discard = function(ctx) ctx:return_poison(ctx:source_inst()) end },

  ---------------------------------------------------------------------------
  -- 变格 A1 绊
  ---------------------------------------------------------------------------
  { set = "chikage.A1", form = "A1", num = 5, name = "机关油纸伞", kind = "normal", type = "attack",
    attack = function(ctx)
      if ctx:hand_size(ctx:opp()) >= 2 then
        return { range = {2, 6}, damage = { aura = 2, life = 1 }, keywords = { "unrespondable" } }
      end
      return { range = {4, 4}, damage = { aura = 2, life = 1 }, keywords = { "unrespondable" } }
    end },

  { set = "chikage.A1", form = "A1", num = 6, name = "奋迅", kind = "normal", type = "action",
    on_play = function(ctx)
      if ctx:hand_size(ctx:opp()) >= 2 then ctx:gain_vigor(ctx:player(), 1) end
      ctx:move("distance", "dust", 1)
    end },

  { set = "chikage.A1", form = "A1", num = 4, name = "绊毒余残滓", kind = "special",
    type = "attack", cost = 4,
    attack = function(ctx)
      local x = ctx:hand_size(ctx:opp()) * 2
      return { range = {0, 1}, damage = { aura = 4, life = x }, keywords = { "unrespondable" } }
    end },

}
