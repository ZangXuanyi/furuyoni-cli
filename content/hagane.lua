-- 08-Hagane 破钟
-- 机制 离心：要打出带离心的牌，需当前距 >= 回合开始时距 + 2，且本回合你尚未攻击过。
-- 全开：可作为本回合唯一动作打出（同全力），并获得额外效果。O=锤；A1=金(砧)。

return {

  ---------------------------------------------------------------------------
  -- 本格 O 锤
  ---------------------------------------------------------------------------
  { set = "hagane", form = "O", num = 1, name = "离心击", kind = "normal", type = "attack",
    centrifugal = true, terminal = true,
    attack = { range = {2, 6}, damage = { aura = 5, life = 3 } },
    on_attack_after = function(ctx)
      if not ctx:is_my_turn() then return end
      for _, i in ipairs(ctx:hand(0)) do ctx:cover_card(i) end
      for _, i in ipairs(ctx:hand(1)) do ctx:cover_card(i) end
      ctx:set_vigor(ctx:player(), 0)
    end },

  { set = "hagane", form = "O", num = 2, name = "砂风尘", kind = "normal", type = "attack",
    attack = { range = {0, 6}, damage = { aura = 1 } },
    on_attack_after = function(ctx)
      local d = ctx:distance() - ctx:distance_at_turn_start()
      if math.abs(d) >= 2 then ctx:random_discard(ctx:opp()) end
    end },

  { set = "hagane", form = "O", num = 3, name = "裂地击", kind = "normal", type = "attack",
    full_power = true,
    attack = { range = {0, 3}, damage = { aura = 2 }, keywords = { "unrespondable" } },
    on_attack_after = function(ctx)
      ctx:set_vigor(ctx:opp(), 0)
      ctx:cower(ctx:opp())
    end },

  { set = "hagane", form = "O", num = 4, name = "超反动", kind = "normal", type = "action",
    on_play = function(ctx)
      if ctx:distance() >= 5 then
        ctx:move("distance", "flare", 1, ctx:player(), ctx:player())
      elseif ctx:distance() <= 4 then
        ctx:move("flare", "distance", 1, ctx:opp(), ctx:player())
      end
    end },

  { set = "hagane", form = "O", num = 5, name = "流星锤", kind = "normal", type = "action",
    centrifugal = true,
    on_play = function(ctx)
      if ctx:flare(ctx:opp()) >= 3 then
        ctx:move("flare", "aura", 2, ctx:opp(), ctx:player())
      end
    end },

  { set = "hagane", form = "O", num = 6, name = "大鸣钟", kind = "normal", type = "action",
    centrifugal = true,
    on_play = function(ctx)
      if ctx:choose("大鸣钟", { "+2/+1", "不可对且远+1" }) == 1 then
        ctx:next_attack_mod { apply = function(c2, atk) atk:add { aura = 2, life = 1 } end }
      else
        ctx:next_attack_mod {
          apply = function(c2, atk)
            atk:keyword("unrespondable")
            atk:extend_far(1)
          end,
        }
      end
    end },

  { set = "hagane", form = "O", num = 7, name = "引力场", kind = "normal", type = "enhance",
    nagi = 2, zenkai = true,
    on_expand = function(ctx)
      local n = ctx:zenkai() and 2 or 1
      ctx:move("distance", "aura", n, ctx:player(), ctx:player())
    end,
    near_distance_mod = -1 },

  { set = "hagane", form = "O", num = 1, name = "大天空·破限", kind = "special", type = "attack",
    cost = 4,
    attack = function(ctx)
      local x = math.abs(ctx:distance() - ctx:distance_at_turn_start())
      local y = math.ceil(x / 2)
      return { range = {0, 10}, damage = { aura = x, life = y }, keywords = { "overwhelm" } }
    end },

  { set = "hagane", form = "O", num = 2, name = "大破钟·断限", kind = "special", type = "action",
    cost = 2,
    on_play = function(ctx)
      local me = ctx:player()
      local anyUnused = false
      for _, i in ipairs(ctx:special_cards(me)) do
        if i ~= ctx:source_inst() and not ctx:is_used(i) then anyUnused = true end
      end
      if not anyUnused then ctx:move("dust", "life", 2, me, me) end
    end },

  { set = "hagane", form = "O", num = 3, name = "大重力·无限", kind = "special", type = "action",
    cost = 5,
    on_play = function(ctx) ctx:move("distance", "flare", 3, ctx:player(), ctx:player()) end,
    reset = { kind = "end_turn", cond = function(ctx)
      return ctx:played_centrifugal_this_turn(ctx:player()) and not ctx:used_this_turn(ctx:source_inst())
    end } },

  { set = "hagane", form = "O", num = 4, name = "大山脉·转限", kind = "special", type = "action",
    cost = 3, centrifugal = true,
    on_play = function(ctx)
      for _ = 1, 2 do
        if ctx:choose("大山脉·转限", { "弃置你的牌库", "从弃牌使用一张非全力牌" }) == 1 then
          ctx:discard_deck(ctx:player())
        else
          local pool = {}
          for _, c in ipairs(ctx:discard_pile(ctx:player())) do
            if not ctx:is_full_power(c) then pool[#pool + 1] = c end
          end
          if #pool > 0 then
            local sel = ctx:choose_cards("转限：选一张非全力牌使用", pool, 0, 1)
            for _, c in ipairs(sel) do ctx:use_card(c, false) end
          end
        end
      end
    end },

  ---------------------------------------------------------------------------
  -- 变格 A1 金（砧）
  ---------------------------------------------------------------------------
  { set = "hagane.A1", form = "A1", num = 1, name = "炉火", kind = "normal", type = "action",
    on_play = function(ctx)
      if ctx:distance() >= 3 then
        ctx:move("distance", "flare", 1, ctx:player(), ctx:player())
        ctx:move("distance", "flare", 1, ctx:opp(), ctx:opp())
      end
    end },

  { set = "hagane.A1", form = "A1", num = 2, name = "旋回起", kind = "normal", type = "action",
    on_play = function(ctx)
      local d = ctx:distance() - ctx:distance_at_turn_start()
      if math.abs(d) >= 2 then
        ctx:draw(ctx:player(), 1)
        ctx:gain_vigor(ctx:player(), 1)
      end
    end },

  { set = "hagane.A1", form = "A1", num = 1, name = "大炼成·原限", kind = "special",
    type = "action", cost = 1,
    on_play = function(ctx)
      local me = ctx:player()
      if ctx:sealed_card(ctx:source_inst()) < 0 then
        local pool = {}
        for _, c in ipairs(ctx:hand(me)) do
          if ctx:is_attack(c) and not ctx:card_is_goddess(c, "hagane") then pool[#pool + 1] = c end
        end
        if #pool > 0 then
          local sel = ctx:choose_cards("大炼成：封印一张非破钟攻击牌", pool, 1, 1)
          for _, c in ipairs(sel) do ctx:seal_card(ctx:source_inst(), c) end
        end
        -- 只有确实封印了牌才产出「炼成攻击」（否则会得到一张空规格的复制）。
        if ctx:sealed_card(ctx:source_inst()) >= 0 then
          local e = ctx:gain_extra("炼成攻击")
          if e >= 0 then ctx:to_deck_bottom(e) end
        end
      else
        ctx:dust_to_card(ctx:source_inst(), 1)
      end
    end,
    reset = { kind = "end_turn", cond = function(ctx)
      return ctx:played_liancheng_this_turn(ctx:player())
    end } },

  { set = "hagane.A1", form = "A1", num = 999, name = "炼成攻击", kind = "normal",
    type = "attack", extra = true, unsealable = true, no_opponent_pick = true,
    attack = function(ctx)
      local me = ctx:player()
      local host = ctx:find_named(me, "大炼成·原限")
      local sealed = (host >= 0) and ctx:sealed_card(host) or -1
      local spec
      if sealed >= 0 then
        spec = ctx:card_attack_spec(sealed)
      else
        spec = { range = { { 0, 0 } }, damage = {} }
      end
      local cr = (host >= 0) and ctx:crystals(host) or 0
      local addA, addL, far = 1, 0, 1
      if cr == 1 then addA, addL, far = 1, 1, 2
      elseif cr >= 2 then addA, addL, far = 2, 2, 3 end
      spec.damage = spec.damage or {}
      spec.damage.aura = (spec.damage.aura or 0) + addA
      spec.damage.life = (spec.damage.life or 0) + addL
      local spans = {}
      local r = spec.range or {}
      if type(r[1]) == "table" then
        for _, s in ipairs(r) do spans[#spans + 1] = { s[1], s[2] } end
      else
        spans = { { r[1] or 0, r[2] or r[1] or 0 } }
      end
      local mi = 1
      for i, s in ipairs(spans) do if s[2] > spans[mi][2] then mi = i end end
      spans[mi][2] = spans[mi][2] + far
      spec.range = spans
      spec.keywords = spec.keywords or {}
      if cr >= 2 then spec.keywords[#spec.keywords + 1] = "no_negate" end
      return spec
    end },

}
