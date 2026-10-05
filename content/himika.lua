-- 03-Himika 绯弥香  【机制：连射 ~ 你同一回合打出的第三张牌及以后强化】
-- Phase 2 只实现本格 O；变格 A1(炎) 留待 Phase 3。

return {

  -- 常
  { set = "himika", form = "O", num = 1, name = "射击", kind = "normal", type = "attack",
    attack = { range = {4, 10}, damage = { aura = 2, life = 1 } } },

  { set = "himika", form = "O", num = 2, name = "连射", kind = "normal", type = "attack",
    attack = function(ctx)
      local a, l = 2, 1
      if ctx:rensha(ctx:player()) then a, l = a + 1, l + 1 end
      return { range = {6, 8}, damage = { aura = a, life = l } }
    end },

  { set = "himika", form = "O", num = 3, name = "加农炮", kind = "normal", type = "attack",
    attack = { range = {5, 8}, damage = { aura = 3, life = 2 } },
    on_attack_after = function(ctx) ctx:lose_life(ctx:opp(), 1, "dust") end },

  -- 两侧伤害：装伤与命伤同时结算。
  { set = "himika", form = "O", num = 4, name = "完全爆破", kind = "normal", type = "attack",
    full_power = true,
    attack = { range = {5, 9}, damage = { aura = 3, life = 1 }, keywords = { "both_sides" } } },

  { set = "himika", form = "O", num = 5, name = "后跳", kind = "normal", type = "action",
    on_play = function(ctx)
      ctx:draw(ctx:player(), 1)
      ctx:move("dust", "distance", 1)
    end },

  { set = "himika", form = "O", num = 6, name = "回燃", kind = "normal", type = "action",
    on_play = function(ctx)
      ctx:cower(ctx:opp())
      if ctx:rensha(ctx:player()) then
        ctx:next_attack_mod {
          this_turn = false,
          match = function(ctx2, atk)
            local ad = atk:aura_damage()
            return atk:source_goddess() ~= "himika" and (ad == nil or ad < 3)
          end,
          apply = function(ctx2, atk) atk:add { aura = 1, life = 1 } end,
        }
      end
    end },

  -- 所有改变【距】的牌效无效（基本动作不受影响）。
  { set = "himika", form = "O", num = 7, name = "迷烟", kind = "normal", type = "enhance",
    nagi = 3, lock_distance = true },

  -- 切
  { set = "himika", form = "O", num = 1, name = "真红凶弹", kind = "special", type = "attack",
    cost = 0,
    attack = { range = {5, 10}, damage = { aura = 3, life = 1 } } },

  { set = "himika", form = "O", num = 2, name = "绯红零时", kind = "special", type = "attack",
    cost = 5,
    attack = function(ctx)
      local kw = { "both_sides" }
      if ctx:distance() == 0 then kw[#kw + 1] = "unrespondable" end
      return { range = {0, 2}, damage = { aura = 2, life = 2 }, keywords = kw }
    end },

  { set = "himika", form = "O", num = 3, name = "猩红狂想", kind = "special", type = "action",
    cost = 3,
    on_play = function(ctx)
      ctx:draw(ctx:player(), 2)
      local hand = ctx:hand(ctx:player())
      if #hand > 0 then
        local sel = ctx:choose_cards("弃 1 张牌", hand, 1, 1)
        for _, inst in ipairs(sel) do ctx:discard_card(inst) end
      end
    end },

  { set = "himika", form = "O", num = 4, name = "真红领域", kind = "special", type = "action",
    cost = 2,
    on_play = function(ctx)
      if ctx:rensha(ctx:player()) then ctx:move("dust", "distance", 2) end
    end,
    reset = { kind = "end_turn", cond = function(ctx) return ctx:hand_size(ctx:player()) == 0 end } },

}
