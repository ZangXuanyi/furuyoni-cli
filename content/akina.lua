-- 23-Akina 源上安琪娜
-- 【象征武器】算盘（O）。
-- 【机制】资本 / 股价 / 股市：
--   资本 = 该玩家的装 + 气 + 股市结晶数之和；不控制安琪娜的玩家其股市视作 0。
--   股价初始 2，取值域 [1,4]。敌人的命受到攻击伤害时 +2；自己投资时 +1；
--   自己的命受到攻击伤害时 -1；自己套现时 -2。
--   投资：重铸流程开始时（洗牌之前），或回合结束且本回合内没有套现时，可以把一张
--   「投资券」翻至背面向上并支付投资资金（股价 1 从虚 / 2 自装 / 3 自气 / 4 自命），
--   然后股价 +1。恫吓 / 直接金融（常规牌）从弃牌堆移到盖牌堆；正解（切牌）从已使用
--   重置为未使用。
--   套现：回合开始时，股市至少 1 个结晶：股市 1 个移到虚，然后按股价把 1 个结晶移入
--   自装（1 从虚 / 2 敌装 / 3 敌气 / 4 敌命），最后股价 -2。
-- 引擎侧支持：ctx:market / stock / capital / cash_out / invest / invest_available /
--   set_algorithm / algorithm；区域名 "market"；CardDef.investmentTicket（投资券）、
--   stockCost（费用 = 股价且不受任何修正）、reuseWhileAhead（差列递归强制再使用）、
--   crystalShield（本牌结晶不可被本牌以外方式移除）、cashSubstitute（正解替代套现）、
--   decayTo = "enemy_flare"（乱拨）、on_death 死亡窗口（仙霄鬼泉）。

return {

  ---------------------------------------------------------------------------
  -- 算盘 O 常规牌
  ---------------------------------------------------------------------------
  -- 【1-6 1/0】攻击后：选择一项：获得 1 集中力；套现一次；1 自装到股市。
  { set = "akina", form = "O", num = 1, name = "算盘珠", kind = "normal", type = "attack",
    attack = { range = { 1, 6 }, damage = { aura = 1, life = 0 } },
    on_attack_after = function(ctx)
      local me = ctx:player()
      local opts, keys = {}, {}
      opts[#opts + 1] = "获得 1 集中力"
      keys[#keys + 1] = "vigor"
      if ctx:market(me) >= 1 then  -- 股市为空时不能套现
        opts[#opts + 1] = "套现一次"
        keys[#keys + 1] = "cash"
      end
      opts[#opts + 1] = "1 自装到股市"
      keys[#keys + 1] = "stock"
      local pick = ctx:choose("算盘珠：选择一项", opts)
      local k = keys[pick]
      if k == "vigor" then
        ctx:gain_vigor(me, 1)
      elseif k == "cash" then
        ctx:cash_out(me)
      else
        ctx:move("aura", "market", 1, me, me)
      end
    end },

  -- 【4 -/0】投资券。若你的资本比对手多，此攻击 +0/+1。
  -- 攻击后：若你的资本比对手少，本牌结算完毕后被盖伏。
  { set = "akina", form = "O", num = 2, name = "恫吓", kind = "normal", type = "attack",
    investment_ticket = true,
    attack = { range = { 4, 4 }, damage = { life = 0 } },
    on_play = function(ctx)
      local me = ctx:player()
      if ctx:capital(me) > ctx:capital(ctx:opp()) then
        local src = ctx:source_inst()
        ctx:next_attack_mod {
          match = function(c, atk) return atk:source_inst() == src end,
          apply = function(c, atk) atk:add { life = 1 } end,
          this_turn = true,
        }
      end
    end,
    on_attack_after = function(ctx)
      if ctx:capital(ctx:player()) < ctx:capital(ctx:opp()) then
        ctx:cover_card(ctx:source_inst())
      end
    end },

  -- 【1-5 2/0】终端。攻击后：若你的资本比对手多，可以执行一次基本动作。
  -- 攻击后：若你的资本比对手至少多 3，可以把弃牌堆中一张非安琪娜的牌拿到手中。
  { set = "akina", form = "O", num = 3, name = "交易", kind = "normal", type = "attack",
    terminal = true,
    attack = { range = { 1, 5 }, damage = { aura = 2, life = 0 } },
    on_attack_after = function(ctx)
      local me = ctx:player()
      local opp = ctx:opp()
      -- 两条「攻击后」的条件都在同一时机判定（先锁定，再按顺序结算）。
      local ahead = ctx:capital(me) > ctx:capital(opp)
      local far = ctx:capital(me) >= ctx:capital(opp) + 3
      if ahead then
        ctx:free_basics(me, 1)
      end
      if far then
        local pool = {}
        for _, i in ipairs(ctx:discard_pile(me)) do
          if not ctx:card_is_goddess(i, "akina") then pool[#pool + 1] = i end
        end
        if #pool > 0 then
          local sel = ctx:choose_cards("交易：从弃牌堆取一张非安琪娜的牌", pool, 0, 1)
          for _, i in ipairs(sel) do ctx:to_hand(i) end
        end
      end
    end },

  -- 选择一项：2 自装到股市，或 2 虚到自装。
  { set = "akina", form = "O", num = 4, name = "投机倒把", kind = "normal", type = "action",
    on_play = function(ctx)
      local me = ctx:player()
      local pick = ctx:choose("投机倒把：选择一项", { "2 自装到股市", "2 虚到自装" })
      if pick == 1 then
        ctx:move("aura", "market", 2, me, me)
      else
        ctx:move("dust", "aura", 2, me, me)
      end
    end },

  -- 对应。本回合内，所有攻击获得距离扩大（近1）与距离缩小（远1）。
  { set = "akina", form = "O", num = 5, name = "算法", kind = "normal", type = "action",
    response = true,
    on_play = function(ctx) ctx:set_algorithm() end },

  -- 【纳2】限制距离 0-3。展开时：2 敌气到距。
  -- 展开中：若此牌上的樱花结晶将被移除，将其移至敌气而非虚。
  { set = "akina", form = "O", num = 6, name = "乱拨", kind = "normal", type = "enhance",
    nagi = 2, limit_distance = { 0, 3 }, decay_to = "enemy_flare",
    on_expand = function(ctx) ctx:move("flare", "distance", 2, ctx:opp(), ctx:opp()) end },

  -- 全力【纳2】投资券。破绽。展开时：1 敌装到自装，你可以支付 1 集中力再执行一次。
  -- 弃置时：进行攻击【2-5 1/0】。
  { set = "akina", form = "O", num = 7, name = "直接金融", kind = "normal", type = "enhance",
    nagi = 2, full_power = true, breakable = true, investment_ticket = true,
    on_expand = function(ctx)
      local me = ctx:player()
      ctx:move("aura", "aura", 1, ctx:opp(), me)
      if ctx:vigor(me) >= 1 then
        local pick = ctx:choose("直接金融：支付 1 集中力再执行一次？", { "支付", "不支付" })
        if pick == 1 then
          ctx:cost_vigor(me, 1)
          ctx:move("aura", "aura", 1, ctx:opp(), me)
        end
      end
    end,
    on_discard = function(ctx)
      ctx:attack { range = { 2, 5 }, damage = { aura = 1, life = 0 } }
    end },

  ---------------------------------------------------------------------------
  -- 算盘 O 切札
  ---------------------------------------------------------------------------
  -- 差列递归征税法（股价）：进行攻击"【0-10 -/1】这个攻击被任何牌对应时，打消自身
  -- （无论对手的对应牌是什么）"。若你的资本大于对手，则你必须再使用一次这张牌
  -- （照常支付费用），直到你的资本不大于对手为止。
  { set = "akina", form = "O", num = 1, name = "差列递归征税法", kind = "special",
    type = "attack",
    cost = 0, stock_cost = true, reuse_while_ahead = true,
    attack = { range = { 0, 10 }, damage = { life = 1 } },
    on_play = function(ctx)
      ctx:on_response(function(c, atk) atk:negate() end)
    end },

  -- 大衍算科手打表（0气）：对应【0-10 2/0】攻击后：从自己的气、命、股市中各移动
  -- 一片樱花结晶到自装。
  { set = "akina", form = "O", num = 2, name = "大衍算科手打表", kind = "special",
    type = "attack",
    cost = 0, response = true,
    attack = { range = { 0, 10 }, damage = { aura = 2, life = 0 } },
    on_attack_after = function(ctx)
      local me = ctx:player()
      ctx:move("flare", "aura", 1, me, me)
      ctx:move("life", "aura", 1, me, me)
      ctx:move("market", "aura", 1, me, me)
    end },

  -- 仙霄鬼泉天元术（1气）：【纳1】展开时：4 自命到自气。
  -- 展开中：这张牌上的樱花结晶不能被除了本牌外的任何方式移除。
  -- 当你死亡时：4 自气到自命，然后移除这张牌（其上所有樱花结晶移到虚）。
  { set = "akina", form = "O", num = 3, name = "仙霄鬼泉天元术", kind = "special",
    type = "enhance",
    cost = 1, nagi = 1, crystal_shield = true,
    on_expand = function(ctx) ctx:move("life", "flare", 4, ctx:player(), ctx:player()) end,
    on_death = function(ctx)
      local me = ctx:player()
      local inst = ctx:source_inst()
      ctx:move("flare", "life", 4, me, me)
      ctx:empty_card(inst)   -- 其上所有樱花结晶移到虚
      ctx:remove_card(inst)
    end },

  -- 源上安琪娜的正解（股价）：距离限制 0-7。投资券。
  -- 你可以选择立即套现一次，然后 2 虚到自装，然后将此牌移出游戏。
  -- 使用后：你每回合开始时可以用 1 自装到自气来替代套现操作。
  { set = "akina", form = "O", num = 4, name = "源上安琪娜的正解", kind = "special",
    type = "action",
    cost = 0, stock_cost = true, investment_ticket = true, cash_substitute = true,
    limit_distance = { 0, 7 },
    on_play = function(ctx)
      local me = ctx:player()
      if ctx:market(me) < 1 then return end  -- 不能套现则整个选择不可用
      local pick = ctx:choose("正解：立即套现一次，然后 2 虚到自装，并将此牌移出游戏？",
                              { "执行", "不执行" })
      if pick ~= 1 then return end
      ctx:cash_out(me)
      ctx:move("dust", "aura", 2, me, me)
      ctx:remove_card(ctx:source_inst())
    end },

}
