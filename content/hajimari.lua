-- 《最初的决斗》两张教学卡组 (rules/00-hajimari.md)
-- 每张牌 = 元数据 + 行为钩子。引擎不认识任何具体牌，只认识这些声明。

return {

  ---------------------------------------------------------------------------
  -- 虚路的碎片 (固定先手)  9 常 + 4 切
  ---------------------------------------------------------------------------
  { set = "hajimari.ukiro", num = 1, name = "投射", kind = "normal", type = "attack",
    attack = { range = {5, 9}, damage = { aura = 3, life = 1 } } },

  { set = "hajimari.ukiro", num = 2, name = "牵制", kind = "normal", type = "attack",
    attack = { range = {1, 3}, damage = { aura = 2, life = 1 } } },

  { set = "hajimari.ukiro", num = 3, name = "背刺", kind = "normal", type = "attack",
    attack = { range = {1, 1}, damage = { aura = 3, life = 2 } } },

  { set = "hajimari.ukiro", num = 4, name = "横斩", kind = "normal", type = "attack",
    attack = { range = {2, 3}, damage = { aura = 2, life = 2 } } },

  { set = "hajimari.ukiro", num = 5, name = "二刀一闪", kind = "normal", type = "attack",
    full_power = true,
    attack = { range = {2, 3}, damage = { aura = 4, life = 2 } } },

  { set = "hajimari.ukiro", num = 6, name = "潜行", kind = "normal", type = "action",
    response = true,
    on_play = function(ctx) ctx:move("distance", "dust", 1) end },

  { set = "hajimari.ukiro", num = 7, name = "恶疾", kind = "normal", type = "action",
    response = true,
    on_play = function(ctx)
      local a = ctx:responding_attack()
      if a then a:add { aura = -1 } end
      ctx:cower(ctx:opp())
    end },

  { set = "hajimari.ukiro", num = 8, name = "步法", kind = "normal", type = "action",
    on_play = function(ctx)
      ctx:gain_vigor(ctx:player(), 1)
      if ctx:choose("步法", { "1 距 -> 虚", "1 虚 -> 距" }) == 1 then
        ctx:move("distance", "dust", 1)
      else
        ctx:move("dust", "distance", 1)
      end
    end },

  { set = "hajimari.ukiro", num = 9, name = "阴之阱", kind = "normal", type = "enhance",
    nagi = 2, breakable = true,
    on_discard = function(ctx)
      ctx:attack { range = {2, 3}, damage = { aura = 3, life = 2 }, keywords = { "unrespondable" } }
    end },

  { set = "hajimari.ukiro", num = 10, name = "强夺之棘", kind = "special", type = "action",
    cost = 1, full_power = true,
    on_play = function(ctx)
      ctx:discard_all_hand(ctx:opp())
      ctx:vigor_to(ctx:opp(), 0)
    end,
    reset = { kind = "end_turn", cond = function(ctx) return ctx:dust() >= 10 end } },

  { set = "hajimari.ukiro", num = 11, name = "苦痛之壳", kind = "special", type = "action",
    cost = 3, response = true,
    on_play = function(ctx)
      local a = ctx:responding_attack()
      if a then a:add { aura = -1 } end
      ctx:move("aura", "dust", 2, ctx:opp(), ctx:opp())
    end },

  { set = "hajimari.ukiro", num = 12, name = "无穷之刃", kind = "special", type = "attack",
    cost = 5,
    attack = { range = {1, 2}, damage = { aura = 4, life = 3 } } },

  { set = "hajimari.ukiro", num = 13, name = "静默之声", kind = "special", type = "action",
    cost = 4,
    on_play = function(ctx) ctx:draw(ctx:player(), 2) end },

  ---------------------------------------------------------------------------
  -- 仄佳的碎片 (固定后手)  9 常 + 4 切
  ---------------------------------------------------------------------------
  { set = "hajimari.okika", num = 1, name = "瞬灵式", kind = "normal", type = "attack",
    attack = { range = {5, 5}, damage = { aura = 3, life = 2 }, keywords = { "unrespondable" } } },

  { set = "hajimari.okika", num = 2, name = "花瓣刃", kind = "normal", type = "attack",
    attack = { range = {4, 5}, damage = { life = 1 } } },

  { set = "hajimari.okika", num = 3, name = "樱花刀", kind = "normal", type = "attack",
    attack = { range = {3, 4}, damage = { aura = 3, life = 1 } } },

  { set = "hajimari.okika", num = 4, name = "回身斩", kind = "normal", type = "attack",
    response = true,
    attack = { range = {2, 4}, damage = { aura = 2, life = 1 } },
    on_attack_after = function(ctx) ctx:move("dust", "aura", 1, ctx:player(), ctx:player()) end },

  { set = "hajimari.okika", num = 5, name = "光之刃", kind = "normal", type = "attack",
    attack = { range = {3, 5},
               damage = { aura = function(ctx) return ctx:flare(ctx:player()) end, life = 1 },
               keywords = { "overwhelm" } } },

  { set = "hajimari.okika", num = 6, name = "步法", kind = "normal", type = "action",
    on_play = function(ctx)
      ctx:gain_vigor(ctx:player(), 1)
      if ctx:choose("步法", { "1 距 -> 虚", "1 虚 -> 距" }) == 1 then
        ctx:move("distance", "dust", 1)
      else
        ctx:move("dust", "distance", 1)
      end
    end },

  { set = "hajimari.okika", num = 7, name = "唤樱", kind = "normal", type = "action",
    response = true,
    on_play = function(ctx) ctx:move("aura", "aura", 1, ctx:opp(), ctx:player()) end },

  { set = "hajimari.okika", num = 8, name = "光辉收束", kind = "normal", type = "action",
    full_power = true,
    on_play = function(ctx)
      ctx:move("dust", "aura", 2, ctx:player(), ctx:player())
      ctx:move("dust", "flare", 1, ctx:player(), ctx:player())
    end },

  { set = "hajimari.okika", num = 9, name = "精灵联动", kind = "normal", type = "enhance",
    nagi = 3, terminal = true,
    continuous = {
      { when = "expanded", query = "attack",
        apply = function(ctx, atk)
          if atk:attacker() == ctx:player() then atk:add { aura = 1 } end
        end },
    } },

  { set = "hajimari.okika", num = 10, name = "散花之景", kind = "special", type = "action",
    cost = 4,
    on_play = function(ctx) ctx:move("aura", "distance", 2, ctx:opp(), ctx:opp()) end },

  { set = "hajimari.okika", num = 11, name = "精灵之风", kind = "special", type = "action",
    cost = 3, response = true,
    on_play = function(ctx)
      local a = ctx:responding_attack()
      if a and a:from_normal() then a:negate() end
      ctx:draw(ctx:player(), 1)
    end },

  { set = "hajimari.okika", num = 12, name = "绚丽之舞", kind = "special", type = "attack",
    cost = 2,
    attack = { range = {3, 3}, damage = { aura = 2, life = 2 }, keywords = { "lock" } },
    reset = { kind = "immediate", at_least = 2 } },

  { set = "hajimari.okika", num = 13, name = "辉光之刃", kind = "special", type = "attack",
    cost = 5,
    attack = { range = {3, 4}, damage = { aura = 4, life = 3 } } },

}
