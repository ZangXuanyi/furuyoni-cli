-- 17-Hastumi 初海
-- 【象征武器】桨（O），信任（A1）
-- 【机制】
--   航海～若上一回合内对手没有进行过攻击，则本回合你顺风，否则逆风；第一回合固定顺风。
--   潜水～秘密选择“前进”“后退”。对手使用牌时（在该牌结算之前）或你的回合开始时，
--     公开潜水状态、执行效果并解除。若由攻击牌触发解除且该攻击落空（无视“锁定”词条），
--     则该攻击视为未发生过（不计数、不结算），你的下回合固定顺风。
--     前进：本回合内距离 -1、达人距离 -1；后退：+1 / +1。
--     已处于潜水状态时再次潜水 = 什么都不做（引擎保证）。

-- 潜水：秘密选择前进 / 后退。（对手无法看到这个选择）
local function dive(ctx)
  if ctx:dive_state(ctx:player()) ~= 0 then return end
  local pick = ctx:choose("潜水：选择前进或后退", { "潜水前进", "潜水后退" })
  ctx:dive(pick == 1 and 1 or 2)
end

-- 弄潮：从盖牌区选择至多 1 张置于牌库顶。
local function cover_to_deck_top(ctx)
  local me = ctx:player()
  local pile = ctx:cover_pile(me)
  if #pile == 0 then return end
  local sel = ctx:choose_cards("弄潮：从盖牌区选 1 张置于牌库顶", pile, 0, 1)
  for _, inst in ipairs(sel) do ctx:to_deck_top(inst) end
end

return {

  ---------------------------------------------------------------------------
  -- 桨 O
  ---------------------------------------------------------------------------
  -- 【3-5 0/0】顺风：+2/+2。逆风：攻击后 2距到虚或2虚到距。
  { set = "hatsumi", form = "O", num = 1, name = "水球", kind = "normal", type = "attack",
    attack = { range = { 3, 5 }, damage = { aura = 0, life = 0 } },
    on_play = function(ctx)
      if ctx:tailwind(ctx:player()) then
        ctx:next_attack_mod { apply = function(c2, atk) atk:add { aura = 2, life = 2 } end }
      end
    end,
    on_attack_after = function(ctx)
      if ctx:tailwind(ctx:player()) then return end
      if ctx:choose("水球（逆风）：2距到虚，或2虚到距？", { "2距到虚", "2虚到距" }) == 1 then
        ctx:move("distance", "dust", 2)
      else
        ctx:move("dust", "distance", 2)
      end
    end },

  -- 【4-5 2/1】顺风：+1/+1。全开：不可对，且因伤害移动的结晶改为移动到距。
  { set = "hatsumi", form = "O", num = 2, name = "洋流", kind = "normal", type = "attack",
    zenkai = true,
    attack = { range = { 4, 5 }, damage = { aura = 2, life = 1 } },
    on_play = function(ctx)
      if ctx:tailwind(ctx:player()) then
        ctx:next_attack_mod { apply = function(c2, atk) atk:add { aura = 1, life = 1 } end }
      end
      if ctx:zenkai() then
        ctx:next_attack_mod { apply = function(c2, atk)
          atk:keyword("unrespondable")
          atk:keyword("to_distance")
        end }
      end
    end },

  -- 【5-6 3/1】逆风：若对手因该攻击受到命伤，则本应进入敌气的结晶改为进入虚。
  { set = "hatsumi", form = "O", num = 3, name = "强酸", kind = "normal", type = "attack",
    attack = { range = { 5, 6 }, damage = { aura = 3, life = 1 } },
    on_play = function(ctx)
      if not ctx:tailwind(ctx:player()) then
        ctx:next_attack_mod { apply = function(c2, atk) atk:life_to_dust() end }
      end
    end },

  -- 对应。若当前距离 <= 4：1虚到距；逆风：改为 1敌气到距。
  { set = "hatsumi", form = "O", num = 4, name = "海啸", kind = "normal", type = "action",
    response = true,
    on_play = function(ctx)
      if ctx:distance() > 4 then return end
      if ctx:tailwind(ctx:player()) then
        ctx:move("dust", "distance", 1)
      else
        ctx:move("flare", "distance", 1, ctx:opp())
      end
    end },

  -- 全力。3虚到自装，你可以抽一张牌。逆风：本回合你的手牌上限 +1。
  { set = "hatsumi", form = "O", num = 5, name = "准备万全", kind = "normal", type = "action",
    full_power = true,
    on_play = function(ctx)
      local me = ctx:player()
      ctx:move("dust", "aura", 3, me, me)
      if ctx:choose("准备万全：抽一张牌？", { "抽牌", "不抽" }) == 1 then
        ctx:draw(me, 1)
      end
      if not ctx:tailwind(me) then ctx:add_hand_limit(me, 1) end
    end },

  -- 【纳3】你的攻击额外获得“攻击距离5”，对手的攻击失去“攻击距离5”（多个相互抵消）。
  -- 弃置时：1虚到自装。
  { set = "hatsumi", form = "O", num = 6, name = "罗盘", kind = "normal", type = "enhance",
    nagi = 3, compass = true,
    on_discard = function(ctx)
      ctx:move("dust", "aura", 1, ctx:player(), ctx:player())
    end },

  -- 【纳1】仅自己回合且顺风时才能移除本牌上的结晶。
  -- 展开中&弃置时：准备阶段开始 / 弃置时，可从盖牌区选 1 张置于牌库顶。
  -- 弃置时：至多 1 次基本动作，并进行攻击【2-7 1/-】。
  { set = "hatsumi", form = "O", num = 7, name = "弄潮", kind = "normal", type = "enhance",
    nagi = 1, keep_crystals_unless_tailwind = true,
    triggers = {
      { event = "turn_start",
        cond = function(ctx, ev) return ev:subject() == ctx:player() end,
        run = function(ctx, ev) cover_to_deck_top(ctx) end },
    },
    on_discard = function(ctx)
      cover_to_deck_top(ctx)
      ctx:free_basics(ctx:player(), 1)
      ctx:attack { range = { 2, 7 }, damage = { aura = 1 } }
    end },

  ---------------------------------------------------------------------------
  -- 切
  ---------------------------------------------------------------------------
  -- 【3-5 2/1】顺风：+1/+2。攻击后：若逆风，2虚到距，然后将此牌设为未使用状态。
  { set = "hatsumi", form = "O", num = 1, name = "鲸鱼海域", kind = "special", type = "attack",
    cost = 3,
    attack = { range = { 3, 5 }, damage = { aura = 2, life = 1 } },
    on_play = function(ctx)
      if ctx:tailwind(ctx:player()) then
        ctx:next_attack_mod { apply = function(c2, atk) atk:add { aura = 1, life = 2 } end }
      end
    end,
    on_attack_after = function(ctx)
      if ctx:tailwind(ctx:player()) then return end
      ctx:move("dust", "distance", 2)
      ctx:reset_special(ctx:source_inst())
    end },

  -- 【5-6 2/2】每当这张牌变为未使用状态时，你可以执行一次基本动作。
  -- 即再起：对手的回合内，距离减小了 2 或以上。
  { set = "hatsumi", form = "O", num = 2, name = "鱼雷炮击", kind = "special", type = "attack",
    cost = 2,
    attack = { range = { 5, 6 }, damage = { aura = 2, life = 2 } },
    triggers = {
      { event = "distance_changed",
        cond = function(ctx, ev)
          return not ctx:is_my_turn() and ctx:distance() <= ctx:distance_at_turn_start() - 2
        end,
        run = function(ctx, ev)
          ctx:reset_special(ctx:source_inst())
          if ctx:choose("鱼雷炮击：执行一次基本动作？", { "执行", "不执行" }) == 1 then
            ctx:free_basics(ctx:player(), 1)
          end
        end },
    } },

  -- 顺风：你畏缩。使用后：达人距离 +1。
  -- 使用后：对手的回合内，对手从手牌使用非攻击牌时，改为弃置这张牌（不结算效果，
  -- 视作对手使用了这张牌），然后将其设为未使用状态。
  { set = "hatsumi", form = "O", num = 3, name = "子午灯塔", kind = "special", type = "action",
    cost = 1, near_distance_mod = 1, intercept_non_attack = true,
    on_play = function(ctx)
      if ctx:tailwind(ctx:player()) then ctx:cower(ctx:player()) end
    end },

  -- 逆风：对手畏缩，你展示对手牌库顶一张，若是攻击则弃置之。
  -- 使用后：你的回合开始时，若你逆风，则可以免费使用这张牌。
  { set = "hatsumi", form = "O", num = 4, name = "引水航道", kind = "special", type = "action",
    cost = 2,
    on_play = function(ctx)
      local me = ctx:player()
      if not ctx:tailwind(me) then
        ctx:cower(ctx:opp())
        local top = ctx:deck_top(ctx:opp())
        if top >= 0 and ctx:is_attack(top) then ctx:discard_card(top) end
      end
    end,
    triggers = {
      { event = "turn_start",
        cond = function(ctx, ev)
          return ev:subject() == ctx:player() and not ctx:tailwind(ctx:player())
        end,
        run = function(ctx, ev)
          if ctx:choose("引水航道：免费使用？", { "使用", "不使用" }) == 1 then
            ctx:reuse_special(ctx:source_inst())
          end
        end },
    } },

  ---------------------------------------------------------------------------
  -- 信 A1
  ---------------------------------------------------------------------------
  -- 【纳2】破绽。展开时：潜水。弃置时：攻击【1-7 -/1】不可对。
  { set = "hatsumi.A1", form = "A1", num = 4, name = "水雷", kind = "normal", type = "enhance",
    nagi = 2, breakable = true,
    on_expand = function(ctx) dive(ctx) end,
    on_discard = function(ctx)
      ctx:attack { range = { 1, 7 }, damage = { life = 1 }, keywords = { "unrespondable" } }
    end },

  -- 对应【纳4】。展开中：对手的回合内，若对手的攻击距离包含至少三个自然数，
  -- 则该攻击失去除最大值和最小值以外的所有攻击距离（3-5 → 3,5）。
  { set = "hatsumi.A1", form = "A1", num = 1, name = "暗礁海域", kind = "special",
    type = "enhance", cost = 3, nagi = 4, response = true,
    continuous = {
      { when = "expanded", query = "attack",
        apply = function(ctx, atk)
          if atk:attacker() == ctx:opp() and not ctx:is_my_turn() then atk:keep_extremes() end
        end },
    } },

  -- 【纳2】展开时：潜水，对手畏缩。弃置时：若顺风，对敌装敌命各造成 1 伤害。
  -- 即再起：你逆风（你的回合开始时）。
  { set = "hatsumi.A1", form = "A1", num = 4, name = "汪洋航道", kind = "special",
    type = "enhance", cost = 2, nagi = 2,
    on_expand = function(ctx)
      dive(ctx)
      ctx:cower(ctx:opp())
    end,
    on_discard = function(ctx)
      if ctx:tailwind(ctx:player()) then
        ctx:deal_damage(ctx:opp(), 1, 1, { "both_sides" })
      end
    end,
    triggers = {
      { event = "turn_start",
        cond = function(ctx, ev)
          return ev:subject() == ctx:player() and not ctx:tailwind(ctx:player())
        end,
        run = function(ctx, ev) ctx:reset_special(ctx:source_inst()) end },
    } },

}
