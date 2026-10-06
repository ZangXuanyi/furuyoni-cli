-- 11-Thallya 茱莉亚·萨利亚
-- 机制：蒸汽引擎（开局引擎模块5个）。燃烧X=引擎→用尽；恢复X=用尽→引擎；气动=可选 距+1/-1；
-- 你的回合开始时，把场上所有不在引擎模块的蒸汽移到用尽。变形：同时只保留一个变形光环。
-- O=骑；A1=新型。

return {

  ---------------------------------------------------------------------------
  -- 本格 O 骑
  ---------------------------------------------------------------------------
  { set = "thallya", form = "O", num = 1, name = "蒸汽燃烧", kind = "normal", type = "attack",
    attack = { range = {3, 5}, damage = { aura = 2, life = 1 } },
    on_attack_after = function(ctx) ctx:pneumatic(ctx:player()) end },

  { set = "thallya", form = "O", num = 2, name = "震荡波", kind = "normal", type = "attack",
    burn_require = 1,
    attack = { range = {1, 3}, damage = { aura = 3, life = 1 } },
    on_play = function(ctx) ctx:burn(ctx:player(), 1) end,
    on_attack_after = function(ctx) ctx:pneumatic(ctx:player()) end },

  { set = "thallya", form = "O", num = 3, name = "倒车", kind = "normal", type = "attack",
    burn_require = 1,
    attack = { range = {1, 1}, damage = { aura = 3, life = 2 }, keywords = { "to_distance" } },
    on_play = function(ctx) ctx:burn(ctx:player(), 1) end },

  { set = "thallya", form = "O", num = 4, name = "蒸汽大炮", kind = "normal", type = "attack",
    burn_require = 1, zenkai = true,
    attack = function(ctx)
      if ctx:zenkai() then return { range = {2, 8}, damage = { aura = 3, life = 3 } } end
      return { range = {3, 7}, damage = { aura = 1, life = 1 } }
    end,
    on_play = function(ctx) ctx:burn(ctx:player(), 1) end,
    on_attack_after = function(ctx)
      if not ctx:zenkai() then ctx:recover(ctx:player(), 2) end
    end },

  { set = "thallya", form = "O", num = 5, name = "特技表演", kind = "normal", type = "action",
    on_play = function(ctx)
      ctx:move("aura", "flare", 2, ctx:player(), ctx:player())
      ctx:cower(ctx:opp())
    end },

  { set = "thallya", form = "O", num = 6, name = "轰鸣", kind = "normal", type = "action",
    on_play = function(ctx)
      local me = ctx:player()
      -- 你可以燃烧2并获得1集中力，对手失去1集中力并畏缩。
      if ctx:can_burn(me, 2) and ctx:choose("轰鸣：燃烧2并获得1集中力？", { "是", "否" }) == 1 then
        ctx:burn(me, 2)
        ctx:gain_vigor(me, 1)
        ctx:set_vigor(ctx:opp(), math.max(0, ctx:vigor(ctx:opp()) - 1))
        ctx:cower(ctx:opp())
      end
      if ctx:vigor(me) >= 2 and ctx:choose("轰鸣：支付2集中力并恢复3？", { "是", "否" }) == 1 then
        ctx:set_vigor(me, ctx:vigor(me) - 2)
        ctx:recover(me, 3)
      end
    end },

  { set = "thallya", form = "O", num = 7, name = "换档", kind = "normal", type = "action",
    response = true, burn_require = 1,
    on_play = function(ctx)
      ctx:burn(ctx:player(), 1)
      ctx:pneumatic(ctx:player())
    end },

  { set = "thallya", form = "O", num = 1, name = "阿尔法之刃", kind = "special", type = "attack",
    cost = 1,
    attack = { range = { { 1, 1 }, { 3, 3 }, { 5, 5 }, { 7, 7 } },
               damage = { aura = 1, life = 1 } },
    reset = { kind = "immediate", on = "pneumatic" } },

  { set = "thallya", form = "O", num = 2, name = "最终燃烧", kind = "special", type = "action",
    cost = 4, response = true,
    on_play = function(ctx)
      local me = ctx:player()
      local x = ctx:steam_exhausted(me)
      ctx:recover(me, x)
      local a = ctx:responding_attack()
      if a then
        local ad = a:aura_damage()
        if ad == nil or ad <= x then a:negate() end
      end
    end },

  { set = "thallya", form = "O", num = 3, name = "萨利亚的杰作", kind = "special", type = "enhance",
    cost = 2, nagi = 3 },

  { set = "thallya", form = "O", num = 4, name = "黑盒", kind = "special", type = "action",
    cost = 2, full_power = true,
    playable = function(ctx) return ctx:steam_engine(ctx:player()) == 0 end,
    on_play = function(ctx)
      local me = ctx:player()
      if ctx:steam_engine(me) == 0 then ctx:transform_choose(me) end
      ctx:recover(me, 2)
    end },

  ---------------------------------------------------------------------------
  -- 变形（追加区）
  ---------------------------------------------------------------------------
  { set = "thallya", form = "O", num = 801, name = "夜叉", kind = "special", type = "action",
    transform = true,
    on_transform = function(ctx)
      ctx:cower(ctx:opp())
      ctx:set_next_draw_one(ctx:player())  -- 下个回合开始时只抽一张牌
    end,
    extra_basic = function(ctx)
      ctx:attack { range = { { 2, 2 }, { 4, 4 }, { 6, 6 }, { 8, 8 } }, damage = { aura = 2, life = 1 },
        after = function(c2, a) c2:pneumatic(c2:player()) end }
    end },

  { set = "thallya", form = "O", num = 802, name = "娜迦", kind = "special", type = "action",
    transform = true,
    on_transform = function(ctx)
      local f = ctx:flare(ctx:opp())
      if f > 2 then
        ctx:set_flare(ctx:opp(), 2)
        ctx:add_crystal("dust", f - 2)
      end
    end,
    extra_basic = function(ctx) ctx:discard_top(ctx:opp()) end },

  { set = "thallya", form = "O", num = 803, name = "迦楼罗", kind = "special", type = "action",
    transform = true,
    on_transform = function(ctx)
      ctx:draw(ctx:player(), 2)
      ctx:add_hand_limit(ctx:player(), 90)  -- 本回合手牌无上限（回合开始重置）
    end,
    extra_basic = function(ctx)
      if ctx:distance() <= 7 then ctx:move("dust", "distance", 1) end
    end },

  ---------------------------------------------------------------------------
  -- 变格 A1 新型
  ---------------------------------------------------------------------------
  { set = "thallya.A1", form = "A1", num = 5, name = "快速改装", kind = "normal", type = "enhance",
    nagi = 3,
    on_enter = function(ctx)
      ctx:recover(ctx:player(), 1)
      local pool = ctx:transform_cards(ctx:player())
      if #pool > 0 then
        local sel = ctx:choose_cards("快速改装：封印一张变形牌", pool, 1, 1)
        for _, i in ipairs(sel) do ctx:seal_card(ctx:source_inst(), i) end
      end
    end,
    on_discard = function(ctx)
      local host = ctx:source_inst()
      local s = ctx:sealed_card(host)
      if s >= 0 then ctx:remove_card(s) end
    end },

  { set = "thallya.A1", form = "A1", num = 1, name = "新型黑盒", kind = "special",
    type = "action", cost = 1, terminal = true,
    on_play = function(ctx)
      local me = ctx:player()
      ctx:recover(me, 1)
      if ctx:steam_exhausted(me) == 0 then ctx:dust_to_card(ctx:source_inst(), 1) end
      if ctx:crystals(ctx:source_inst()) >= 2 then
        ctx:drain_card_crystals(ctx:source_inst(), 2)
        ctx:transform_choose(me)
      end
    end,
    reset = { kind = "end_turn", cond = function(ctx)
      return ctx:steam_engine(ctx:player()) <= 3 or ctx:transform_count(ctx:player()) > 0
    end } },

  { set = "thallya.A1", form = "A1", num = 2, name = "超级燃烧", kind = "special", type = "attack",
    cost = function(ctx) return ctx:transform_count(ctx:player()) end,
    attack = function(ctx)
      local x = ctx:transform_count(ctx:player())
      return { range = {3, 10}, damage = { aura = x, life = x } }
    end },

  { set = "thallya.A1", form = "A1", num = 811, name = "紧那罗", kind = "special", type = "action",
    transform = true,
    on_transform = function(ctx) ctx:cover_deck(ctx:opp()) end,  -- 盖伏对手的牌库
    triggers = {
      { event = "rebuilt",
        cond = function(ctx, ev) return ev:subject() == ctx:opp() end,
        run = function(ctx, ev) ctx:attack { range = { { 2, 2 }, { 4, 4 }, { 6, 6 } }, damage = { aura = 2, life = 2 } } end },
    } },

  { set = "thallya.A1", form = "A1", num = 813, name = "阿修罗", kind = "special", type = "action",
    transform = true,
    on_transform = function(ctx)
      local cov = ctx:cover_cards(ctx:opp())
      if #cov > 0 then
        local sel = ctx:choose_cards_for(ctx:opp(), "阿修罗：选择2张盖牌翻正面", cov, math.min(2, #cov), math.min(2, #cov))
        for _, i in ipairs(sel) do ctx:discard_card(i) end
      end
    end,
    extra_basic = function(ctx)
      ctx:attack { range = { { 3, 3 }, { 5, 5 } }, damage = { aura = 3, life = 2 },
        after = function(c2, a) c2:cower(c2:player()) end }
    end },

  { set = "thallya.A1", form = "A1", num = 814, name = "提婆", kind = "special", type = "action",
    transform = true,
    on_transform = function(ctx)
      ctx:recover(ctx:player(), 2)
      ctx:do_basic(ctx:player(), "aura")
      ctx:do_basic(ctx:player(), "aura")
    end,
    triggers = {
      { event = "discarded",
        cond = function(ctx, ev)
          -- 每当"对手的"弃牌数量变为 0 以外的偶数时（每次弃牌 +1，故偶数即"变为偶数那一刻"）。
          return ev:subject() == ctx:opp() and ctx:discard_size(ctx:opp()) % 2 == 0 and
                 ctx:discard_size(ctx:opp()) > 0
        end,
        run = function(ctx, ev) ctx:gain_vigor(ctx:player(), 1) end },
    } },

}
