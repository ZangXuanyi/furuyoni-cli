-- 26-Innealra 伊努露·诺伦 & 玛希露·诺伦 & 阿库露·诺伦
-- 【象征武器】时间之枪。
-- 【机制】共鸣～四个命运槽：过去 / 现在 / 未来 / 待启。开局按规则书写顺序放入
--   四个命运。除游戏的第一个回合外，你的回合开始时、抽牌后，若手牌张数等于 3，
--   进行一次共鸣（执行当前寄宿的枪对应时间点的命运效果，然后轮转命运槽：
--   过去→待启、现在→过去、未来→现在、待启→未来）。对手使用全力牌或对应牌时，
--   你可以轮转一次（不共鸣）。「纠葛」= 万劫缠迫展开中时，每个命运额外的
--   「纠葛：」子句也结算。「行动力」= 集中力 +1；「王牌」= 切牌。
-- 三把枪 = O（枪过去）/ A1（枪现在）/ A2（枪未来）；共有牌用 forms 列出适用形态。
-- 引擎侧支持：ctx:waku / fate_slot / fate_pos / resolve_fate_slot / resonance /
--   rotate_fates / entangle_fates / fates_entangled / resonance_count /
--   used_non_innealra / used_normal_this_turn / fate_resolving_slot /
--   fate_from_turn_start / cost_paid / cost_to_waku / last_attack_responded /
--   set_cannot_use_normals / set_cannot_retreat / set_rebuild_freeze / is_response；
--   CardDef.fate / fate_slot / fate_entangler / suppress_enemy_attack_mods /
--   nagi_from_distance / rebuild_freeze / fragile_will / decay_to="waku"；
--   AF_AuraToDistance ("aura_to_distance") / AF_ToWaku ("to_waku")。

-- 祖枪 / 鸣枪 / 怪枪：攻击后选择「共鸣一次」或「轮转命运」；若被对应，改为
-- 对手替你选择。
local function select_resonance_or_rotate(ctx, label)
  local chooser = ctx:player()
  if ctx:last_attack_responded() then chooser = ctx:opp() end
  local pick = ctx:choose_for(chooser, label .. "：选择一项", { "共鸣一次", "轮转命运" })
  if pick == 0 then
    ctx:resonance()
  else
    ctx:rotate_fates()
  end
end

-- 哀愁意志：对手弃一张手牌或一张盖牌（由对手选择；没有可弃的牌则无事发生）。
local function enemy_discards_one(ctx, label)
  local opp = ctx:opp()
  local pool = {}
  for _, i in ipairs(ctx:hand(opp)) do pool[#pool + 1] = i end
  for _, i in ipairs(ctx:cover_pile(opp)) do pool[#pool + 1] = i end
  if #pool == 0 then return end
  local sel = ctx:choose_cards_for(opp, label .. "：弃一张手牌或一张盖牌", pool, 1, 1)
  for _, i in ipairs(sel) do ctx:discard_card(i) end
end

-- 虚幻意志：你可以轮转命运一次。
local function optional_rotate(ctx, label)
  if ctx:choose(label .. "：轮转命运？", { "轮转命运", "不轮转" }) == 1 then
    ctx:rotate_fates()
  end
end

return {

  ---------------------------------------------------------------------------
  -- 共有牌（O / A1 / A2 同名同效）
  ---------------------------------------------------------------------------
  -- O-N1 挥枪【1-5 1/1】攻击后：共鸣1次。
  { set = "innealra", form = "O", forms = { "O", "A1", "A2" }, num = 1, name = "挥枪",
    kind = "normal", type = "attack",
    attack = { range = { 1, 5 }, damage = { aura = 1, life = 1 } },
    on_attack_after = function(ctx) ctx:resonance() end },

  -- O-N5 雨露霜雪：全力。执行至多一次基本动作。按过去、现在、未来的顺序结算
  -- 三个命运的效果。（这不是共鸣，命运不会移动）
  { set = "innealra", form = "O", forms = { "O", "A1", "A2" }, num = 5, name = "雨露霜雪",
    kind = "normal", type = "action", full_power = true,
    on_play = function(ctx)
      ctx:free_basics(ctx:player(), 1)
      for i = 0, 2 do
        if ctx:fate_slot(i) >= 0 then ctx:resolve_fate_slot(i, false) end
      end
    end },

  -- O-N7 变迁：对应【纳1】。展开时：1虚到自装。弃置时：轮转命运。
  { set = "innealra", form = "O", forms = { "O", "A1", "A2" }, num = 7, name = "变迁",
    kind = "normal", type = "enhance", nagi = 1, response = true,
    on_expand = function(ctx) ctx:move("dust", "aura", 1, ctx:player(), ctx:player()) end,
    on_discard = function(ctx) ctx:rotate_fates() end },

  -- O-S4 造物诺伦神的万劫缠迫（5）：全力【纳5】展开时：盖伏任意多手牌，执行至多
  -- 等量的基本动作。生效中：纠葛所有命运。弃置时：所有命运回归常态，然后将此牌
  -- 移出游戏。使用后：此牌不能由其他牌的效果再次发动。
  { set = "innealra", form = "O", forms = { "O", "A1", "A2" }, num = 4,
    name = "造物诺伦神的万劫缠迫", kind = "special", type = "enhance",
    cost = 5, nagi = 5, full_power = true, no_reuse = true, fate_entangler = true,
    on_expand = function(ctx)
      local me = ctx:player()
      local hand = ctx:hand(me)
      local sel = {}
      if #hand > 0 then
        sel = ctx:choose_cards("万劫缠迫：盖伏任意多手牌（每张可执行一次基本动作）",
                               hand, 0, #hand)
      end
      for _, i in ipairs(sel) do ctx:cover_card(i) end
      ctx:free_basics(me, #sel)
      ctx:entangle_fates(true)
    end,
    on_discard = function(ctx)
      ctx:entangle_fates(false)
      ctx:remove_card(ctx:source_inst())
    end },

  ---------------------------------------------------------------------------
  -- 枪过去（过）
  ---------------------------------------------------------------------------
  -- O1-N2 诅咒【3 -/0】若本回合内你共鸣过，此攻击获得 +0/+1。
  -- 若本回合内你使用过非诺伦的牌，此攻击获得距离扩大（远2）。
  { set = "innealra", form = "O", num = 2, name = "诅咒", kind = "normal", type = "attack",
    attack = { range = { 3, 3 }, damage = { life = 0 } },
    on_play = function(ctx)
      local me = ctx:player()
      local src = ctx:source_inst()
      if ctx:resonance_count(me) > 0 then
        ctx:next_attack_mod {
          match = function(c, atk) return atk:source_inst() == src end,
          apply = function(c, atk) atk:add { aura = 0, life = 1 } end,
          this_turn = true,
        }
      end
      if ctx:used_non_innealra(me) then
        ctx:next_attack_mod {
          match = function(c, atk) return atk:source_inst() == src end,
          apply = function(c, atk) atk:extend_far(2) end,
          this_turn = true,
        }
      end
    end },

  -- O1-N3 祖枪【5-8 2/1】攻击后：选择一项（若被对应，改为对手为你选择）：
  -- 共鸣一次；轮转命运。
  { set = "innealra", form = "O", num = 3, name = "祖枪", kind = "normal", type = "attack",
    attack = { range = { 5, 8 }, damage = { aura = 2, life = 1 } },
    on_attack_after = function(ctx) select_resonance_or_rotate(ctx, "祖枪") end },

  -- O1-N4 怅憾：对手畏缩。共鸣一次。
  { set = "innealra", form = "O", num = 4, name = "怅憾", kind = "normal", type = "action",
    on_play = function(ctx)
      ctx:cower(ctx:opp())
      ctx:resonance()
    end },

  -- O1-N6 哀愁意志【纳2】展开时&弃置时：对手弃一张手牌或一张盖牌。
  { set = "innealra", form = "O", num = 6, name = "哀愁意志", kind = "normal",
    type = "enhance", nagi = 2,
    on_expand = function(ctx) enemy_discards_one(ctx, "哀愁意志") end,
    on_discard = function(ctx) enemy_discards_one(ctx, "哀愁意志") end },

  -- O1-S1 神枪·衰朽（2）【(4-X)-(4+Y) -/1】X = 对手用过的王牌数量，
  -- Y = 对手弃牌堆中牌数。再起：一回合内你共鸣了至少两次。
  { set = "innealra", form = "O", num = 1, name = "神枪·衰朽", kind = "special",
    type = "attack", cost = 2,
    attack = function(ctx)
      local x = #ctx:used_specials(ctx:opp())
      local y = #ctx:discard_pile(ctx:opp())
      local lo = 4 - x
      if lo < 0 then lo = 0 end
      return { range = { lo, 4 + y }, damage = { life = 1 } }
    end,
    reset = { kind = "end_turn",
      cond = function(ctx) return ctx:resonance_count(ctx:player()) >= 2 end } },

  -- O1-S2 阴郁·埋葬（1）：对应【纳1】展开中：对手的攻击不受攻击修正
  -- （即只有卡面数值，但替换类的正常结算）。
  { set = "innealra", form = "O", num = 2, name = "阴郁·埋葬", kind = "special",
    type = "enhance", cost = 1, nagi = 1, response = true,
    suppress_enemy_attack_mods = true },

  -- O1-S3 栖身·垂暮（3）：使用后：对手下一次重铸牌库时，他弃牌堆中所有牌不因
  -- 重铸而移动。使用后：对手重铸牌库后，将此牌移出游戏。
  { set = "innealra", form = "O", num = 3, name = "栖身·垂暮", kind = "special",
    type = "action", cost = 3, rebuild_freeze = true,
    on_play = function(ctx) ctx:set_rebuild_freeze(ctx:opp()) end },

  ---------------------------------------------------------------------------
  -- 枪现在（现）
  ---------------------------------------------------------------------------
  -- O2-N2 刃碎【4-5 2/1】若本回合内你共鸣过 +0/+1；若使用过非诺伦的牌 +0/+1。
  { set = "innealra.A1", form = "A1", num = 2, name = "刃碎", kind = "normal",
    type = "attack",
    attack = { range = { 4, 5 }, damage = { aura = 2, life = 1 } },
    on_play = function(ctx)
      local me = ctx:player()
      local src = ctx:source_inst()
      local function plus()
        ctx:next_attack_mod {
          match = function(c, atk) return atk:source_inst() == src end,
          apply = function(c, atk) atk:add { aura = 0, life = 1 } end,
          this_turn = true,
        }
      end
      if ctx:resonance_count(me) > 0 then plus() end
      if ctx:used_non_innealra(me) then plus() end
    end },

  -- O2-N3 鸣枪【4-6 2/1】选择一项（若被对应，改为对手替你选择）：共鸣一次；轮转命运。
  { set = "innealra.A1", form = "A1", num = 3, name = "鸣枪", kind = "normal",
    type = "attack",
    attack = { range = { 4, 6 }, damage = { aura = 2, life = 1 } },
    on_attack_after = function(ctx) select_resonance_or_rotate(ctx, "鸣枪") end },

  -- O2-N4 迷惘：装附或后退一次。共鸣一次。
  { set = "innealra.A1", form = "A1", num = 4, name = "迷惘", kind = "normal",
    type = "action",
    on_play = function(ctx)
      ctx:free_basics_of(ctx:player(), 1, { "aura", "retreat" })
      ctx:resonance()
    end },

  -- O2-N6 脆弱意志【纳4】展开中：对手因为装附外的手段将樱花结晶移动到敌装时，
  -- 改为将这些樱花结晶移到这张牌上；对手因基本动作将樱花结晶移动到敌装时，
  -- 将此牌上 1 樱花结晶移到虚。
  { set = "innealra.A1", form = "A1", num = 6, name = "脆弱意志", kind = "normal",
    type = "enhance", nagi = 4, fragile_will = true },

  -- O2-S1 神枪·啸叹（7）【5-10 4/4】
  { set = "innealra.A1", form = "A1", num = 1, name = "神枪·啸叹", kind = "special",
    type = "attack", cost = 7,
    attack = { range = { 5, 10 }, damage = { aura = 4, life = 4 } } },

  -- O2-S2 阵雨·覆逆（5）：对应【纳1】展开中：对手的所有攻击获得 -1/+0；
  -- 对手攻击结算完毕后，对敌装造成 1 伤害。
  { set = "innealra.A1", form = "A1", num = 2, name = "阵雨·覆逆", kind = "special",
    type = "enhance", cost = 5, nagi = 1, response = true,
    on_expand = function(ctx)
      -- 以对应打出的这一次：攻击的连续修正已在声明时结算，这里补上 -1/+0。
      local atk = ctx:responding_attack()
      if atk then atk:add { aura = -1 } end
    end,
    continuous = {
      { when = "expanded", query = "attack",
        apply = function(ctx, atk)
          if atk:attacker() == ctx:player() then return end
          atk:add { aura = -1 }
        end },
    },
    triggers = {
      { event = "attack_resolved",
        cond = function(ctx, ev) return ev:attacker() == ctx:opp() end,
        run = function(ctx, ev) ctx:deal_damage(ctx:opp(), 1, nil) end },
    } },

  -- O2-S3 残恣·嗜灭（3）【1-3 1/1】若此攻击对敌装造成伤害，把本应移到虚的结晶
  -- 移到距。再起：本回合内你至少共鸣过 2 次。
  { set = "innealra.A1", form = "A1", num = 3, name = "残恣·嗜灭", kind = "special",
    type = "attack", cost = 3,
    attack = { range = { 1, 3 }, damage = { aura = 1, life = 1 },
               keywords = { "aura_to_distance" } },
    reset = { kind = "end_turn",
      cond = function(ctx) return ctx:resonance_count(ctx:player()) >= 2 end } },

  ---------------------------------------------------------------------------
  -- 枪未来（未）
  ---------------------------------------------------------------------------
  -- O3-N2 星空【0-2 1/1】若本回合内你共鸣过 +0/+1；若本回合内你使用过非诺伦的
  -- 牌，此牌对对手造成伤害时，将要移动的樱花结晶移到惑。
  { set = "innealra.A2", form = "A2", num = 2, name = "星空", kind = "normal",
    type = "attack",
    attack = { range = { 0, 2 }, damage = { aura = 1, life = 1 } },
    on_play = function(ctx)
      local me = ctx:player()
      local src = ctx:source_inst()
      if ctx:resonance_count(me) > 0 then
        ctx:next_attack_mod {
          match = function(c, atk) return atk:source_inst() == src end,
          apply = function(c, atk) atk:add { aura = 0, life = 1 } end,
          this_turn = true,
        }
      end
      if ctx:used_non_innealra(me) then
        ctx:next_attack_mod {
          match = function(c, atk) return atk:source_inst() == src end,
          apply = function(c, atk) atk:keyword("to_waku") end,
          this_turn = true,
        }
      end
    end },

  -- O3-N3 怪枪【1-3 2/1】选择一项（若被对应，改为对手替你选择）：共鸣一次；轮转命运。
  { set = "innealra.A2", form = "A2", num = 3, name = "怪枪", kind = "normal",
    type = "attack",
    attack = { range = { 1, 3 }, damage = { aura = 2, life = 1 } },
    on_attack_after = function(ctx) select_resonance_or_rotate(ctx, "怪枪") end },

  -- O3-N4 谛听：聚气或前进一次。共鸣一次。
  { set = "innealra.A2", form = "A2", num = 4, name = "谛听", kind = "normal",
    type = "action",
    on_play = function(ctx)
      ctx:free_basics_of(ctx:player(), 1, { "flare", "advance" })
      ctx:resonance()
    end },

  -- O3-N6 虚幻意志【纳2】打出时&弃置时：你可以轮转命运。
  -- 展开中：这张牌上的樱花结晶被移除时，不移到虚，而移到惑。
  { set = "innealra.A2", form = "A2", num = 6, name = "虚幻意志", kind = "normal",
    type = "enhance", nagi = 2, decay_to = "waku",
    on_expand = function(ctx) optional_rotate(ctx, "虚幻意志") end,
    on_discard = function(ctx) optional_rotate(ctx, "虚幻意志") end },

  -- O3-S1 神枪·永世（2）【0-2 2/1】若你使用此牌时支付了费用，将费用移到惑。
  -- 再起：本回合内你至少共鸣过 2 次。
  { set = "innealra.A2", form = "A2", num = 1, name = "神枪·永世", kind = "special",
    type = "attack", cost = 2,
    attack = { range = { 0, 2 }, damage = { aura = 2, life = 1 } },
    on_play = function(ctx) ctx:cost_to_waku() end,
    reset = { kind = "end_turn",
      cond = function(ctx) return ctx:resonance_count(ctx:player()) >= 2 end } },

  -- O3-S2 舍弃·希冀（2）：对应【纳1】展开时：若你使用此牌时支付了费用，将费用
  -- 移到惑。展开时：这张牌的献可以从距中选择。
  { set = "innealra.A2", form = "A2", num = 2, name = "舍弃·希冀", kind = "special",
    type = "enhance", cost = 2, nagi = 1, response = true, nagi_from_distance = true,
    on_expand = function(ctx) ctx:cost_to_waku() end },

  -- O3-S3 宇宙·幽邃（0）：对应。你可以轮转命运。1 自装到惑，或 1 惑到自装。
  -- 使用后：每回合准备阶段开始时，进行攻击"【0-10 1/1】攻击后：若上述攻击造成了
  -- 伤害，将要移动的樱花结晶移到惑，然后将此牌（宇宙·幽邃）移出游戏"。
  { set = "innealra.A2", form = "A2", num = 3, name = "宇宙·幽邃", kind = "special",
    type = "action", cost = 0, response = true,
    on_play = function(ctx)
      local me = ctx:player()
      optional_rotate(ctx, "宇宙·幽邃")
      if ctx:choose("宇宙·幽邃：选择一项", { "1 自装到惑", "1 惑到自装" }) == 1 then
        ctx:move("aura", "waku", 1, me, me)
      else
        ctx:move("waku", "aura", 1, me, me)
      end
    end,
    triggers = {
      { event = "turn_start",
        cond = function(ctx, ev) return ev:subject() == ctx:player() end,
        run = function(ctx, ev)
          ctx:attack {
            range = { 0, 10 }, damage = { aura = 1, life = 1 },
            keywords = { "to_waku" },
            after = function(c, atk)
              if c:last_attack_amount() > 0 then
                c:remove_card(c:source_inst())
              end
            end,
          }
        end },
    } },

  ---------------------------------------------------------------------------
  -- 命运（隐藏 CardDef，不进构筑 / 不实例化）
  ---------------------------------------------------------------------------
  -- 过去的命运（枪过去 O）
  -- 修省：本回合内你不能攻击。若当前距离 <=5，1 敌装到距；若此命运位于过去，
  -- 再执行一次。纠葛：本回合内你不能使用任何通常牌。对敌命造成 1 伤害。
  { set = "innealra", form = "O", num = 901, name = "修省", kind = "normal",
    type = "action", fate = true, fate_slot = 0,
    on_fate = function(ctx)
      local me, opp = ctx:player(), ctx:opp()
      local slot = ctx:fate_resolving_slot()
      ctx:set_cannot_attack(me)
      local function once()
        if ctx:distance() <= 5 then ctx:move("aura", "distance", 1, opp, opp) end
      end
      once()
      if slot == 0 then once() end
      if ctx:fates_entangled() then
        ctx:set_cannot_use_normals(me)
        ctx:deal_damage(opp, nil, 1)
      end
    end },

  -- 悔恨：本回合内你不能前进或后退。1 敌气到敌装。
  -- 纠葛：本回合内你不能执行基本动作，对敌命造成 1 伤害。
  { set = "innealra", form = "O", num = 902, name = "悔恨", kind = "normal",
    type = "action", fate = true, fate_slot = 1,
    on_fate = function(ctx)
      local me, opp = ctx:player(), ctx:opp()
      ctx:set_cannot_advance(me)
      ctx:set_cannot_retreat(me)
      ctx:move("flare", "aura", 1, opp, opp)
      if ctx:fates_entangled() then
        ctx:set_cannot_basic(me)
        ctx:deal_damage(opp, nil, 1)
      end
    end },

  -- 怨艾：若此命运位于过去，对敌命造成 1 伤害。若如此做，你弃置一张对应牌，
  -- 若不能弃置则展示手牌。纠葛：若你本回合内没有使用过通常牌，对敌命造成 1 伤害。
  { set = "innealra", form = "O", num = 903, name = "怨艾", kind = "normal",
    type = "action", fate = true, fate_slot = 2,
    on_fate = function(ctx)
      local me, opp = ctx:player(), ctx:opp()
      local slot = ctx:fate_resolving_slot()
      if slot == 0 then
        ctx:deal_damage(opp, nil, 1)
        local pool = {}
        for _, i in ipairs(ctx:hand(me)) do
          if ctx:is_response(i) then pool[#pool + 1] = i end
        end
        if #pool > 0 then
          local sel = ctx:choose_cards("怨艾：弃置一张对应牌", pool, 1, 1)
          for _, i in ipairs(sel) do ctx:discard_card(i) end
        else
          ctx:reveal_cards(ctx:opp(), me, "hand")
        end
      end
      if ctx:fates_entangled() and ctx:used_normal_this_turn(me) == 0 then
        ctx:deal_damage(opp, nil, 1)
      end
    end },

  -- 长眠：弃置你牌库顶一张牌。弃置对手牌库顶一张牌。若此命运位于过去，再执行一次。
  -- 纠葛：若你本回合内没有执行过基本动作，对敌命造成 1 伤害。
  { set = "innealra", form = "O", num = 904, name = "长眠", kind = "normal",
    type = "action", fate = true, fate_slot = 3,
    on_fate = function(ctx)
      local me, opp = ctx:player(), ctx:opp()
      local slot = ctx:fate_resolving_slot()
      local function once()
        ctx:discard_top(me)
        ctx:discard_top(opp)
      end
      once()
      if slot == 0 then once() end
      if ctx:fates_entangled() and not ctx:did_basic_this_turn(me) then
        ctx:deal_damage(opp, nil, 1)
      end
    end },

  -- 现在的命运（枪现在 A1）
  -- 惶惑：1 敌装到敌气。若敌装不小于 5，再执行一次。纠葛：2 敌装到自气。
  { set = "innealra.A1", form = "A1", num = 901, name = "惶惑", kind = "normal",
    type = "action", fate = true, fate_slot = 0,
    on_fate = function(ctx)
      local me, opp = ctx:player(), ctx:opp()
      ctx:move("aura", "flare", 1, opp, opp)
      if ctx:aura(opp) >= 5 then ctx:move("aura", "flare", 1, opp, opp) end
      if ctx:fates_entangled() then ctx:move("aura", "flare", 2, opp, me) end
    end },

  -- 颓废：距离大于 4 时，2 距到虚；距离小于 4 时，2 虚到距。
  -- 纠葛：至多 3 距到虚，具体数值你指定。
  { set = "innealra.A1", form = "A1", num = 902, name = "颓废", kind = "normal",
    type = "action", fate = true, fate_slot = 1,
    on_fate = function(ctx)
      local me = ctx:player()
      local d = ctx:distance()
      if d > 4 then
        ctx:move("distance", "dust", 2)
      elseif d < 4 then
        ctx:move("dust", "distance", 2)
      end
      if ctx:fates_entangled() then
        local pick = ctx:choose("颓废（纠葛）：至多 3 距到虚，具体数值你指定",
                                { "0 个", "1 个", "2 个", "3 个" })
        local n = pick - 1
        if n > 0 then ctx:move("distance", "dust", n) end
      end
    end },

  -- 流离：你畏缩。进行攻击【3-5 2/2】。纠葛：进行攻击【2-5 3/2】。
  { set = "innealra.A1", form = "A1", num = 903, name = "流离", kind = "normal",
    type = "action", fate = true, fate_slot = 2,
    on_fate = function(ctx)
      local me = ctx:player()
      ctx:cower(me)
      ctx:attack { range = { 3, 5 }, damage = { aura = 2, life = 2 } }
      if ctx:fates_entangled() then
        ctx:attack { range = { 2, 5 }, damage = { aura = 3, life = 2 } }
      end
    end },

  -- 救赎：2 虚到自装，1 虚到敌装。纠葛：3 虚到自装。
  { set = "innealra.A1", form = "A1", num = 904, name = "救赎", kind = "normal",
    type = "action", fate = true, fate_slot = 3,
    on_fate = function(ctx)
      local me, opp = ctx:player(), ctx:opp()
      ctx:move("dust", "aura", 2, me, me)
      ctx:move("dust", "aura", 1, me, opp)
      if ctx:fates_entangled() then ctx:move("dust", "aura", 3, me, me) end
    end },

  -- 未来的命运（枪未来 A2）
  -- 歆羡：至多 3 惑到虚，然后进行攻击【0-3 X/1】，X 为你移动的樱花结晶的数目。
  -- 纠葛：进行攻击【0-X 3/5】不可对，X 为惑的数目，然后将所有惑移到虚。
  { set = "innealra.A2", form = "A2", num = 901, name = "歆羡", kind = "normal",
    type = "action", fate = true, fate_slot = 0,
    on_fate = function(ctx)
      local me = ctx:player()
      local pick = ctx:choose("歆羡：至多 3 惑到虚", { "0 个", "1 个", "2 个", "3 个" })
      local want = pick - 1
      local moved = 0
      if want > 0 then moved = ctx:move("waku", "dust", want, me, me) end
      ctx:attack {
        range = { 0, 3 },
        damage = function(c) return { aura = moved, life = 1 } end,
      }
      if ctx:fates_entangled() then
        local x = ctx:waku(me)
        ctx:attack {
          range = { 0, x }, damage = { aura = 3, life = 5 },
          keywords = { "unrespondable" },
        }
        ctx:move("waku", "dust", ctx:waku(me), me, me)
      end
    end },

  -- 掩抑：若你是回合开始时抽牌后进行共鸣，获得 1 行动力，1 自装到惑。
  -- 否则对手畏缩，1 惑到虚。纠葛：1 敌命到惑。
  { set = "innealra.A2", form = "A2", num = 902, name = "掩抑", kind = "normal",
    type = "action", fate = true, fate_slot = 1,
    on_fate = function(ctx)
      local me, opp = ctx:player(), ctx:opp()
      if ctx:fate_from_turn_start() then
        ctx:gain_vigor(me, 1)
        ctx:move("aura", "waku", 1, me, me)
      else
        ctx:cower(opp)
        ctx:move("waku", "dust", 1, me, me)
      end
      if ctx:fates_entangled() then ctx:move("life", "waku", 1, opp, me) end
    end },

  -- 脱逃：若你是回合开始时抽牌后进行共鸣，1 距到惑。否则 1 距到虚，1 惑到虚。
  -- 纠葛：1 距到惑，1 虚到惑。
  { set = "innealra.A2", form = "A2", num = 903, name = "脱逃", kind = "normal",
    type = "action", fate = true, fate_slot = 2,
    on_fate = function(ctx)
      local me = ctx:player()
      if ctx:fate_from_turn_start() then
        ctx:move("distance", "waku", 1, me, me)
      else
        ctx:move("distance", "dust", 1)
        ctx:move("waku", "dust", 1, me, me)
      end
      if ctx:fates_entangled() then
        ctx:move("distance", "waku", 1, me, me)
        ctx:move("dust", "waku", 1, me, me)
      end
    end },

  -- 迷蒙：若你是回合开始时抽牌后进行共鸣，2 虚到惑。否则 1 虚到惑。
  -- 纠葛：1 敌装到惑，1 敌气到惑。
  { set = "innealra.A2", form = "A2", num = 904, name = "迷蒙", kind = "normal",
    type = "action", fate = true, fate_slot = 3,
    on_fate = function(ctx)
      local me, opp = ctx:player(), ctx:opp()
      if ctx:fate_from_turn_start() then
        ctx:move("dust", "waku", 2, me, me)
      else
        ctx:move("dust", "waku", 1, me, me)
      end
      if ctx:fates_entangled() then
        ctx:move("aura", "waku", 1, opp, me)
        ctx:move("flare", "waku", 1, opp, me)
      end
    end },

}
