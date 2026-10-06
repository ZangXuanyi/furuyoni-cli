-- 12-Raira 雷螺
-- 机制：风雷（风神/雷神槽 0..20；使用非雷螺牌时任一槽 +1）；岚之力（扣槽换效果，风X 扣 X 风）。
-- O=爪；A1=岚。

-- 使用一次岚之力。forbidSame 时 used 记录已选项，禁止重复。
local function use_raira(ctx, forbidSame, used)
  local me = ctx:player()
  local opts = {}
  for t = 1, 3 do
    if ctx:raira_can("wind", t) and not (forbidSame and used["wind" .. t]) then
      opts[#opts + 1] = { kind = "wind", tier = t, label = "风" .. t }
    end
  end
  for t = 1, 3 do
    if ctx:raira_can("thunder", t) and not (forbidSame and used["thunder" .. t]) then
      opts[#opts + 1] = { kind = "thunder", tier = t, label = "雷" .. t }
    end
  end
  if #opts == 0 then return false end
  local labels = {}
  for _, o in ipairs(opts) do labels[#labels + 1] = o.label end
  labels[#labels + 1] = "不使用"
  local pick = ctx:choose("岚之力", labels)
  if pick > #opts then return false end  -- 可以选择不使用岚之力
  local o = opts[pick]
  if not ctx:raira_spend(o.kind, o.tier) then return false end
  if forbidSame then used[o.kind .. o.tier] = true end
  if o.kind == "wind" then
    if o.tier == 1 then
      if ctx:choose("风1", { "1虚到距", "1距到虚" }) == 1 then
        ctx:move("dust", "distance", 1)
      else
        ctx:move("distance", "dust", 1)
      end
    elseif o.tier == 2 then
      ctx:draw(me, 1)
      local hand = ctx:hand(me)
      if #hand > 0 then
        local sel = ctx:choose_cards("风2：盖伏1张", hand, 1, 1)
        for _, i in ipairs(sel) do ctx:cover_card(i) end
      end
    else
      ctx:gain_vigor(me, 1)
      ctx:set_vigor(ctx:opp(), math.max(0, ctx:vigor(ctx:opp()) - 1))
    end
  else
    if o.tier == 1 then
      ctx:next_attack_mod { apply = function(c2, a) a:add { aura = 1 } end }
    elseif o.tier == 2 then
      ctx:attack { range = {0, 4}, damage = { aura = 1, life = 1 } }
    else
      ctx:next_attack_mod {
        match = function(c2, a) return a:aura_damage() ~= nil end,
        apply = function(c2, a) a:add { life = 1 } end,
      }
    end
  end
  return true
end

return {

  ---------------------------------------------------------------------------
  -- 本格 O 爪
  ---------------------------------------------------------------------------
  { set = "raira", form = "O", num = 1, name = "兽爪", kind = "normal", type = "attack",
    attack = { range = {1, 2}, damage = { aura = 3, life = 1 } } },

  { set = "raira", form = "O", num = 2, name = "风雷击", kind = "normal", type = "attack",
    attack = function(ctx)
      local me = ctx:player()
      local x = math.min(ctx:wind(me), ctx:thunder(me))
      return { range = {1, 2}, damage = { aura = x, life = 2 } }
    end },

  { set = "raira", form = "O", num = 3, name = "流转爪", kind = "normal", type = "attack",
    attack = { range = {1, 2}, damage = { aura = 2, life = 1 } },
    on_attack_after = function(ctx)
      local pool = {}
      for _, i in ipairs(ctx:discard_pile(ctx:player())) do
        if ctx:is_attack(i) then pool[#pool + 1] = i end
      end
      if #pool == 0 then return end
      local sel = ctx:choose_cards("流转爪：选一张攻击牌置于牌库顶（可不选）", pool, 0, 1)
      for _, i in ipairs(sel) do ctx:to_deck_top(i) end
    end },

  { set = "raira", form = "O", num = 4, name = "疾风步", kind = "normal", type = "action",
    on_play = function(ctx)
      if ctx:distance() >= 3 then ctx:move("distance", "dust", 2) end
    end },

  { set = "raira", form = "O", num = 5, name = "风雷的智慧", kind = "normal", type = "action",
    on_play = function(ctx)
      local me = ctx:player()
      if ctx:wind(me) + ctx:thunder(me) < 4 then return end
      local pool = {}
      for _, i in ipairs(ctx:discard_pile(me)) do
        if not ctx:card_is_goddess(i, "raira") then pool[#pool + 1] = i end
      end
      if #pool == 0 then return end
      local sel = ctx:choose_cards("风雷的智慧：选一张非雷螺牌置于牌库顶（可不选）", pool, 0, 1)
      for _, i in ipairs(sel) do ctx:to_deck_top(i) end
    end },

  { set = "raira", form = "O", num = 6, name = "吼叫", kind = "normal", type = "action",
    full_power = true,
    on_play = function(ctx)
      local me = ctx:player()
      if ctx:choose("吼叫", { "对手畏缩，风+1、雷+1", "雷神翻倍" }) == 1 then
        ctx:cower(ctx:opp())
        ctx:raira_gain(me, 0)
        ctx:raira_gain(me, 1)
      else
        local t = ctx:thunder(me) * 2
        if t > 20 then t = 20 end
        ctx:set_slot(me, "thunder", t)
      end
      if ctx:wind(me) >= 7 or ctx:thunder(me) >= 7 then
        ctx:deal_damage(ctx:opp(), 1, nil)
        ctx:free_basics(me, 2)
      end
    end },

  { set = "raira", form = "O", num = 7, name = "驭空", kind = "normal", type = "action",
    full_power = true,
    on_play = function(ctx)
      if ctx:choose("驭空", { "3距到虚", "3虚到距" }) == 1 then
        ctx:move("distance", "dust", 3)
      else
        ctx:move("dust", "distance", 3)
      end
    end },

  { set = "raira", form = "O", num = 1, name = "雷螺风神爪", kind = "special", type = "attack",
    cost = 3,
    attack = function(ctx)
      local a = 2
      if ctx:thunder(ctx:player()) >= 4 then a = 3 end
      return { range = {1, 2}, damage = { aura = a, life = 2 } }
    end,
    reset = { kind = "end_turn", cond = function(ctx) return ctx:wind(ctx:player()) >= 4 end } },

  { set = "raira", form = "O", num = 2, name = "天雷召唤阵", kind = "special", type = "action",
    cost = 6, full_power = true,
    on_play = function(ctx)
      local x = math.ceil(ctx:thunder(ctx:player()) / 2)
      for _ = 1, x do ctx:attack { range = {0, 10}, damage = { aura = 1, life = 1 } } end
    end },

  { set = "raira", form = "O", num = 3, name = "风魔招来孔", kind = "special", type = "action",
    cost = 2,
    on_play = function(ctx)
      local w = ctx:wind(ctx:player())
      if w >= 3 then local e = ctx:gain_extra("旋风"); if e >= 0 then ctx:reset_special(e) end end
      if w >= 7 then ctx:gain_extra("缠回") end
      if w >= 12 then ctx:gain_extra("天狗道") end
      ctx:remove_card(ctx:source_inst())
    end },

  { set = "raira", form = "O", num = 4, name = "圆环轮回旋", kind = "special", type = "enhance",
    nagi = 3, response = true,
    triggers = {
      { event = "attack_resolved",
        cond = function(ctx, ev) return ev:subject() == ctx:opp() end,
        run = function(ctx, ev)
          if ctx:choose("圆环轮回旋", { "1距到虚", "1虚到距" }) == 1 then
            ctx:move("distance", "dust", 1)
          else
            ctx:move("dust", "distance", 1)
          end
          if ctx:choose("圆环轮回旋：风或雷 +1", { "风+1", "雷+1" }) == 1 then
            ctx:raira_gain(ctx:player(), "wind")
          else
            ctx:raira_gain(ctx:player(), "thunder")
          end
        end },
    } },

  { set = "raira", form = "O", num = 901, name = "旋风", kind = "special", type = "attack",
    cost = 1, extra = true,
    attack = { range = {1, 3}, damage = { aura = 1, life = 2 } } },

  { set = "raira", form = "O", num = 902, name = "缠回", kind = "special", type = "action",
    cost = 1, extra = true,
    on_play = function(ctx)
      local pool = {}
      for _, i in ipairs(ctx:special_cards(ctx:player())) do
        if ctx:is_used(i) then pool[#pool + 1] = i end
      end
      if #pool > 0 then
        local sel = ctx:choose_cards("缠回：选择一张切牌设为未使用", pool, 1, 1)
        for _, i in ipairs(sel) do ctx:reset_special(i) end
      end
      ctx:raira_perm_cut(ctx:player())
    end },

  { set = "raira", form = "O", num = 903, name = "天狗道", kind = "special", type = "action",
    cost = 4, extra = true, response = true,
    on_play = function(ctx)
      local n = ctx:choose_options("天狗道：移动多少距到虚？",
        { "5", "4", "3", "2", "1", "0" }, 1, 1)[1]
      local amounts = { 5, 4, 3, 2, 1, 0 }
      ctx:move("distance", "dust", amounts[n + 1])
      ctx:remove_card(ctx:source_inst())
    end },

  ---------------------------------------------------------------------------
  -- 变格 A1 岚
  ---------------------------------------------------------------------------
  { set = "raira.A1", form = "A1", num = 2, name = "暴风", kind = "normal", type = "attack",
    attack = { range = {2, 3}, damage = { aura = 2, life = 1 } },
    on_attack_after = function(ctx) use_raira(ctx, false, {}) end },

  { set = "raira.A1", form = "A1", num = 6, name = "大岚", kind = "normal", type = "enhance",
    nagi = 0, full_power = true,
    on_enter = function(ctx)
      for _ = 1, 3 do
        local c = ctx:choose("大岚：选择3次", { "风+1", "雷+1", "1虚到本牌" })
        if c == 1 then
          ctx:raira_gain(ctx:player(), "wind")
        elseif c == 2 then
          ctx:raira_gain(ctx:player(), "thunder")
        else
          ctx:dust_to_card(ctx:source_inst(), 1)
        end
      end
    end,
    triggers = {
      { event = "end_phase_start",
        cond = function(ctx, ev) return ev:subject() == ctx:player() end,
        run = function(ctx, ev)
          ctx:attack { range = {0, 4}, damage = { aura = 1, life = 1 }, keywords = { "unrespondable" } }
        end },
      { event = "attack_declared",
        cond = function(ctx, ev)
          return ev:subject() == ctx:opp() and ctx:attacks_this_turn(ctx:opp()) == 1
        end,
        run = function(ctx, ev)
          local a = ev:attack()
          if a then a:add { life = -1 } end
        end },
    } },

  { set = "raira.A1", form = "A1", num = 3, name = "阵风祭天式", kind = "special", type = "action",
    cost = 2,
    on_play = function(ctx)
      local me = ctx:player()
      ctx:raira_gain(me, 0)
      ctx:raira_gain(me, 1)
      ctx:cower(ctx:opp())
      ctx:raira_restrict(me)
    end,
    triggers = {
      { event = "main_start",
        cond = function(ctx, ev) return ev:subject() == ctx:player() end,
        run = function(ctx, ev)
          local used = {}
          for _ = 1, 2 do
            if not use_raira(ctx, true, used) then break end
          end
        end },
    } },

}
