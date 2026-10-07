-- 14-Honoka 仄佳
-- 【象征武器】旗（O）、勾玉（A1）。
-- 机制 绽放：打出后把这张牌移出游戏（追加区），换成指定的追加牌（EX）。
-- 机制 光辉：虚 <= 5 时一部分牌强化（仄佳只把它当条件用）。
-- 机制 灰尘：虚 >= 12（沿用虚路的定义）。
--
-- 追加牌不进入构筑（extra = true）。绽放用本文件的 bloom() 统一处理：
-- 移除自身 → gain_extra(追加牌) → 视需要放置到抽牌堆底/弃牌堆。
-- 四季轮回的「使用后」在每次追加牌离场（= gain_extra）时询问是否重置；
-- 引擎没有“从追加区移动到其他区域”的事件，故由 bloom 统一代触发（见文件末尾说明）。

-- 光辉：虚 <= 5
local function kouki(ctx) return ctx:dust() <= 5 end
-- 灰尘：虚 >= 12
local function haibokuri(ctx) return ctx:dust() >= 12 end

local BASIC_LABEL = { advance = "前进", retreat = "后退", aura = "装附",
                      flare = "聚气", escape = "离脱" }
local function basic_label(n) return BASIC_LABEL[n] or n end

-- 绽放。opt = true 时先问“是否移除这张牌”。
-- dest: "choose"（抽牌堆底/弃牌堆/留在切牌区，由玩家选）
--       "discard" / "deck_bottom" / "special"（未使用状态留在切牌区）
-- 返回值：-1 = 未绽放；0 = 已绽放但留在切牌区；1 = 已绽放且放到抽牌堆底或弃牌堆。
local function bloom(ctx, name, dest, opt)
  if opt then
    if ctx:choose("绽放～移除这张牌，获得「" .. name .. "」？", { "是", "否" }) ~= 1 then
      return -1
    end
  end
  local self = ctx:source_inst()
  ctx:remove_card(self)
  local e = ctx:gain_extra(name)
  if e < 0 then return -1 end
  if dest == "discard" then
    ctx:discard_card(e)
    return 1
  end
  if dest == "deck_bottom" then
    ctx:to_deck_bottom(e)
    return 1
  end
  if dest == "choose" then
    -- 文本只给两个去处：抽牌堆底 或 弃牌堆。
    local c = ctx:choose("将「" .. name .. "」放到哪里？", { "抽牌堆底", "弃牌堆" })
    if c == 1 then
      ctx:to_deck_bottom(e)
    else
      ctx:discard_card(e)
    end
    return 1
  end
  return 0
end

return {

  ---------------------------------------------------------------------------
  -- 本格 O 旗（7 常规 + 4 切札）
  ---------------------------------------------------------------------------

  -- 精灵式 → 守护灵式
  { set = "honoka", form = "O", num = 1, name = "精灵式", kind = "normal", type = "attack",
    attack = { range = { 2, 8 }, damage = { aura = 1, life = 1 }, keywords = { "unrespondable" } },
    on_attack_after = function(ctx)
      bloom(ctx, "守护灵式", "choose", true)
    end },

  -- 樱吹雪：对手选择 1距到自装 或 1敌装到距
  { set = "honoka", form = "O", num = 2, name = "樱吹雪", kind = "normal", type = "attack",
    attack = { range = { 3, 5 }, damage = { aura = 2, life = 1 } },
    on_attack_after = function(ctx)
      local me, opp = ctx:player(), ctx:opp()
      local opts, acts = {}, {}
      if ctx:distance() > 0 then
        opts[#opts + 1], acts[#acts + 1] = "1距到自装", 1
      end
      if ctx:aura(me) > 0 then
        opts[#opts + 1], acts[#acts + 1] = "1敌装到距", 2
      end
      if #opts == 0 then return end
      local pick = ctx:choose_for(opp, "樱吹雪：选择一项", opts)
      if pick < 0 or pick >= #acts then return end
      if acts[pick + 1] == 1 then
        ctx:move("distance", "aura", 1, opp, opp)
      else
        ctx:move("aura", "distance", 1, me, me)
      end
    end },

  -- 义旗共振：抽1 / 一张手牌到牌库底 / 此牌到牌库底（皆可选）
  { set = "honoka", form = "O", num = 3, name = "义旗共振", kind = "normal", type = "attack",
    full_power = true,
    attack = { range = { 2, 9 }, damage = { aura = 2, life = 2 } },
    on_attack_after = function(ctx)
      local me = ctx:player()
      if ctx:choose("义旗共振：抽一张牌？", { "是", "否" }) == 1 then
        ctx:draw(me, 1)
      end
      local hand = ctx:hand(me)
      if #hand > 0 then
        local sel = ctx:choose_cards("义旗共振：将一张手牌放到抽牌堆底（可不选）", hand, 0, 1)
        for _, i in ipairs(sel) do ctx:to_deck_bottom(i) end
      end
      if ctx:choose("义旗共振：将这张牌放到抽牌堆底？", { "是", "否" }) == 1 then
        ctx:to_deck_bottom(ctx:source_inst())
      end
    end },

  -- 樱飞翅 ⇄ 再生（再生直接进弃牌堆）
  { set = "honoka", form = "O", num = 4, name = "樱飞翅", kind = "normal", type = "action",
    on_play = function(ctx)
      ctx:move("distance", "dust", 2)
      bloom(ctx, "再生", "discard", false)
    end },

  -- 樱花护符：对应；盖伏一张牌以打消被对应的非王牌攻击
  { set = "honoka", form = "O", num = 5, name = "樱花护符", kind = "normal", type = "action",
    response = true,
    on_play = function(ctx)
      local me = ctx:player()
      local a = ctx:responding_attack()
      if a ~= nil and not a:from_special() then
        local hand = ctx:hand(me)
        if #hand > 0 and
           ctx:choose("樱花护符：盖伏一张牌，打消被对应的非王牌攻击？", { "是", "否" }) == 1 then
          local sel = ctx:choose_cards("樱花护符：选择盖伏的手牌", hand, 1, 1)
          for _, i in ipairs(sel) do ctx:cover_card(i) end
          a:negate()
        end
      end
      bloom(ctx, "暗淡的光辉", "choose", false)
    end },

  -- 指挥：展开中，你的回合结束时进行攻击
  { set = "honoka", form = "O", num = 6, name = "指挥", kind = "normal", type = "enhance",
    nagi = 3,
    triggers = {
      { event = "turn_end",
        cond = function(ctx, ev) return ev:subject() == ctx:player() end,
        run = function(ctx, ev)
          ctx:attack { range = { 1, 5 }, damage = { aura = 1, life = 1 },
                       keywords = { "unrespondable" } }
        end },
    } },

  -- 追轻风：展开中，你的攻击距离扩大（远1）
  { set = "honoka", form = "O", num = 7, name = "追轻风", kind = "normal", type = "enhance",
    nagi = 3,
    continuous = {
      { when = "expanded", query = "attack",
        apply = function(ctx, atk)
          if atk:attacker() == ctx:player() then atk:extend_far(1) end
        end },
    } },

  -- 心胸所念 → 双掌生花
  { set = "honoka", form = "O", num = 1, name = "心胸所念", kind = "special", type = "action",
    cost = 5,
    on_play = function(ctx) bloom(ctx, "双掌生花", "special", false) end },

  -- 在此旗的名义之下：把承伤结晶**直接改放到**选定的付与牌上（伤害结算的第一个原子操作）。
  { set = "honoka", form = "O", num = 2, name = "在此旗的名义之下", kind = "special",
    type = "attack", cost = 4,
    attack = { range = { 3, 7 }, damage = { aura = 3, life = 2 } },
    on_play = function(ctx)
      local enh = {}
      for _, i in ipairs(ctx:enhances(ctx:player())) do enh[#enh + 1] = i end
      if #enh == 0 then return end
      local sel = ctx:choose_cards("在此旗的名义之下：选择一张你展开中的付与牌", enh, 1, 1)
      if #sel == 0 then return end
      if ctx:choose("若此牌造成伤害，是否改为把结晶移到该付与牌上？", { "是", "否" }) == 1 then
        ctx:redirect_damage_to_card(sel[1])
      end
    end },

  -- 四季轮回：对应。三个可选效果；使用后随追加牌离场而重置（见 bloom/kisetsu_reset）
  { set = "honoka", form = "O", num = 3, name = "四季轮回", kind = "special", type = "action",
    cost = 1, response = true,
    triggers = {
      -- 使用后：每当你的牌从追加牌区移出（引擎事件 extra_gained），可以回到未使用状态。
      { event = "extra_gained",
        cond = function(ctx, ev) return ev:subject() == ctx:player() end,
        run = function(ctx, ev)
          if ctx:choose("四季轮回：追加牌已离场，让「四季轮回」回到未使用状态？",
                        { "是", "否" }) == 1 then
            ctx:reset_special(ctx:source_inst())
          end
        end },
    },
    on_play = function(ctx)
      local me = ctx:player()
      -- 1）从盖牌堆选一张置于牌库底（可不选）
      local cover = ctx:cover_cards(me)
      if #cover > 0 then
        local sel = ctx:choose_cards("四季轮回：从盖牌堆选一张置于牌库底（可不选）", cover, 0, 1)
        for _, i in ipairs(sel) do ctx:to_deck_bottom(i) end
      end
      -- 2）抽一张（可选）
      if ctx:choose("四季轮回：抽一张牌？", { "是", "否" }) == 1 then
        ctx:draw(me, 1)
      end
      -- 3）盖伏一张牌并装附1次（可选）
      local hand = ctx:hand(me)
      if #hand > 0 and
         ctx:choose("四季轮回：盖伏一张牌并装附1次？", { "是", "否" }) == 1 then
        local sel = ctx:choose_cards("四季轮回：选择盖伏的手牌", hand, 1, 1)
        for _, i in ipairs(sel) do ctx:cover_card(i) end
        ctx:do_basic(me, "aura")
      end
    end },

  -- 漫天的花道：本牌上的结晶被移除时进入持有者的装（满则入气）
  { set = "honoka", form = "O", num = 4, name = "漫天的花道", kind = "special", type = "enhance",
    cost = 2, nagi = 5, decay_to_owner_aura = true },

  ---------------------------------------------------------------------------
  -- 变格 A1 勾玉（同编号替换 O-N1 / O-S1）
  ---------------------------------------------------------------------------

  -- 樱之双剑 ⇄ 影之两手
  { set = "honoka.A1", form = "A1", num = 1, name = "樱之双剑", kind = "normal", type = "attack",
    attack = { range = { 4, 6 }, damage = { aura = 2, life = 1 } },
    on_attack_after = function(ctx)
      ctx:move("dust", "aura", 1, ctx:player(), ctx:player())
      bloom(ctx, "影之两手", "choose", true)
    end },

  -- 独醒于昏明 → 循迹访清荧（光辉）/ 彷徨独视影
  { set = "honoka.A1", form = "A1", num = 1, name = "独醒于昏明", kind = "special",
    type = "action",
    on_play = function(ctx)
      ctx:move("aura", "aura", 1, ctx:opp(), ctx:player())
      if kouki(ctx) then
        bloom(ctx, "循迹访清荧", "deck_bottom", false)
      else
        bloom(ctx, "彷徨独视影", "deck_bottom", false)
      end
    end },

  ---------------------------------------------------------------------------
  -- 追加牌（EX，不进入构筑）
  ---------------------------------------------------------------------------

  -- 精灵式 → 守护灵式 → 突击灵式 → 神灵奥华
  { set = "honoka", form = "O", num = 901, name = "守护灵式", kind = "normal", type = "attack",
    response = true, extra = true,
    attack = { range = { 2, 3 }, damage = { aura = 2, life = 1 } },
    on_attack_after = function(ctx)
      ctx:move("dust", "aura", 1, ctx:player(), ctx:player())
      bloom(ctx, "突击灵式", "choose", true)
    end },

  { set = "honoka", form = "O", num = 902, name = "突击灵式", kind = "normal", type = "attack",
    extra = true,
    attack = { range = { 5, 5 }, damage = { aura = 3, life = 2 },
               keywords = { "no_normal_response" } },
    on_attack_after = function(ctx)
      local placed = bloom(ctx, "神灵奥华", "choose", true)
      if placed == 1 then
        ctx:move("dust", "life", 1, ctx:player(), ctx:player())
      end
    end },

  { set = "honoka", form = "O", num = 903, name = "神灵奥华", kind = "normal", type = "attack",
    extra = true, full_power = true,
    attack = { range = { 1, 4 }, damage = { aura = 4, life = 3 }, keywords = { "unrespondable" } },
    on_attack_after = function(ctx)
      ctx:move("dust", "aura", 2, ctx:player(), ctx:player())
    end },

  -- 樱飞翅 ⇄ 再生（樱飞翅是常规牌，额外准备一张同名追加牌供“获得”）
  { set = "honoka", form = "O", num = 904, name = "樱飞翅", kind = "normal", type = "action",
    extra = true,
    on_play = function(ctx)
      ctx:move("distance", "dust", 2)
      bloom(ctx, "再生", "discard", false)
    end },

  { set = "honoka", form = "O", num = 905, name = "再生", kind = "normal", type = "action",
    extra = true, full_power = true,
    on_play = function(ctx)
      ctx:move("dust", "aura", 1, ctx:player(), ctx:player())
      ctx:move("dust", "flare", 1, ctx:player(), ctx:player())
      bloom(ctx, "樱飞翅", "discard", false)
    end },

  -- 樱花护符 → 暗淡的光辉
  { set = "honoka", form = "O", num = 906, name = "暗淡的光辉", kind = "normal", type = "attack",
    extra = true,
    attack = { range = { 1, 3 }, damage = { aura = 1, life = 2 } } },

  -- 心胸所念 → 双掌生花 → 新幕来临
  { set = "honoka", form = "O", num = 907, name = "双掌生花", kind = "special", type = "enhance",
    cost = 0, full_power = true, extra = true, absorb_aura_basic = true,
    on_expand = function(ctx)
      -- 打出时的那次装附不替换（引擎在装附时询问是否放到此牌上）。
      ctx:set_aura_redirect_suppressed(true)
      ctx:do_basic(ctx:player(), "aura")
      ctx:set_aura_redirect_suppressed(false)
    end,
    triggers = {
      -- 使用后（1）：你的结束阶段开始时，装附一次
      { event = "end_phase_start",
        cond = function(ctx, ev)
          return ev:subject() == ctx:player() and ctx:is_used(ctx:source_inst())
        end,
        run = function(ctx, ev) ctx:do_basic(ctx:player(), "aura") end },
      -- 使用后（2）：恰好 5 个结晶时绽放
      { event = "basic_aura",
        cond = function(ctx, ev)
          return ev:subject() == ctx:player() and ctx:is_used(ctx:source_inst())
        end,
        run = function(ctx, ev)
          -- 引擎已在装附时把结晶放到这张牌上（absorb_aura_basic）。
          local self = ctx:source_inst()
          if ctx:crystals(self) == 5 then
            local me = ctx:player()
            ctx:move_from_card(self, "flare", 5, me)  -- 牌上所有结晶 → 自气
            bloom(ctx, "新幕来临", "special", false)
          end
        end },
    } },

  -- 新幕来临：使用后，你的结束阶段开始时进行 X/X 攻击
  { set = "honoka", form = "O", num = 908, name = "新幕来临", kind = "special", type = "action",
    cost = 5, extra = true,
    triggers = {
      { event = "end_phase_start",
        cond = function(ctx, ev)
          return ev:subject() == ctx:player() and ctx:is_used(ctx:source_inst())
        end,
        run = function(ctx, ev)
          local x = ctx:areas_with(5)
          ctx:attack { range = { 0, 10 }, damage = { aura = x, life = x },
                       keywords = { "unrespondable" } }
        end },
    } },

  -- 独醒于昏明 → 循迹访清荧 / 彷徨独视影
  -- 这两张是“获得…到抽牌堆底”的追加牌，用 kind="normal" 才能在抽到后从手牌打出
  -- （切札不进入抽牌堆/手牌；引擎对切牌区未使用牌的枚举不看 kind，故置于切牌区时同样可用）。
  { set = "honoka", form = "A1", num = 909, name = "循迹访清荧", kind = "normal",
    type = "action", extra = true,
    on_play = function(ctx)
      local me = ctx:player()
      local used = {}
      for _ = 1, 2 do
        local opts, names = {}, {}
        for _, n in ipairs(ctx:legal_basics(me)) do
          if not used[n] then
            opts[#opts + 1], names[#names + 1] = basic_label(n), n
          end
        end
        if #opts == 0 then break end
        opts[#opts + 1] = "停止"
        local pick = ctx:choose("循迹访清荧：执行基本动作", opts)
        if pick < 1 or pick > #names then break end
        local nm = names[pick]
        ctx:do_basic(me, nm)
        used[nm] = true
      end
      if ctx:dust() == 0 then
        bloom(ctx, "熠熠见繁樱", "special", false)
      else
        bloom(ctx, "觫觫结袂情", "special", false)
      end
    end },

  { set = "honoka", form = "A1", num = 910, name = "彷徨独视影", kind = "normal",
    type = "action", extra = true,
    on_play = function(ctx)
      local opp = ctx:opp()
      local opts, areas = {}, {}
      if ctx:flare(opp) > 0 then
        opts[#opts + 1], areas[#areas + 1] = "1敌气到虚", "flare"
      end
      if ctx:life(opp) > 0 then
        opts[#opts + 1], areas[#areas + 1] = "1敌命到虚", "life"
      end
      if #opts > 0 then
        local pick = ctx:choose_for(opp, "彷徨独视影：选择1樱花结晶移到虚", opts)
        if pick >= 0 and pick < #areas then
          ctx:move(areas[pick + 1], "dust", 1, opp, opp)
        end
      end
      if haibokuri(ctx) then
        bloom(ctx, "踽踽虚路行", "special", false)
      else
        bloom(ctx, "觫觫结袂情", "special", false)
      end
    end },

  -- 熠熠见繁樱：X = 此牌上的结晶数；再起：有区域恰好 5 结晶
  { set = "honoka.A1", form = "A1", num = 911, name = "熠熠见繁樱", kind = "special",
    type = "attack", cost = 1, extra = true, keep_crystals_on_reset = true,
    attack = function(ctx)
      local x = ctx:crystals(ctx:source_inst())
      return { range = { 3, 5 }, damage = { aura = x, life = 2 }, keywords = { "overwhelm" } }
    end,
    on_attack_after = function(ctx)
      ctx:move_to_card("dust", ctx:source_inst(), 1)
    end,
    reset = { kind = "end_turn", cond = function(ctx) return ctx:areas_with(5) >= 1 end } },

  { set = "honoka.A1", form = "A1", num = 912, name = "觫觫结袂情", kind = "special",
    type = "action", cost = 5, extra = true,
    on_play = function(ctx)
      ctx:move("aura", "aura", 5, ctx:opp(), ctx:player())
    end },

  { set = "honoka.A1", form = "A1", num = 913, name = "踽踽虚路行", kind = "special",
    type = "action", cost = 3, extra = true,
    on_play = function(ctx)
      ctx:skip_next_main(ctx:opp())
      ctx:remove_card(ctx:source_inst())
    end },

  -- 樱之双剑 → 影之两手 → 樱之双剑（同名追加牌供“获得”）
  { set = "honoka.A1", form = "A1", num = 914, name = "影之两手", kind = "normal", type = "attack",
    extra = true,
    attack = { range = { 3, 4 }, damage = { aura = 1, life = 0 } },
    on_attack_after = function(ctx)
      local opp = ctx:opp()
      local side = ctx:last_attack_side()
      if side == 1 then
        local hand = ctx:hand(opp)
        if #hand > 0 then
          local sel = ctx:choose_cards("影之两手：检视对手手牌并弃置其中一张", hand, 1, 1)
          for _, i in ipairs(sel) do ctx:discard_card(i) end
        end
      elseif side == 2 then
        ctx:move("flare", "dust", 2, opp, opp)
      end
      bloom(ctx, "樱之双剑", "choose", true)
    end },

  { set = "honoka.A1", form = "A1", num = 915, name = "樱之双剑", kind = "normal", type = "attack",
    extra = true,
    attack = { range = { 4, 6 }, damage = { aura = 2, life = 1 } },
    on_attack_after = function(ctx)
      ctx:move("dust", "aura", 1, ctx:player(), ctx:player())
      bloom(ctx, "影之两手", "choose", true)
    end },

}
