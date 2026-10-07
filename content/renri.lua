-- 22-Renri 夜山恋离
-- 【武器】衣（O），遗物（A1）
-- 【机制】
--   伪证：你在自己的回合内使用任意常规牌时，可以背面向上打出并声称它是 5 张
--         伪证牌（构陷/夸口/罗织/恐吓/捧杀）之一。A1 追加 3 张史前遗物。
--         对手不质疑 → 按声称的牌名结算；质疑且牌名一致 → 质疑失败，对手焦躁
--         一次并正常使用该牌；质疑且不一致 → 两者都不结算。
--   回归：对手质疑失败时，可以把这张牌移出游戏并把「考古」置回弃牌堆。
--   铭镌之衣：按「夜山恋离的终幕」上的樱花结晶数视作某张牌的复制。

-- 5 张本格伪证牌名（唆使 / 论理伶俐 用）。
local BLUFF_NAMES = { "构陷", "夸口", "罗织", "恐吓", "捧杀" }

-- 弃牌区与付与区的「通常牌」张数和（夸口 / 恐吓 / 论理伶俐的再起）。
local function normal_total(ctx, p)
  local n = 0
  for _, i in ipairs(ctx:discard_pile(p)) do
    if ctx:is_normal_card(i) then n = n + 1 end
  end
  for _, i in ipairs(ctx:enhances(p)) do
    if ctx:is_normal_card(i) then n = n + 1 end
  end
  return n
end

local function is_bluff_name(n)
  for _, bn in ipairs(BLUFF_NAMES) do
    if n == bn then return true end
  end
  return false
end

-- 铭镌之衣: 按「夜山恋离的终幕」上的樱花结晶数决定复制哪张牌。
local function copy_name(ctx)
  local host = ctx:find_named(ctx:player(), "夜山恋离的终幕")
  local n = 0
  if host >= 0 then n = ctx:crystals(host) end
  if n >= 3 then return "久远之花" end
  if n == 2 then return "完全论破" end
  if n == 1 then return "夙愿" end
  return "御剑桐子的巫女神乐"
end

return {

  ---------------------------------------------------------------------------
  -- 衣 O: 常规
  ---------------------------------------------------------------------------

  -- 【1-2 2/1】伪证: 若你用构陷进行伪证且对手未质疑，可以展示被宣称为构陷的
  -- 牌并置入弃牌堆；若这张牌确实不是构陷，则变为 -/1。
  { set = "renri", form = "O", num = 1, name = "构陷", kind = "normal", type = "attack",
    bluff = true,
    attack = { range = { 1, 2 }, damage = { aura = 2, life = 1 } },
    on_play = function(ctx)
      local me = ctx:player()
      if not ctx:bluff_undoubted() or ctx:bluff_is_real() then return end
      if ctx:choose("构陷：展示被宣称为构陷的牌并置入弃牌堆？（此攻击变为 -/1）",
                    { "展示并置入弃牌堆", "不展示" }) ~= 1 then return end
      local used = ctx:bluff_inst()
      if used >= 0 then ctx:discard_card(used) end
      local my = ctx:source_inst()
      ctx:next_attack_mod {
        match = function(c2, atk) return atk:source_inst() == my end,
        apply = function(c2, atk) atk:no_aura_damage() end,
      }
    end },

  -- 【3-5 2/1】若你弃牌区与付与区的通常牌张数和 >= 3，这张牌获得 +1/+0。
  { set = "renri", form = "O", num = 2, name = "夸口", kind = "normal", type = "attack",
    bluff = true,
    attack = { range = { 3, 5 }, damage = { aura = 2, life = 1 } },
    on_play = function(ctx)
      if normal_total(ctx, ctx:player()) < 3 then return end
      local my = ctx:source_inst()
      ctx:next_attack_mod {
        match = function(c2, atk) return atk:source_inst() == my end,
        apply = function(c2, atk) atk:add { aura = 1 } end,
      }
    end },

  -- 对应【1-5 1/2】攻击后: 若本回合内有樱花结晶移出虚，则被对应的攻击的对装
  -- 伤害 >= 3 时获得 -1/+0，否则获得 +0/-1。（对应打出时不能伪证）
  { set = "renri", form = "O", num = 3, name = "罗织", kind = "normal", type = "attack",
    bluff = true, response = true,
    attack = { range = { 1, 5 }, damage = { aura = 1, life = 2 } },
    on_attack_after = function(ctx)
      if not ctx:crystal_left_dust() then return end
      local a = ctx:responding_attack()
      if a == nil then return end
      local au = a:aura_damage()
      if au ~= nil and au >= 3 then
        a:add { aura = -1 }
      else
        a:add { life = -1 }
      end
    end },

  -- 对手畏缩。若对手本回合质疑失败过，则从弃牌堆选一张伪证牌加入手牌。
  { set = "renri", form = "O", num = 4, name = "唆使", kind = "normal", type = "action",
    on_play = function(ctx)
      local me, opp = ctx:player(), ctx:opp()
      ctx:cower(opp)
      if not ctx:doubt_failed(opp) then return end
      local pool = {}
      for _, i in ipairs(ctx:discard_pile(me)) do
        if is_bluff_name(ctx:card_name(i)) then pool[#pool + 1] = i end
      end
      if #pool == 0 then return end
      local sel = ctx:choose_cards("唆使：选择弃牌堆中的一张伪证牌加入手牌", pool, 1, 1)
      for _, i in ipairs(sel) do ctx:to_hand(i) end
    end },

  -- 若你弃牌区与付与区的通常牌张数和 >= 3，则对手盖伏一张牌；若对手无牌可盖，
  -- 则对手畏缩。若你用恐吓伪证且对手质疑失败，对手改为受到 2 次焦躁。
  { set = "renri", form = "O", num = 5, name = "恐吓", kind = "normal", type = "action",
    bluff = true,
    on_play = function(ctx)
      local me, opp = ctx:player(), ctx:opp()
      if ctx:bluff_doubt_failed() then ctx:impatience(opp) end  -- 通用流程已给 1 次
      if normal_total(ctx, me) < 3 then return end
      local pool = {}
      for _, i in ipairs(ctx:hand(opp)) do
        if not ctx:is_poison(i) then pool[#pool + 1] = i end
      end
      if #pool > 0 then
        local sel = ctx:choose_cards_for(opp, "恐吓：选择一张手牌盖伏", pool, 1, 1)
        for _, i in ipairs(sel) do ctx:cover_card(i) end
      else
        ctx:cower(opp)
      end
    end },

  -- 1距到自气。
  { set = "renri", form = "O", num = 6, name = "捧杀", kind = "normal", type = "action",
    bluff = true,
    on_play = function(ctx)
      local me = ctx:player()
      ctx:move("distance", "flare", 1, me, me)
    end },

  -- 【纳3】展开时 & 弃置时: 若距离 >= 2，则 1距到虚。
  { set = "renri", form = "O", num = 7, name = "蛊惑", kind = "normal", type = "enhance",
    nagi = 3,
    on_expand = function(ctx)
      if ctx:distance() >= 2 then ctx:move("distance", "dust", 1) end
    end,
    on_discard = function(ctx)
      if ctx:distance() >= 2 then ctx:move("distance", "dust", 1) end
    end },

  ---------------------------------------------------------------------------
  -- 衣 O: 切札
  ---------------------------------------------------------------------------

  -- 【0-10 1/2】若对手本回合质疑失败过，则此攻击获得两侧伤害。使用后：你的
  -- 回合结束时，对对手造成 1 装伤，并将此牌设为未使用状态。
  { set = "renri", form = "O", num = 1, name = "谰厉淋漓", kind = "special", type = "attack",
    cost = 4,
    attack = { range = { 0, 10 }, damage = { aura = 1, life = 2 } },
    on_play = function(ctx)
      if not ctx:doubt_failed(ctx:opp()) then return end
      local my = ctx:source_inst()
      ctx:next_attack_mod {
        match = function(c2, atk) return atk:source_inst() == my end,
        apply = function(c2, atk) atk:both_sides() end,
      }
    end,
    triggers = {
      { event = "turn_end",
        cond = function(ctx, ev) return ev:subject() == ctx:player() end,
        run = function(ctx, ev)
          local inst = ctx:source_inst()
          ctx:deal_damage(ctx:opp(), 1, nil)
          ctx:reset_special(inst)
        end },
    } },

  -- O-S2 立睖凌厉（4）：对应。仅限对应使用。查看对手手牌，选择一张非恋离的
  -- 非全力牌使用或盖伏之。使用的那张牌也视作对应「凌厉」正对应的攻击。
  { set = "renri", form = "O", num = 2, name = "立睖凌厉", kind = "special", type = "action",
    cost = 4, response = true, response_only = true,
    on_play = function(ctx)
      local me, opp = ctx:player(), ctx:opp()
      local sel = ctx:reveal_cards(me, opp, "hand", "立睖凌厉：检视对手手牌并选择一张", 1, 1,
                                   function(c2, i)
                                     return not c2:card_is_goddess(i, "renri") and
                                            not c2:is_full_power(i)
                                   end)
      if #sel == 0 then return end
      local card = sel[1]
      local opts = {}
      local acts = {}
      if ctx:is_normal_card(card) or ctx:flare(me) >= ctx:cut_cost(card) then
        opts[#opts + 1], acts[#acts + 1] = "使用这张牌（视作对应）", "use"
      end
      opts[#opts + 1], acts[#acts + 1] = "盖伏之", "cover"
      local pick = ctx:choose("立睖凌厉：使用或盖伏？", opts)
      local act = acts[pick]
      if act == "use" then
        ctx:play_hand_card_as_response(card)
      elseif act == "cover" then
        ctx:cover_card(card)
      end
    end },

  -- O-S3 论理伶俐（2）：对应。从构筑时没有选择的伪证牌中选择一张使用，然后
  -- 移出游戏。再起：你弃牌区与付与区的通常牌张数和 >= 3。
  { set = "renri", form = "O", num = 3, name = "论理伶俐", kind = "special", type = "action",
    cost = 2, response = true,
    on_play = function(ctx)
      local me = ctx:player()
      local pool = {}
      for _, i in ipairs(ctx:unchosen_normals(me)) do
        if is_bluff_name(ctx:card_name(i)) then pool[#pool + 1] = i end
      end
      if #pool == 0 then return end
      local sel = ctx:choose_cards("论理伶俐：从未选择的伪证牌中选择一张使用", pool, 1, 1)
      if #sel == 0 then return end
      local card = sel[1]
      local resp = ctx:responding_attack() ~= nil
      ctx:use_card(card, resp)
      ctx:remove_from_game(card)
    end,
    reset = { kind = "end_turn",
              cond = function(ctx) return normal_total(ctx, ctx:player()) >= 3 end } },

  -- O-S4 夜山恋离的终幕（1）：【纳3】终端。展开时：获得「铭镌之衣」。
  -- 仅在你的回合开始时，可以选择移除这张牌上的一个樱花结晶。
  -- 对手不能移动这张牌上的樱花结晶。
  { set = "renri", form = "O", num = 4, name = "夜山恋离的终幕", kind = "special",
    type = "enhance", cost = 1, nagi = 3, terminal = true,
    enemy_crystal_immune = true,
    on_expand = function(ctx) ctx:gain_extra("铭镌之衣") end,
    triggers = {
      { event = "turn_start",
        cond = function(ctx, ev) return ev:subject() == ctx:player() end,
        run = function(ctx, ev)
          local inst = ctx:source_inst()
          if ctx:crystals(inst) <= 0 then return end
          if ctx:choose("夜山恋离的终幕：移除这张牌上的一个樱花结晶？",
                        { "移除1个", "不移除" }) == 1 then
            ctx:move_from_card(inst, "dust", 1)
          end
        end },
    } },

  ---------------------------------------------------------------------------
  -- 遗物 A1: 常规
  ---------------------------------------------------------------------------

  -- A1-N2 洛阳铲: 声称对手构筑阶段能够选择的一张常规非付与牌并使用之。
  -- 若对手从手牌/盖牌堆/弃牌堆展示了这张牌，则使用失败，这张牌被弃置。
  { set = "renri.A1", form = "A1", num = 2, name = "洛阳铲", kind = "normal", type = "action",
    on_declare = function(ctx)
      local names = ctx:opp_build_normals()
      if #names == 0 then return end
      local pick = ctx:choose("洛阳铲：声称对手构筑阶段可选的一张常规非付与牌", names)
      local name = names[pick]
      if name == nil then return end
      if ctx:opp_shows(name) then return end  -- 使用失败：这张牌被弃置
      ctx:resolve_as(name)
    end },

  -- A1-N6 考古: 打出无效果。当你将要重铸牌库时，若此牌位于弃牌区，可以移出
  -- 游戏并将一件史前遗物放到牌库顶。
  { set = "renri.A1", form = "A1", num = 6, name = "考古", kind = "normal", type = "action",
    kaogu_return = true,
    triggers = {
      { event = "before_rebuild", zone = "discard",
        cond = function(ctx, ev) return ev:subject() == ctx:player() end,
        run = function(ctx, ev)
          local me = ctx:player()
          local opts = {}
          for _, n in ipairs({ "谎言的武器", "刀刃的本质", "最初的樱花" }) do
            if ctx:count_named(me, n) == 0 then opts[#opts + 1] = n end
          end
          if #opts == 0 then return end
          if ctx:choose("考古：移出游戏，并将一件史前遗物放到牌库顶？",
                        { "使用", "不使用" }) ~= 1 then return end
          local pick = ctx:choose("考古：选择一件史前遗物", opts)
          local name = opts[pick]
          if name == nil then return end
          ctx:remove_card(ctx:source_inst())
          local e = ctx:gain_extra(name)
          if e >= 0 then ctx:to_deck_top(e) end
        end },
    } },

  ---------------------------------------------------------------------------
  -- 遗物 A1: 切札
  ---------------------------------------------------------------------------

  -- A1-S1 道化的觉悟（0）：【纳3】游戏开始时：将这张牌设为使用后状态。
  -- 使用后：你的回合结束时，可以使用这张牌（至少 1 个结晶来自你的命）。
  -- 展开中：你回合开始时可以多抽一张；对手的焦躁伤害变为 2/1；
  --         伪证未被质疑时可以公开并获得 1 集中力。
  { set = "renri.A1", form = "A1", num = 1, name = "道化的觉悟", kind = "special",
    type = "enhance", cost = 0, nagi = 3,
    start_used = true, enemy_impatience_up = true, nagi_from_life = 1,
    triggers = {
      { event = "turn_start",
        cond = function(ctx, ev)
          return ev:subject() == ctx:player() and ctx:crystals(ctx:source_inst()) > 0
        end,
        run = function(ctx, ev)
          if ctx:choose("道化的觉悟：多抽一张牌？", { "抽一张", "不抽" }) == 1 then
            ctx:draw(ctx:player(), 1)
          end
        end },
      { event = "turn_end",
        cond = function(ctx, ev)
          return ev:subject() == ctx:player() and ctx:life(ctx:player()) >= 1
        end,
        run = function(ctx, ev)
          if ctx:choose("道化的觉悟：使用这张牌？（纳3，至少 1 个来自你的命）",
                        { "使用", "不使用" }) ~= 1 then return end
          ctx:reuse_special(ctx:source_inst())
        end },
      { event = "bluff_undoubted",
        cond = function(ctx, ev)
          return ev:subject() == ctx:player() and ctx:crystals(ctx:source_inst()) > 0
        end,
        run = function(ctx, ev)
          if ctx:bluff_is_real() then return end
          if ctx:choose("道化的觉悟：公开用于伪证的牌并获得 1 集中力？",
                        { "公开并获得1集中力", "不公开" }) == 1 then
            ctx:gain_vigor(ctx:player(), 1)
          end
        end },
    } },

  -- A1-EX-N1 谎言的武器: 【2-4 1/1】回归。若本回合内你使用的第三张牌是攻击，
  -- 这个攻击获得 +0/+1。在你重铸牌库时，可以宣称一个背面向上的卡牌是这张牌，
  -- 如设置一样打出（视作伪证）；结算完毕这张牌必然进入牌山。
  { set = "renri.A1", form = "A1", num = 901, name = "谎言的武器", kind = "normal",
    type = "attack", extra = true, relic = true, regression = true, bluff = true,
    rebuild_claim = true,
    attack = { range = { 2, 4 }, damage = { aura = 1, life = 1 } },
    on_play = function(ctx)
      if ctx:cards_played_this_turn(ctx:player()) ~= 3 then return end
      local my = ctx:source_inst()
      ctx:next_attack_mod {
        match = function(c2, atk) return atk:source_inst() == my end,
        apply = function(c2, atk) atk:add { life = 1 } end,
      }
    end },

  -- A1-EX-N2 刀刃的本质: 【3-4 3/1】回归。若「道化的觉悟」上有樱花结晶，
  -- 该攻击获得 +0/+1。此牌移出游戏时，对手的集中力变为 0。
  { set = "renri.A1", form = "A1", num = 902, name = "刀刃的本质", kind = "normal",
    type = "attack", extra = true, relic = true, regression = true, bluff = true,
    trigger_from_removed = true,
    attack = { range = { 3, 4 }, damage = { aura = 3, life = 1 } },
    on_play = function(ctx)
      local host = ctx:find_named(ctx:player(), "道化的觉悟")
      if host < 0 or ctx:crystals(host) <= 0 then return end
      local my = ctx:source_inst()
      ctx:next_attack_mod {
        match = function(c2, atk) return atk:source_inst() == my end,
        apply = function(c2, atk) atk:add { life = 1 } end,
      }
    end,
    triggers = {
      { event = "removed_from_game",
        cond = function(ctx, ev) return ev:card() == ctx:source_inst() end,
        run = function(ctx, ev) ctx:flare_to(ctx:opp(), 0) end },
    } },

  -- A1-EX-N3 最初的樱花: 回归。执行一次基本动作；若你用这张牌伪证且未被质疑，
  -- 再执行一次基本动作。若虚 <= 5，从弃牌堆选一张牌（这张除外）放到抽牌堆底。
  -- 此牌移出游戏时，1虚到自命。
  { set = "renri.A1", form = "A1", num = 903, name = "最初的樱花", kind = "normal",
    type = "action", extra = true, relic = true, regression = true, bluff = true,
    trigger_from_removed = true,
    on_play = function(ctx)
      local me = ctx:player()
      ctx:free_basics(me, 1)
      if ctx:bluff_undoubted() then ctx:free_basics(me, 1) end
      if ctx:dust() <= 5 then
        local pool = {}
        for _, i in ipairs(ctx:discard_pile(me)) do
          if i ~= ctx:source_inst() then pool[#pool + 1] = i end
        end
        if #pool > 0 then
          local sel = ctx:choose_cards("最初的樱花：选择一张牌放到抽牌堆底", pool, 1, 1)
          for _, i in ipairs(sel) do ctx:to_deck_bottom(i) end
        end
      end
    end,
    triggers = {
      { event = "removed_from_game",
        cond = function(ctx, ev) return ev:card() == ctx:source_inst() end,
        run = function(ctx, ev) ctx:move("dust", "life", 1, ctx:player(), ctx:player()) end },
    } },

  -- O-S4-EX1 铭镌之衣: 按「夜山恋离的终幕」上的樱花结晶数视作某张牌的复制，
  -- 使用复制的时候需要支付费用。视作「夙愿」时继承其「使用后」光环（你不会
  -- 受到任何伤害）与「即再起：你的主要阶段开始时」；身份随结晶数随时变化，
  -- 而这张牌自身的使用后/未使用状态不变。
  { set = "renri.A1", form = "A1", num = 904, name = "铭镌之衣", kind = "special",
    type = "action", extra = true,
    cost = function(ctx)
      local c = ctx:def_cost(copy_name(ctx))
      if c < 0 then return 0 end
      return c
    end,
    on_declare = function(ctx) ctx:resolve_as(copy_name(ctx)) end,
    damage_immune_aura = function(ctx) return copy_name(ctx) == "夙愿" end,
    reset = { kind = "immediate", on = "main_start",
              cond = function(ctx) return copy_name(ctx) == "夙愿" end } },

  -- 22-kiriko-NA-O-S4 御剑桐子的巫女神乐（3）：【2-3 3/2】白板攻击牌
  -- （特殊说明：没有附加效果）。
  { set = "renri", form = "O", num = 930, name = "御剑桐子的巫女神乐", kind = "special",
    type = "attack", cost = 3, extra = true,
    attack = { range = { 2, 3 }, damage = { aura = 3, life = 2 } } },

}
