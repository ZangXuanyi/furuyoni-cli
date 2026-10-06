-- 13-Utsuro 虚路
-- 机制 灰尘：虚 >= 12 时一部分卡强化。
-- O=镰；A1=尘。
-- 【特殊】铳镰组合（03-Himika-O + 13-Utsuro-O）禁用「真红凶弹」——
-- 由 data/combo_bans.json 的禁用组合表处理（见 docs/content-modules.md）。

-- 灰尘：虚 >= 12
local function jin(ctx) return ctx:dust() >= 12 end

-- 让 who 从自己的装/气/命中依次选择共计 total 片结晶移到虚（逐片选择，可重复同一区域）。
local function pick_crystals_to_dust(ctx, who, total, prompt)
  for _ = 1, total do
    local opts, areas = {}, {}
    if ctx:aura(who) > 0 then opts[#opts + 1], areas[#areas + 1] = "装", "aura" end
    if ctx:flare(who) > 0 then opts[#opts + 1], areas[#areas + 1] = "气", "flare" end
    if ctx:life(who) > 0 then opts[#opts + 1], areas[#areas + 1] = "命", "life" end
    if #opts == 0 then return end
    local pick = ctx:choose_for(who, prompt, opts)
    if pick < 0 or pick >= #areas then return end
    ctx:move(areas[pick + 1], "dust", 1, who, who)
  end
end

return {

  ---------------------------------------------------------------------------
  -- 本格 O 镰
  ---------------------------------------------------------------------------
  { set = "utsuro", form = "O", num = 1, name = "圆月", kind = "normal", type = "attack",
    attack = function(ctx)
      if jin(ctx) then
        -- 灰尘：距离扩大（近1），对装伤害变为 -
        return { range = { 4, 7 }, damage = { life = 2 } }
      end
      return { range = { 5, 7 }, damage = { aura = 2, life = 2 } }
    end },

  { set = "utsuro", form = "O", num = 2, name = "黑之波动", kind = "normal", type = "attack",
    attack = { range = { 4, 7 }, damage = { aura = 1, life = 2 } },
    on_attack_after = function(ctx)
      if ctx:last_attack_side() ~= 1 then return end  -- 对手选择以装承伤
      local hand = ctx:hand(ctx:opp())
      if #hand == 0 then return end
      local sel = ctx:choose_cards("黑之波动：检视对手手牌并弃置一张", hand, 1, 1)
      for _, i in ipairs(sel) do ctx:discard_card(i) end
    end },

  { set = "utsuro", form = "O", num = 3, name = "收割", kind = "normal", type = "attack",
    attack = { range = { 4, 4 }, damage = { life = 0 } },
    on_attack_after = function(ctx)
      pick_crystals_to_dust(ctx, ctx:opp(), 2, "收割：选择 2 片樱花结晶移到虚")
      local enh = {}
      for _, i in ipairs(ctx:enhances(ctx:opp())) do enh[#enh + 1] = i end
      if #enh == 0 then return end
      local sel = ctx:choose_cards("收割：选择对手一张展开中的付与牌（可不选）", enh, 0, 1)
      for _, i in ipairs(sel) do ctx:drain_card_crystals(i, 2) end
    end },

  { set = "utsuro", form = "O", num = 4, name = "重压", kind = "normal", type = "action",
    on_play = function(ctx)
      pick_crystals_to_dust(ctx, ctx:opp(), 1, "重压：选择 1 片樱花结晶移到虚")
      if jin(ctx) then ctx:cower(ctx:opp()) end
    end },

  { set = "utsuro", form = "O", num = 5, name = "影飞翅", kind = "normal", type = "action",
    on_play = function(ctx)
      -- 直到回合结束：当前距离 +2，达人距离 +2
      ctx:add_temp_distance(ctx:player(), 2)
      ctx:add_temp_near_distance(ctx:player(), 2)
    end },

  { set = "utsuro", form = "O", num = 6, name = "影之壁", kind = "normal", type = "action",
    response = true,
    on_play = function(ctx)
      local a = ctx:responding_attack()
      if a then a:add { life = -1 } end
    end },

  { set = "utsuro", form = "O", num = 7, name = "遗灰咒", kind = "normal", type = "enhance",
    nagi = 2, full_power = true,
    on_enter = function(ctx) ctx:move("aura", "dust", 3, ctx:opp(), ctx:opp()) end,
    on_discard = function(ctx)
      if not jin(ctx) then return end
      local opp = ctx:opp()
      ctx:move("dust", "aura", 2, opp, opp)
      ctx:move("life", "dust", 1, opp, opp)
    end },

  -- 切
  { set = "utsuro", form = "O", num = 1, name = "灰灭", kind = "special", type = "action",
    cost = function(ctx) return math.max(0, 24 - ctx:dust()) end,  -- 24-X, X=当前虚
    on_play = function(ctx) ctx:move("life", "dust", 3, ctx:opp(), ctx:opp()) end },

  { set = "utsuro", form = "O", num = 2, name = "虚伪", kind = "special", type = "enhance",
    cost = 3, nagi = 3, enemy_nagi_mod = -1,
    continuous = {
      { when = "expanded", query = "attack",
        apply = function(ctx, atk)
          if atk:attacker() == ctx:opp() then atk:shrink_near(1) end  -- 对手攻击距离 -1（近端）
        end },
    } },

  { set = "utsuro", form = "O", num = 3, name = "终末", kind = "special", type = "enhance",
    cost = 3, nagi = 3,
    triggers = {
      { event = "attack_resolved",
        cond = function(ctx, ev)
          -- 当你因被攻击受到了 >=1 点伤害（落空/被打消的攻击不会误触发）
          return ev:subject() == ctx:opp() and ctx:last_attack_side() ~= 0 and
                 ctx:last_attack_amount() >= 1
        end,
        run = function(ctx, ev) ctx:empty_card(ctx:source_inst()) end },
    },
    on_discard = function(ctx) ctx:end_current_main() end,  -- 结束当前阶段
    reset = { kind = "end_turn", cond = function(ctx) return jin(ctx) end } },

  { set = "utsuro", form = "O", num = 4, name = "魔食", kind = "special", type = "action",
    cost = 4,
    triggers = {
      { event = "turn_start",
        cond = function(ctx, ev) return ev:subject() == ctx:player() end,
        run = function(ctx, ev)
          local opp = ctx:opp()
          local pick = ctx:choose_for(opp, "魔食：1 装到虚，或 2 气到虚", { "1装到虚", "2气到虚" })
          if pick == 0 then
            ctx:move("aura", "dust", 1, opp, opp)
          else
            ctx:move("flare", "dust", 2, opp, opp)
          end
        end },
    } },

  ---------------------------------------------------------------------------
  -- 变格 A1 尘
  ---------------------------------------------------------------------------
  { set = "utsuro.A1", form = "A1", num = 2, name = "侵蚀之尘", kind = "normal", type = "attack",
    attack = { range = { 3, 6 }, damage = { aura = 2, life = 0 } },
    on_attack_after = function(ctx)
      if ctx:last_attack_side() == 2 then  -- 对手选择以命承伤
        ctx:move("flare", "dust", 2, ctx:opp(), ctx:opp())
      end
    end },

  { set = "utsuro.A1", form = "A1", num = 1, name = "虚路的残响装置", kind = "special",
    type = "attack", cost = 2, terminal = true,
    attack = { range = { 3, 10 }, damage = { aura = 2, life = 1 } },
    triggers = {
      { event = "end_phase_start",
        cond = function(ctx, ev) return ctx:dust() >= 13 end,  -- 任意一方的结束阶段
        run = function(ctx, ev)
          local me = ctx:player()
          local self = ctx:source_inst()
          ctx:remove_all_normals(me)
          local wish = ctx:gain_extra("夙愿")
          if wish >= 0 then
            ctx:set_used(wish)
            ctx:reuse_special(wish)  -- 立即免费使用
          end
          for _, n in ipairs({ "万象乖离残灭之影", "我等亡殁静寂往灭", "终焉降临" }) do
            local ex = ctx:gain_extra(n)
            if ex >= 0 then ctx:to_deck_top(ex) end  -- 到抽牌堆后一起重铸
          end
          ctx:rebuild(me, false)  -- 重铸牌库（不受命伤）
          ctx:draw(me, 1)
          if ctx:life(me) > 5 then
            ctx:move("life", "dust", ctx:life(me) - 5, me, me)
          end
          ctx:remove_card(self)
        end },
    } },

  { set = "utsuro.A1", form = "A1", num = 901, name = "夙愿", kind = "special",
    type = "action", cost = 6, extra = true,
    damage_immune = true,  -- 使用后：你不会受到任何伤害
    reset = { kind = "immediate", on = "main_start" } },  -- 即再起：你的主要阶段开始时

  { set = "utsuro.A1", form = "A1", num = 911, name = "万象乖离残灭之影", kind = "normal",
    type = "attack", extra = true, full_power = true,
    attack = { range = { 0, 3 }, damage = { life = 0 }, keywords = { "unrespondable" } },
    on_attack_after = function(ctx)
      pick_crystals_to_dust(ctx, ctx:opp(), 6, "万象乖离残灭之影：选择 6 片樱花结晶移到虚")
    end },

  { set = "utsuro.A1", form = "A1", num = 912, name = "我等亡殁静寂往灭", kind = "normal",
    type = "action", extra = true, full_power = true,
    on_play = function(ctx)
      -- 至多五次非前进的基本动作，然后三次攻击
      ctx:free_basics_of(ctx:player(), 5, { "retreat", "aura", "flare", "escape" })
      ctx:attack { range = { 4, 10 }, damage = { aura = 3, life = 2 } }
      ctx:attack { range = { 5, 10 }, damage = { aura = 1, life = 1 } }
      ctx:attack { range = { 6, 10 }, damage = { aura = 1, life = 1 } }
    end },

  { set = "utsuro.A1", form = "A1", num = 913, name = "终焉降临", kind = "normal",
    type = "enhance", extra = true, nagi = 2,
    on_discard = function(ctx)
      local opp = ctx:opp()
      ctx:discard_all_hand(opp)
      ctx:discard_deck(opp)
      ctx:set_vigor(opp, 0)
      ctx:cower(opp)
    end },

}
