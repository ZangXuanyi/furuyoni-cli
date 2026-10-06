-- 16-Yatsuha 八叶
-- 【象征武器】八咫叶慧镜（O, 镜）；八重徒寄樱（A1, 花）；魂（AA1）
-- 【机制】镜映：你的装、气、命中，与对手对应区域樱花结晶数相同的区域数量（ctx:mirror()，0..3）。
-- 注释：A1、AA1 两个形态只有 1 张切牌（用 solo_specials 标记，见引擎 deck_def_ids）。
-- 注释：AA1 是"异相的异相"，按 A2 处理（引擎按 form 字符串枚举，无需特判）。


-- 旅途的四项奖励
function traveler_rewards()
  return { "抽1张牌", "获得1集中力", "付与牌 1献到虚 或 1虚到献", "弃牌/盖牌堆 1 张置于牌库底" }
end

function traveler_grant(ctx, idx)
  local me = ctx:player()
  if idx == 0 then
    ctx:draw(me, 1)
  elseif idx == 1 then
    ctx:gain_vigor(me, 1)
  elseif idx == 2 then
    local cand = {}
    for _, i in ipairs(ctx:enhances(me)) do cand[#cand + 1] = i end
    for _, i in ipairs(ctx:enhances(ctx:opp())) do cand[#cand + 1] = i end
    if #cand > 0 then
      local sel = ctx:choose_cards("旅途：选择一张展开中的付与牌（可不选）", cand, 0, 1)
      if #sel > 0 then
        if ctx:choose("旅途：1献到虚 或 1虚到献", { "1献到虚", "1虚到献" }) == 1 then
          ctx:drain_card_crystals(sel[1], 1)
        else
          ctx:move_to_card("dust", sel[1], 1)
        end
      end
    end
  else
    local cand = {}
    for _, i in ipairs(ctx:discard_pile(me)) do cand[#cand + 1] = i end
    for _, i in ipairs(ctx:cover_pile(me)) do cand[#cand + 1] = i end
    if #cand > 0 then
      local sel = ctx:choose_cards("旅途：选择一张牌置于牌库底（可不选）", cand, 0, 1)
      for _, i in ipairs(sel) do ctx:to_deck_bottom(i) end
    end
  end
end

-- 第二次回到起点：发放"此目所及之物与世"，把全部通常牌扣置入回忆区，并移除旅途与千芳犹增色。
function traveler_finale(ctx, self)
  local me = ctx:player()
  ctx:gain_extra("此目所及之物与世")  -- 以未使用状态加入切牌区
  -- 选择至多 1 张手牌保留
  local keep = -1
  local hand = ctx:hand(me)
  if #hand > 0 then
    local sel = ctx:choose_cards("旅途终幕：选择至多 1 张手牌保留", hand, 0, 1)
    if #sel > 0 then keep = sel[1] end
  end
  ctx:all_normals_to_memory(me, keep)  -- 其余全部通常牌扣置入回忆区
  -- 旅途 与 千芳犹增色 移出游戏（其上的樱花结晶移到虚）
  for _, i in ipairs(ctx:special_cards(me)) do
    local nm = ctx:card_name(i)
    if nm == "千芳犹增色" then
      ctx:empty_card(i)
      ctx:remove_card(i)
    end
  end
  ctx:empty_card(self)
  ctx:remove_card(self)
end

-- 从手牌或弃牌堆选择一张可升级的八叶牌，变为完全态（返回实例号，-1 = 未升级）。
local function upgrade_from_hand_or_discard(ctx, prompt)
  local me = ctx:player()
  local cand = {}
  for _, i in ipairs(ctx:hand(me)) do
    if ctx:can_upgrade(i) then cand[#cand + 1] = i end
  end
  for _, i in ipairs(ctx:discard_pile(me)) do
    if ctx:can_upgrade(i) then cand[#cand + 1] = i end
  end
  if #cand == 0 then return -1 end
  local sel = ctx:choose_cards(prompt, cand, 0, 1)
  if #sel == 0 then return -1 end
  if ctx:upgrade_card(sel[1]) then return sel[1] end
  return -1
end

return {

  ---------------------------------------------------------------------------
  -- 八咫叶慧镜（镜）O
  ---------------------------------------------------------------------------
  { set = "yatsuha", form = "O", num = 1, name = "星云爪", kind = "normal", upgrade = "星辰之利爪", type = "attack",
    attack = { range = { 3, 4 }, damage = { aura = 3, life = 2 },
               keywords = { "no_normal_response" } },
    on_attack_after = function(ctx)
      ctx:move("aura", "flare", 1, ctx:player(), ctx:opp())  -- 1自装到敌气
    end },

  { set = "yatsuha", form = "O", num = 2, name = "昏神颚", kind = "normal", upgrade = "深渊之巨吻", type = "attack",
    attack = function(ctx)
      return { range = { 4, 4 }, damage = { aura = 3, life = 1 + ctx:mirror() } }
    end,
    on_attack_after = function(ctx) ctx:cower(ctx:player()) end },

  { set = "yatsuha", form = "O", num = 3, name = "镜之恶魔", kind = "normal", upgrade = "现世之魔物", type = "attack",
    full_power = true,
    attack = { range = { 2, 3 }, damage = { aura = 5, life = 3 } },
    on_attack_after = function(ctx) ctx:move("life", "dust", 1, ctx:player(), ctx:player()) end },

  { set = "yatsuha", form = "O", num = 4, name = "幻影步法", kind = "normal", upgrade = "幻影联动", type = "action",
    on_play = function(ctx)
      local me = ctx:player()
      ctx:gain_vigor(me, 1)
      -- 本回合内距离/达人距离 +1，或 -1
      local d = (ctx:choose("幻影步法：本回合内", { "距离+1，达人距离+1", "距离-1，达人距离-1" }) == 1)
                    and 1 or -1
      ctx:add_temp_distance(me, d)
      ctx:add_temp_near_distance(me, d)
    end },

  { set = "yatsuha", form = "O", num = 5, name = "意志", kind = "normal", upgrade = "决心", type = "action",
    response = true,
    on_play = function(ctx)
      local me, opp = ctx:player(), ctx:opp()
      local opts, acts = {}, {}
      if ctx:aura(me) >= 1 then opts[#opts + 1], acts[#acts + 1] = "1自装到自气", "a2f" end
      if ctx:flare(me) >= 1 then opts[#opts + 1], acts[#acts + 1] = "1自气到自装", "f2a" end
      if ctx:aura(opp) >= 1 then opts[#opts + 1], acts[#acts + 1] = "1敌装到敌气", "oa2f" end
      if ctx:flare(opp) >= 1 then opts[#opts + 1], acts[#acts + 1] = "1敌气到敌装", "of2a" end
      if #opts == 0 then return end
      local sel = ctx:choose_options("意志：选择至少一项", opts, 1, #opts)
      for _, i in ipairs(sel) do
        local k = acts[i + 1]
        if k == "a2f" then ctx:move("aura", "flare", 1, me, me)
        elseif k == "f2a" then ctx:move("flare", "aura", 1, me, me)
        elseif k == "oa2f" then ctx:move("aura", "flare", 1, opp, opp)
        else ctx:move("flare", "aura", 1, opp, opp) end
      end
    end },

  -- 对应。1敌气到自装。本回合的结束阶段，若敌气不多于自气，则1自装到敌气。
  { set = "yatsuha", form = "O", num = 6, name = "契约", kind = "normal", upgrade = "盟誓", type = "action",
    response = true,
    on_play = function(ctx)
      ctx:move("flare", "aura", 1, ctx:opp(), ctx:player())
      ctx:store_int("turn", ctx:turn_number())  -- 只在本回合的结束阶段生效
    end,
    triggers = {
      { event = "end_phase_start", zone = "discard",
        cond = function(ctx, ev)
          return ctx:load_int("turn", -1) == ctx:turn_number() and
                 ctx:flare(ctx:opp()) <= ctx:flare(ctx:player())
        end,
        run = function(ctx, ev)
          ctx:move("aura", "flare", 1, ctx:player(), ctx:opp())
        end },
    } },

  -- 【纳3】破绽。展开时：将 X 个樱花结晶从这张牌上移到虚（X = 镜映）。
  -- 弃置时：进行攻击"【1-4 0/0】攻击后：2敌装到自装"。
  { set = "yatsuha", form = "O", num = 7, name = "寄花", kind = "normal", upgrade = "徒寄花", type = "enhance",
    nagi = 3, breakable = true,
    on_expanded = function(ctx)
      ctx:drain_card_crystals(ctx:source_inst(), ctx:mirror())
    end,
    on_discard = function(ctx)
      ctx:attack {
        range = { 1, 4 }, damage = { aura = 0, life = 0 },
        after = function(c2, a)
          c2:move("aura", "aura", 2, c2:opp(), c2:player())
        end,
      }
    end },

  ---------------------------------------------------------------------------
  -- 切
  ---------------------------------------------------------------------------
  -- 对应。若自命小于敌命，生成被对应攻击的复制并用它对应受到的攻击；
  -- 否则若被对应的攻击不是切牌，打消之。
  { set = "yatsuha", form = "O", num = 1, name = "双叶镜的祟神", kind = "special",
    type = "action", cost = 4, response = true,
    on_play = function(ctx)
      local a = ctx:responding_attack()
      if not a then return end
      local me = ctx:player()
      if ctx:life(me) >= ctx:life(ctx:opp()) then
        if not a:from_special() then a:negate() end
        return
      end
      -- 精确复制（距离/伤害/超克/攻击后效果/女神），用它对应受到的攻击
      a:negate()
      ctx:copy_attack(a)
    end },

  { set = "yatsuha", form = "O", num = 2, name = "四叶镜的童谣", kind = "special",
    type = "action", cost = 2,
    on_play = function(ctx)
      local all = {}
      for _, i in ipairs(ctx:enhances(ctx:player())) do all[#all + 1] = i end
      for _, i in ipairs(ctx:enhances(ctx:opp())) do all[#all + 1] = i end
      local cand = {}
      for _, i in ipairs(all) do
        if ctx:is_normal_card(i) then cand[#cand + 1] = i end  -- 非切牌的付与牌
      end
      if #cand == 0 then return end
      local sel = ctx:choose_cards("四叶镜的童谣：选择一张非切牌的付与牌", cand, 1, 1)
      if #sel == 0 then return end
      local host = sel[1]
      ctx:empty_card(host)  -- 其上的所有樱花结晶移到虚
      if ctx:choose("童谣：要使用这张付与牌吗？", { "是", "否" }) == 1 then
        local full = ctx:is_full_power(host)
        ctx:use_card(host)
        if full then ctx:end_current_main() end  -- 若这张牌是全力牌，终端
      end
    end },

  { set = "yatsuha", form = "O", num = 3, name = "六叶镜的星海", kind = "special",
    type = "attack", cost = 5,
    attack = function(ctx)
      local x = ctx:mirror()
      return { range = { 3, 7 }, damage = { aura = 3 + x, life = 1 + x },
               keywords = { "overwhelm", "no_normal_response" } }
    end },

  -- 【纳5】终端。展开中：你所有要移动樱花结晶的牌都可以反向执行。
  -- 弃置时：将这张牌移出游戏。
  { set = "yatsuha", form = "O", num = 4, name = "八叶镜的映界", kind = "special",
    type = "enhance", cost = 2, nagi = 5, terminal = true, reverse_moves = true,
    on_discard = function(ctx) ctx:remove_card(ctx:source_inst()) end },

  ---------------------------------------------------------------------------
  -- 八重徒寄樱（花）A1：只有 1 张切牌；完全态（升级版）都在这里
  ---------------------------------------------------------------------------
  { set = "yatsuha.A1", form = "A1", num = 901, name = "星辰之利爪", kind = "normal",
    type = "attack", extra = true, complete = true,
    attack = { range = { 3, 4 }, damage = { aura = 2, life = 1 } },
    on_attack_after = function(ctx)
      ctx:move("flare", "aura", 1, ctx:opp(), ctx:player())  -- 1敌气到自装
    end },

  { set = "yatsuha.A1", form = "A1", num = 902, name = "深渊之巨吻", kind = "normal",
    type = "attack", extra = true, complete = true,
    attack = function(ctx)
      local x = ctx:mirror()
      return { range = { 4, 5 }, damage = { aura = 2 + x, life = 1 + x },
               keywords = { "unrespondable" } }
    end },

  { set = "yatsuha.A1", form = "A1", num = 903, name = "现世之魔物", kind = "normal",
    type = "attack", extra = true, complete = true, full_power = true,
    attack = { range = { 1, 3 }, damage = { aura = 4, life = 1 } },
    on_attack_after = function(ctx)
      if ctx:last_attack_side() == 2 then  -- 对手选择受到命伤
        ctx:move("life", "life", 1, ctx:opp(), ctx:player())  -- 1敌命到自命
      end
    end },

  { set = "yatsuha.A1", form = "A1", num = 904, name = "幻影联动", kind = "normal",
    type = "action", extra = true, complete = true,
    on_play = function(ctx)
      local me = ctx:player()
      local d = (ctx:choose("幻影联动：本回合内", { "距离+1，达人距离+1", "距离-1，达人距离-1" }) == 1)
                    and 1 or -1
      ctx:add_temp_distance(me, d)
      ctx:add_temp_near_distance(me, d)
      ctx:attack { range = { 3, 5 }, damage = { aura = 2, life = 1 } }
    end },

  { set = "yatsuha.A1", form = "A1", num = 905, name = "决心", kind = "normal",
    type = "action", extra = true, complete = true, response = true,
    on_play = function(ctx)
      local me = ctx:player()
      local opts, acts = {}, {}
      if ctx:aura(me) >= 1 then opts[#opts + 1], acts[#acts + 1] = "1自装到自气", "a2f" end
      if ctx:flare(me) >= 1 then opts[#opts + 1], acts[#acts + 1] = "1自气到自装", "f2a" end
      if #opts > 0 then
        local sel = ctx:choose_options("决心：选择至少一项", opts, 1, #opts)
        for _, i in ipairs(sel) do
          if acts[i + 1] == "a2f" then ctx:move("aura", "flare", 1, me, me)
          else ctx:move("flare", "aura", 1, me, me) end
        end
      end
      -- 被对应的攻击非切牌且对装伤害 <= X+1，则打消该攻击
      local a = ctx:responding_attack()
      if a and not a:from_special() and (a:aura_damage() or 0) <= ctx:mirror() + 1 then
        a:negate()
      end
    end },

  { set = "yatsuha.A1", form = "A1", num = 906, name = "盟誓", kind = "normal",
    type = "action", extra = true, complete = true, response = true,
    on_play = function(ctx)
      local me, opp = ctx:player(), ctx:opp()
      local opts, acts = {}, {}
      if ctx:aura(opp) >= 1 then opts[#opts + 1], acts[#acts + 1] = "1敌装到自气", "oa2f" end
      if ctx:flare(me) >= 1 then opts[#opts + 1], acts[#acts + 1] = "1自气到敌装", "f2oa" end
      if ctx:flare(opp) >= 1 then opts[#opts + 1], acts[#acts + 1] = "1敌气到自装", "of2a" end
      if ctx:aura(me) >= 1 then opts[#opts + 1], acts[#acts + 1] = "1自装到敌气", "a2of" end
      if #opts == 0 then return end
      local sel = ctx:choose_options("盟誓：选择一项", opts, 1, 1)
      for _, i in ipairs(sel) do
        local k = acts[i + 1]
        if k == "oa2f" then ctx:move("aura", "flare", 1, opp, me)
        elseif k == "f2oa" then ctx:move("flare", "aura", 1, me, opp)
        elseif k == "of2a" then ctx:move("flare", "aura", 1, opp, me)
        else ctx:move("aura", "flare", 1, me, opp) end
      end
    end },

  -- 【纳3】弃置时：完全态的牌 <= 3 则从手牌或弃牌堆升级；否则置牌库底并把 2 敌命移到游戏外。
  { set = "yatsuha.A1", form = "A1", num = 907, name = "徒寄花", kind = "normal",
    type = "enhance", extra = true, complete = true, nagi = 3,
    on_discard = function(ctx)
      local me = ctx:player()
      if ctx:count_complete(me) <= 3 then
        upgrade_from_hand_or_discard(ctx, "徒寄花：选择一张八叶的牌升级")
      else
        ctx:to_deck_bottom(ctx:source_inst())
        ctx:to_external(ctx:opp(), "life", 2)  -- 2敌命移到游戏外
      end
    end },

  -- 终端。限制距离 0-7（打出时当前距必须 ∈[0,7]）。使用时&使用后：使用这张牌时或重铸牌库时，可以升级一张八叶的牌，然后畏缩。
  { set = "yatsuha.A1", form = "A1", num = 1, name = "八叶镜陨茕樱", kind = "special",
    type = "action", cost = 1, solo_specials = true, terminal = true,
    limit_distance = { 0, 7 },
    on_play = function(ctx)
      upgrade_from_hand_or_discard(ctx, "八叶镜陨茕樱：选择一张八叶的牌升级")
      ctx:cower(ctx:player())
    end,
    triggers = {
      { event = "rebuilt",
        cond = function(ctx, ev) return ev:subject() == ctx:player() end,
        run = function(ctx, ev)
          upgrade_from_hand_or_discard(ctx, "八叶镜陨茕樱（洗牌）：选择一张八叶的牌升级")
          ctx:cower(ctx:player())
        end },
    } },


  ---------------------------------------------------------------------------
  -- 魂 AA1（第三个形态；只有 1 张切牌）
  ---------------------------------------------------------------------------
  -- 【纳1】展开时：若是本局打出的第一张牌，则改为从游戏外获取 1 片结晶放在此牌上。
  -- 弃置时：前进 2 次，以未使用状态获得切牌"千芳犹增色"，然后移出游戏。
  { set = "yatsuha.AA1", form = "AA1", num = 7, name = "万叶仍未识", kind = "normal",
    type = "enhance",
    nagi = function(ctx)
      -- 第一张牌则不从虚/装取献
      return ctx:cards_played_total(ctx:player()) == 1 and 0 or 1
    end,
    on_expanded = function(ctx)
      if ctx:cards_played_total(ctx:player()) == 1 then
        ctx:external_to_card(ctx:source_inst(), 1)  -- 从游戏外获取一片结晶
      end
    end,
    on_discard = function(ctx)
      local me = ctx:player()
      local self = ctx:source_inst()
      ctx:do_basic(me, "advance")
      ctx:do_basic(me, "advance")
      ctx:gain_extra("千芳犹增色")   -- 以未使用状态加入切牌区
      ctx:remove_card(self)
    end },

  -- 【2】开启散樱代的旅途。1虚到这张牌上。
  { set = "yatsuha.AA1", form = "AA1", num = 921, name = "千芳犹增色", kind = "special",
    type = "action", cost = 2, extra = true, complete = true,
    on_play = function(ctx)
      local self = ctx:source_inst()
      ctx:move_to_card("dust", self, 1)
      local t = ctx:gain_extra("散樱代的旅途")
      if t >= 0 then
        ctx:set_used(t)
        ctx:reuse_special(t)  -- 开启旅途
      end
    end },

  -- 旅途：四项奖励，每回合自动推进一项；第二次回到起点时结算终幕。
  { set = "yatsuha.AA1", form = "AA1", num = 922, name = "散樱代的旅途", kind = "special",
    type = "action", cost = 0, extra = true,
    on_play = function(ctx)
      local idx = ctx:choose("散樱代的旅途：选择起点奖励", traveler_rewards()) - 1
      ctx:store_int("stage", idx)
      ctx:store_int("k", 1)
      traveler_grant(ctx, idx)
    end,
    triggers = {
      { event = "turn_start",  -- 双方每个回合都推进一项
        cond = function(ctx, ev) return ctx:is_used(ctx:source_inst()) end,
        run = function(ctx, ev)
          local self = ctx:source_inst()
          local k = ctx:load_int("k", 1)
          local stage = ctx:load_int("stage", 0)
          k = k + 1
          if k >= 10 then  -- 第二次回到起点
            traveler_finale(ctx, self)
            return
          end
          ctx:store_int("k", k)
          traveler_grant(ctx, (stage + k - 1) % 4)
        end },
    } },

  -- 此心所念之神与魂：【4】升级一张八叶的牌；使用后可将"对应"替换为弃一张非八叶牌来打消攻击。
  { set = "yatsuha.AA1", form = "AA1", num = 1, name = "此心所念之神与魂", kind = "special",
    type = "action", cost = 4, solo_specials = true,
    on_play = function(ctx)
      local me = ctx:player()
      local up = upgrade_from_hand_or_discard(ctx, "此心所念之神与魂：选择一张八叶的牌升级")
      if up >= 0 and ctx:choose("将升级后的牌放到牌堆顶？", { "是", "否" }) == 1 then
        ctx:to_deck_top(up)
      end
    end,
    triggers = {
      -- 使用后（本局限一次）：代替"对应"，弃一张非八叶牌并打消受到的一次攻击。
      { event = "attack_declared",
        cond = function(ctx, ev)
          if ev:subject() ~= ctx:opp() or not ctx:is_used(ctx:source_inst()) then return false end
          for _, i in ipairs(ctx:hand(ctx:player())) do
            if not ctx:card_is_goddess(i, "yatsuha") then return true end
          end
          return false
        end,
        run = function(ctx, ev)
          local a = ev:attack()
          if not a then return end
          if ctx:choose("此心所念之神与魂：代替对应，弃一张非八叶牌打消这次攻击？",
                        { "是", "否" }) ~= 1 then
            return
          end
          local me = ctx:player()
          local cand = {}
          for _, i in ipairs(ctx:hand(me)) do
            if not ctx:card_is_goddess(i, "yatsuha") then cand[#cand + 1] = i end
          end
          local sel = ctx:choose_cards("选择弃置一张非八叶的牌", cand, 1, 1)
          if #sel == 0 then return end
          ctx:discard_card(sel[1])
          ctx:empty_card(ctx:source_inst())
          ctx:remove_card(ctx:source_inst())
          a:negate()
        end },
    } },

  -- 此目所及之物与世：【0】回忆区的使用后三件套。
  { set = "yatsuha.AA1", form = "AA1", num = 923, name = "此目所及之物与世", kind = "special",
    type = "action", cost = 0, extra = true,
    memory_draw = true, memory_rebuild_shield = true,
    on_play = function(ctx)
      local me = ctx:player()
      local mem = ctx:memory_of(me)
      if #mem == 0 then return end
      local sel = ctx:choose_cards("此目所及之物与世：从回忆区选择一张牌加入手牌", mem, 1, 1)
      for _, i in ipairs(sel) do ctx:memory_draw(me, 1) end
    end },

}
