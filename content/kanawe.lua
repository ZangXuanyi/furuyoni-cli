-- 20-Kanawe 叶慧
-- 【象征武器】面具（O）。
-- 【机制】地图 + 戏剧。游戏开始时在地图的开始区域（O2）。完成戏剧上的条件后，
--   可以在地图上前进一格，按落格颜色获得奖励：
--     红 → 对敌人造成 1 命伤；紫 → 执行一次基本动作；
--     绿 → 从盖牌区选一张放到牌库底；黄 → 站在黄格时你的非衍生攻击 +0/+1。
--   走到终点 → 对手死亡。
--   戏剧栏开局持有全部 6 张戏剧（O-T1..O-T6）；达成次数跨回合累计；
--   换戏剧时旧戏剧回到未完成堆且进度不保留；完成的戏剧进入已完成堆。
--   达成所需次数：《杀阵》《樱花》《鼓动》《定位》基础 2 / 升级 1，
--   其中《明转》升级的条件明写「达成次数2」；《战栗》基础 1 / 升级 2。

-- 地图节点（引擎静态表，见 src/engine/engine.cpp）：
--   O2 → 1A/1B/2C ；1A → 2A/2B ；1B → 2A/2B ；2A → 3A ；2B → 3A/3B（试炼 3C）
--   2C → 3D ；3A → 4A ；3B → 4A ；3C → 4B ；3D →（试炼 4B）
--   4A → 5A/5B/5C ；4B → 5C ；5A/5B/5C →（试炼 终点）

return {

  ---------------------------------------------------------------------------
  -- 面 O-N
  ---------------------------------------------------------------------------

  -- O-N1 构思：【0-10 X/1】X = 当前剧目数值的一半（向上取整）。
  -- 攻击后：若当前剧目数值是偶数，则 1 距到虚。
  { set = "kanawe", form = "O", num = 1, name = "构思", kind = "normal", type = "attack",
    attack = function(ctx)
      local x = math.floor((ctx:node_value() + 1) / 2)  -- 向上取整
      return { range = { 0, 10 }, damage = { aura = x, life = 1 } }
    end,
    on_attack_after = function(ctx)
      if ctx:node_value() % 2 == 0 then ctx:move("distance", "dust", 1) end
    end },

  -- O-N2 撰写：【3-5 1/1】攻击后：准备一个未完成过的戏剧（换下的进度不保留）。
  -- 你的回合结束时，若此牌在弃牌堆，可以放到牌库底；若如此做，对手获得 1 集中力。
  { set = "kanawe", form = "O", num = 2, name = "撰写", kind = "normal", type = "attack",
    attack = { range = { 3, 5 }, damage = { aura = 1, life = 1 } },
    on_attack_after = function(ctx) ctx:prepare_drama(false) end,
    triggers = {
      { event = "turn_end", zone = "discard",
        cond = function(ctx, ev) return ev:subject() == ctx:player() end,
        run = function(ctx)
          if ctx:choose("撰写：把这张牌放到牌库底？（对手获得1集中力）",
                        { "放到牌库底", "不" }) == 1 then
            ctx:to_deck_bottom(ctx:source_inst())
            ctx:gain_vigor(ctx:opp(), 1)
          end
        end } } },

  -- O-N3 演出：【2-3 2/1】若当前剧目颜色是紫，此攻击 +0/+1。
  -- 攻击后：本回合内你不能完成戏剧。
  { set = "kanawe", form = "O", num = 3, name = "演出", kind = "normal", type = "attack",
    attack = { range = { 2, 3 }, damage = { aura = 2, life = 1 } },
    on_play = function(ctx)
      if ctx:node_color() == "purple" then
        -- 注册「下一次攻击」修正：on_play 之后紧接着结算的就是本牌自己的攻击。
        ctx:next_attack_mod {
          match = function(c2, atk) return atk:attacker() == c2:player() end,
          apply = function(c2, atk) atk:add { life = 1 } end,
          this_turn = true,
        }
      end
    end,
    on_attack_after = function(ctx) ctx:set_no_drama() end },

  -- O-N4 杀青：你可以执行一次装附。然后你可以执行另一个基本动作。
  -- 全开：立即准备下一幕戏剧（可从已完成的戏剧中选择）。对手畏缩。
  { set = "kanawe", form = "O", num = 4, name = "杀青", kind = "normal", type = "action",
    zenkai = true,
    on_play = function(ctx)
      local me = ctx:player()
      if ctx:choose("杀青：执行一次装附？", { "装附", "不" }) == 1 then
        ctx:do_basic(me, "aura")
      end
      if ctx:choose("杀青：再执行另一个基本动作？", { "执行", "不" }) == 1 then
        local legal = ctx:legal_basics(me)
        if #legal > 0 then
          local opts = {}
          for _, nm in ipairs(legal) do opts[#opts + 1] = nm end
          local pick = ctx:choose("杀青：选择基本动作", opts)
          ctx:do_basic(me, legal[pick])
        end
      end
      if ctx:zenkai() then ctx:prepare_drama(true) end
      ctx:cower(ctx:opp())
    end },

  -- O-N5 打光：若当前剧目颜色为紫或绿，查看对手手牌，选 1 张放到对手牌库底，
  -- 对手获得 1 集中力。
  { set = "kanawe", form = "O", num = 5, name = "打光", kind = "normal", type = "action",
    on_play = function(ctx)
      local col = ctx:node_color()
      if col ~= "purple" and col ~= "green" then return end
      local o = ctx:opp()
      local picks = ctx:reveal_cards(ctx:player(), o, "hand",
                                     "打光：检视对手手牌，选 1 张放到其牌库底", 1, 1)
      if #picks > 0 then ctx:to_deck_bottom(picks[1]) end
      ctx:gain_vigor(o, 1)
    end },

  -- O-N6 即兴：对应。你可以从手牌中使用一张非叶慧且非全力的攻击牌。
  -- 若此牌对应了一个攻击，视作你的这张攻击牌也对应了攻击。
  -- 然后，若当前剧目颜色为绿且此牌在弃牌区，将「即兴」拿回手牌。
  { set = "kanawe", form = "O", num = 6, name = "即兴", kind = "normal", type = "action",
    response = true,
    on_play = function(ctx)
      ctx:store_int("used_turn", ctx:turn_number())
      local me = ctx:player()
      local cands = {}
      for _, inst in ipairs(ctx:hand(me)) do
        if ctx:is_attack(inst) and not ctx:is_full_power(inst) and
           not ctx:card_is_goddess(inst, "kanawe") then
          cands[#cands + 1] = inst
        end
      end
      if #cands == 0 then return end
      local picks = ctx:choose_cards("即兴：从手牌使用一张非叶慧非全力的攻击牌", cands, 1, 1)
      if #picks == 0 then return end
      if ctx:responding_attack() then
        ctx:play_hand_card_as_response(picks[1])
      else
        ctx:play_hand_card(picks[1])
      end
    end,
    triggers = {
      { event = "discarded", zone = "discard",
        cond = function(ctx, ev)
          -- 只在「本回合使用过即兴」之后触发（即兴结算完毕后自然进入弃牌堆）。
          return ev:card() == ctx:source_inst() and ctx:node_color() == "green" and
                 ctx:load_int("used_turn", -1) == ctx:turn_number()
        end,
        run = function(ctx) ctx:to_hand(ctx:source_inst()) end } } },

  -- O-N7 封杀：【纳3】破绽。展开时：宣言一个牌名。生效中，对手不能使用同名切牌。
  -- 弃置时：你获得 1 集中力。（卡面「若当前剧目颜色是哦红」文本残缺，见交付报告。）
  { set = "kanawe", form = "O", num = 7, name = "封杀", kind = "normal", type = "enhance",
    nagi = 3, breakable = true, cut_ban = true,
    on_expand = function(ctx) ctx:declare_cut_ban() end,
    on_discard = function(ctx) ctx:gain_vigor(ctx:player(), 1) end },

  ---------------------------------------------------------------------------
  -- 面 O-S
  ---------------------------------------------------------------------------

  -- O-S1 疾书弗尽、尺璧寸阴（1）：立即准备下一幕戏剧（可从已完成的戏剧中选择）。
  -- 若从已完成的戏剧中选择，则将此牌移出游戏。
  -- 使用后：对手的准备阶段开始时，若你上回合没有推进戏剧，则此牌变为未使用状态；
  -- 若如此做，对手获得 1 集中力。
  { set = "kanawe", form = "O", num = 1, name = "疾书弗尽、尺璧寸阴",
    kind = "special", type = "action", cost = 1,
    on_play = function(ctx)
      if ctx:prepare_drama(true) then ctx:remove_card(ctx:source_inst()) end
    end,
    triggers = {
      { event = "turn_start",
        cond = function(ctx, ev)
          return ev:subject() == ctx:opp() and
                 not ctx:drama_progressed_last_turn(ctx:player())
        end,
        run = function(ctx)
          ctx:reset_special(ctx:source_inst())
          ctx:gain_vigor(ctx:opp(), 1)
        end } } },

  -- O-S2 芳颜无常、星辉灯影（X）：X = 当前剧目数值。
  -- 若当前剧目颜色不是黄的，可以结算一次它的效果。
  -- 即再起：你进入下一个剧目（前进到新节点）。
  { set = "kanawe", form = "O", num = 2, name = "芳颜无常、星辉灯影",
    kind = "special", type = "action",
    cost = function(ctx) return ctx:node_value() end,
    on_play = function(ctx)
      local col = ctx:node_color()
      if col == "yellow" or col == "none" then return end
      if ctx:choose("芳颜无常：结算一次当前剧目的效果？", { "结算", "不结算" }) == 1 then
        ctx:resolve_node_reward()
      end
    end,
    triggers = {
      { event = "node_advanced",
        cond = function(ctx, ev) return ev:subject() == ctx:player() end,
        run = function(ctx) ctx:reset_special(ctx:source_inst()) end } } },

  -- O-S3 良宵讵待、胜景久盈（4）：对应【0-4 2/1】。
  -- 攻击后：打消被对应的非王牌的攻击；若被对应的攻击是通常牌，
  -- 将被对应的攻击放在对手的牌库顶。
  { set = "kanawe", form = "O", num = 3, name = "良宵讵待、胜景久盈",
    kind = "special", type = "attack", cost = 4, response = true,
    attack = { range = { 0, 4 }, damage = { aura = 2, life = 1 } },
    on_attack_after = function(ctx)
      local a = ctx:responding_attack()
      if not a then return end
      if not a:from_special() then a:negate() end
      if a:from_normal() then
        local si = a:source_inst()
        if si and si >= 0 then ctx:to_deck_top(si) end
      end
    end },

  -- O-S4 知音难觅、化境在心（2）：从手牌展示一张牌并移出游戏；
  -- 获得你在构筑阶段未获得的一张常规牌。然后将此牌移出游戏，
  -- 获得你在构筑阶段未获得的一张切牌。
  { set = "kanawe", form = "O", num = 4, name = "知音难觅、化境在心",
    kind = "special", type = "action", cost = 2,
    on_play = function(ctx)
      local me = ctx:player()
      local opp = ctx:opp()
      local hand = ctx:hand(me)
      if #hand > 0 then
        local picks = ctx:choose_cards("知音难觅：展示一张手牌并移出游戏", hand, 1, 1)
        if #picks > 0 then
          -- 展示选定的那张牌给对手（瞬时公开；filter 限定只公开这一张）。
          ctx:reveal_cards(opp, me, "hand", nil, nil, nil,
                           function(_, i) return i == picks[1] end)
          ctx:remove_from_game(picks[1])
        end
      end
      local normals = ctx:unchosen_normals(me)
      if #normals > 0 then
        local picks = ctx:choose_cards("知音难觅：获得一张未获得的常规牌", normals, 1, 1)
        if #picks > 0 then ctx:gain_unchosen_normal(picks[1]) end
      end
      ctx:remove_card(ctx:source_inst())
      local cuts = ctx:unchosen_specials(me)
      if #cuts > 0 then
        local picks = ctx:choose_cards("知音难觅：获得一张未获得的切牌", cuts, 1, 1)
        if #picks > 0 then ctx:gain_unchosen_cut(picks[1]) end
      end
    end },

  ---------------------------------------------------------------------------
  -- 戏剧 O-T1..O-T6（不进构筑；开局放入戏剧区，由引擎自动判定达成条件）
  ---------------------------------------------------------------------------

  { set = "kanawe", form = "O", num = 911, name = "《杀阵》", kind = "normal", type = "action",
    extra = true, drama = true, drama_slot = 1 },
  { set = "kanawe", form = "O", num = 912, name = "《樱花》", kind = "normal", type = "action",
    extra = true, drama = true, drama_slot = 2 },
  { set = "kanawe", form = "O", num = 913, name = "《鼓动》", kind = "normal", type = "action",
    extra = true, drama = true, drama_slot = 3 },
  { set = "kanawe", form = "O", num = 914, name = "《明转》", kind = "normal", type = "action",
    extra = true, drama = true, drama_slot = 4 },
  { set = "kanawe", form = "O", num = 915, name = "《战栗》", kind = "normal", type = "action",
    extra = true, drama = true, drama_slot = 5 },
  { set = "kanawe", form = "O", num = 916, name = "《定位》", kind = "normal", type = "action",
    extra = true, drama = true, drama_slot = 6 },

}
