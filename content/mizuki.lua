-- 18-Mizuki 山城水津城
-- 【象征武器】兜（O）
-- 【机制】
--   动员～你拥有一些士兵背面向上置于“兵舍”区域。当你动员时，选取一张士兵牌翻到正面向上，
--         这张士兵视为你的手牌可以打出。打出的士兵翻面，进入未动员状态。
--   阵地～若本回合内，距离没有改变过，你的一些牌会获得强化。（所有者：菰珠Kodama）
--
-- 引擎接口（本柱新增）：
--   ctx:position(p)            阵地：本回合有效距离未变
--   ctx:mobilize(p)            动员 1 张（返回被翻开的实例，无可动员则 -1）
--   ctx:is_soldier(inst)       该牌是否为士兵
--   ctx:soldier_mobilized(i)   该士兵是否已动员
--   ctx:barracks(p) / barracks_count / mobilized_count
--   ctx:gain_soldier(name)     追加牌以已动员态加入兵舍
--   ctx:hand_to_barracks(inst) 手牌以已动员态加入兵舍（此后视为士兵）
--   ctx:responded_last_turn(p) / first_response() / responses_played(p)
--   ctx:attack_cards_played(p) / normal_attacks_this_turn(p)

-- 让“本张正在结算的攻击牌”获得一次性修正（在 finalize 阶段生效）。
local function boost_self(ctx, fn)
  local inst = ctx:source_inst()
  ctx:next_attack_mod {
    match = function(_, atk) return atk:source_inst() == inst end,
    apply = fn,
    this_turn = true,
  }
end

-- 打消被对应的攻击（用于 击落 / 防御 / 天主八龙阁 的共用判定）。
local function responded_is_plain(a)
  return a and not a:from_special() and not a:source_full_power()
end

return {

  ---------------------------------------------------------------------------
  -- 兜 O（常规）
  ---------------------------------------------------------------------------
  { set = "mizuki", form = "O", num = 1, name = "头阵", kind = "normal", type = "attack",
    attack = { range = { 1, 2 }, damage = { aura = 1, life = 1 } },
    on_attack_after = function(ctx) ctx:mobilize(ctx:player()) end },

  -- 【2-3 1/1】若上回合你进行过对应，则获得+2/+1。全开：此攻击获得+1/+1。
  { set = "mizuki", form = "O", num = 2, name = "反攻", kind = "normal", type = "attack",
    attack = { range = { 2, 3 }, damage = { aura = 1, life = 1 } },
    on_play = function(ctx)
      if ctx:responded_last_turn(ctx:player()) then
        boost_self(ctx, function(_, a) a:add { aura = 2, life = 1 } end)
      end
      if ctx:zenkai() then
        boost_self(ctx, function(_, a) a:add { aura = 1, life = 1 } end)
      end
    end },

  -- 对应【1-5 1/1】攻击后：阵地～打消被对应的非全力且非切牌的攻击。
  { set = "mizuki", form = "O", num = 3, name = "击落", kind = "normal", type = "attack",
    response = true,
    attack = { range = { 1, 5 }, damage = { aura = 1, life = 1 } },
    on_attack_after = function(ctx)
      if not ctx:position(ctx:player()) then return end
      local a = ctx:responding_attack()
      if not responded_is_plain(a) then return end
      a:negate()
    end },

  { set = "mizuki", form = "O", num = 4, name = "号令", kind = "normal", type = "action",
    on_play = function(ctx)
      local me = ctx:player()
      ctx:mobilize(me)
      if ctx:responded_last_turn(me) then ctx:gain_vigor(me, 1) end
    end },

  { set = "mizuki", form = "O", num = 5, name = "防御", kind = "normal", type = "action",
    response = true, terminal = true,
    on_play = function(ctx)
      if not ctx:first_response() then return end
      local a = ctx:responding_attack()
      if not responded_is_plain(a) then return end
      a:negate()
    end },

  { set = "mizuki", form = "O", num = 6, name = "压阵", kind = "normal", type = "action",
    full_power = true,
    on_play = function(ctx)
      local me = ctx:player()
      for _ = 1, 3 do
        local opts, acts = {}, {}
        opts[#opts + 1], acts[#acts + 1] = "动员", "mobilize"
        local basics = ctx:legal_basics(me)
        for _, b in ipairs(basics) do
          if b == "advance" then
            opts[#opts + 1], acts[#acts + 1] = "前进", "advance"
          elseif b == "aura" then
            opts[#opts + 1], acts[#acts + 1] = "装附", "aura"
          end
        end
        local pick = ctx:choose("压阵：选择一项（可重复）", opts)
        local k = acts[pick]
        if k == "mobilize" then
          ctx:mobilize(me)
        elseif k == "advance" then
          ctx:do_basic(me, "advance")
        elseif k == "aura" then
          ctx:do_basic(me, "aura")
        end
      end
    end },

  -- 【纳3】展开中：阵地～你回合内的第一张非切牌攻击获得+1/+1。
  { set = "mizuki", form = "O", num = 7, name = "战场", kind = "normal", type = "enhance",
    nagi = 3,
    continuous = { { when = "expanded", query = "attack",
      apply = function(ctx, atk)
        if atk:attacker() ~= ctx:player() then return end
        if not ctx:is_my_turn() then return end
        if not ctx:position(ctx:player()) then return end
        if not atk:from_normal() then return end
        if ctx:normal_attacks_this_turn(ctx:player()) ~= 0 then return end
        atk:add { aura = 1, life = 1 }
      end } } },

  ---------------------------------------------------------------------------
  -- 兜 O（切札）
  ---------------------------------------------------------------------------
  -- 对应【纳3】终端。展开时：打消被对应的攻击。展开中：你的士兵和其他女神的攻击 +0/+1。
  { set = "mizuki", form = "O", num = 1, name = "天主八龙阁", kind = "special", type = "enhance",
    cost = 5, nagi = 3, response = true, terminal = true,
    on_expand = function(ctx)
      local a = ctx:responding_attack()
      if a then a:negate() end
    end,
    continuous = { { when = "expanded", query = "attack",
      apply = function(ctx, atk)
        if atk:attacker() ~= ctx:player() then return end
        local si = atk:source_inst()
        if si < 0 then return end
        local soldier = ctx:is_soldier(si)
        local other_goddess = not ctx:card_is_goddess(si, "mizuki")
        if soldier or other_goddess then atk:add { life = 1 } end
      end } } },

  -- 【3-4 3/1】这张牌必须是你在本回合内打出的第一张攻击牌。即再起：你打出具有终端的牌。
  { set = "mizuki", form = "O", num = 2, name = "三重膝丸橹", kind = "special", type = "attack",
    cost = 2,
    attack = { range = { 3, 4 }, damage = { aura = 3, life = 1 } },
    playable = function(ctx) return ctx:attack_cards_played(ctx:player()) == 0 end,
    triggers = {
      { event = "terminal_card_used",
        cond = function(ctx, ev) return ev:subject() == ctx:player() end,
        run = function(ctx, ev) ctx:reset_special(ctx:source_inst()) end },
    } },

  -- 终端。选择至多一张手牌，将这张手牌与特殊牌“斗神”以已动员状态加入兵舍。
  -- 使用后：你的士兵的攻击获得+1/+0。
  { set = "mizuki", form = "O", num = 3, name = "大手盾无门", kind = "special", type = "action",
    cost = 3, terminal = true,
    on_play = function(ctx)
      local me = ctx:player()
      local hand = ctx:hand(me)
      if #hand > 0 then
        local pick = ctx:choose_cards("大手盾无门：选择至多一张手牌加入兵舍", hand, 0, 1)
        if pick and #pick > 0 then ctx:hand_to_barracks(pick[1]) end
      end
      ctx:gain_soldier("斗神")
    end,
    continuous = { { when = "used", query = "attack",
      apply = function(ctx, atk)
        if atk:attacker() ~= ctx:player() then return end
        local si = atk:source_inst()
        if si >= 0 and ctx:is_soldier(si) then atk:add { aura = 1 } end
      end } } },

  -- 全力【纳5】展开中：你原本具有终端的牌失去终端；你的全力牌失去“全力”，并获得词条终端。
  { set = "mizuki", form = "O", num = 4, name = "山城水津城的冲锋号", kind = "special",
    type = "enhance", cost = 5, nagi = 5, full_power = true, terminal_rewrite = true },

  ---------------------------------------------------------------------------
  -- 士兵（兵舍，不进构筑池）
  ---------------------------------------------------------------------------
  { set = "mizuki", form = "O", num = 101, name = "枪兵", kind = "normal", type = "attack",
    soldier = true, copies = 2, terminal = true,
    attack = { range = { 3, 3 }, damage = { aura = 1, life = 1 } },
    on_play = function(ctx)
      if ctx:responded_last_turn(ctx:player()) then
        boost_self(ctx, function(_, a) a:add { aura = 1 } end)
      end
    end },

  { set = "mizuki", form = "O", num = 102, name = "盾兵", kind = "normal", type = "action",
    soldier = true, response = true, terminal = true,
    on_play = function(ctx)
      local a = ctx:responding_attack()
      if not a then return end
      if a:source_full_power() then return end
      a:add { aura = -1 }
    end },

  -- 【纳2】终端。展开中：对手的攻击失去“不可被对应”。弃置时：你获得1集中力。
  { set = "mizuki", form = "O", num = 103, name = "骑兵", kind = "normal", type = "enhance",
    soldier = true, nagi = 2, terminal = true,
    on_discard = function(ctx) ctx:gain_vigor(ctx:player(), 1) end,
    continuous = { { when = "expanded", query = "attack",
      apply = function(ctx, atk)
        if atk:attacker() ~= ctx:player() then atk:remove_unrespondable() end
      end } } },

  ---------------------------------------------------------------------------
  -- 追加牌（EX）：由 大手盾无门 加入兵舍
  ---------------------------------------------------------------------------
  -- 【1-2 2/1】阵地：此牌获得+0/+1。攻击后移除这张牌。
  { set = "mizuki", form = "O", num = 901, name = "斗神", kind = "normal", type = "attack",
    extra = true,
    attack = { range = { 1, 2 }, damage = { aura = 2, life = 1 } },
    on_play = function(ctx)
      if ctx:position(ctx:player()) then
        boost_self(ctx, function(_, a) a:add { life = 1 } end)
      end
    end,
    on_attack_after = function(ctx) ctx:remove_card(ctx:source_inst()) end },

}
