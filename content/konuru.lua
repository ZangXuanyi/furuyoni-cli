-- 15-Konuru 凝努
-- 机制 冻结：把"蓝色冰晶"置入对方的装。冰晶不视作装、不能承伤，但占位置（与樱花结晶共用上限）。
--   若角色被冻结（冰晶 ≥ 1），其"聚气"的效果改为移除冰晶。
-- O=橇；A1=试炼。

-- 装是否已被填满（樱花结晶 + 冰晶占满）
local function full(ctx, who) return ctx:armor_full(who) end

return {

  ---------------------------------------------------------------------------
  -- 本格 O 橇
  ---------------------------------------------------------------------------
  { set = "konuru", form = "O", num = 1, name = "雪刃", kind = "normal", type = "attack",
    attack = { range = { 3, 4 }, damage = { aura = 1, life = 1 } },
    on_attack_after = function(ctx) ctx:freeze(ctx:opp(), 1) end },

  -- 若本攻击被对应，则每一张对应该攻击的牌结算完毕后，2虚到距 或 2距到虚。
  { set = "konuru", form = "O", num = 2, name = "旋回刃", kind = "normal", type = "attack",
    attack = { range = { 2, 3 }, damage = { aura = 2, life = 2 } },
    on_play = function(ctx)
      ctx:on_response(function(ctx2, atk)
        local me = ctx2:player()
        if ctx2:choose("旋回刃：2虚到距 或 2距到虚", { "2虚到距", "2距到虚" }) == 1 then
          ctx2:move("dust", "distance", 2, me, me)
        else
          ctx2:move("distance", "dust", 2, me, me)
        end
      end)
    end },

  { set = "konuru", form = "O", num = 3, name = "剑舞", kind = "normal", type = "attack",
    attack = function(ctx)
      if full(ctx, ctx:opp()) then
        return { range = { 4, 5 }, damage = { aura = 3, life = 2 } }
      end
      return { range = { 4, 5 }, damage = { aura = 2, life = 1 } }
    end },

  { set = "konuru", form = "O", num = 4, name = "渡雪", kind = "normal", type = "action",
    on_play = function(ctx)
      local me = ctx:player()
      if ctx:choose("渡雪：1距到虚 或 1虚到距", { "1距到虚", "1虚到距" }) == 1 then
        ctx:move("distance", "dust", 1)
      else
        ctx:move("dust", "distance", 1)
      end
      if full(ctx, ctx:opp()) then
        if ctx:choose("渡雪：敌装满，再进行一次 1虚到距？", { "是", "否" }) == 1 then
          ctx:move("dust", "distance", 1)
        end
      end
    end },

  -- 对应。装附1次；若敌装中至少 3 个冰晶，再装附1次。全开：先进行一次攻击。
  { set = "konuru", form = "O", num = 5, name = "绝对零度", kind = "normal", type = "action",
    response = true, zenkai = true,
    on_play = function(ctx)
      local me = ctx:player()
      if ctx:zenkai() then
        ctx:attack {
          range = { 2, 5 }, damage = { aura = 1, life = 2 },
          after = function(c2, a)
            -- 不断冻结对手，直到敌装中没有空位
            while c2:freeze(c2:opp(), 1) > 0 do end
          end,
        }
      end
      ctx:do_basic(me, "aura")
      if ctx:ice(ctx:opp()) >= 3 then ctx:do_basic(me, "aura") end
    end },

  { set = "konuru", form = "O", num = 6, name = "冻僵", kind = "normal", type = "enhance",
    nagi = 2, enemy_no_flare = true,
    on_enter = function(ctx) ctx:freeze(ctx:opp(), 1) end },

  { set = "konuru", form = "O", num = 7, name = "寒冰荆棘", kind = "normal", type = "enhance",
    nagi = 2, may_skip_crystal_loss = true,
    continuous = {
      { when = "expanded", query = "attack",
        apply = function(ctx, atk)
          -- 你每回合进行的第一个非王牌攻击 +1/+1
          if atk:attacker() == ctx:player() and not atk:from_special() and
             ctx:attacks_this_turn(ctx:player()) == 0 then
            atk:add { aura = 1, life = 1 }
          end
        end },
    } },

  -- 切
  { set = "konuru", form = "O", num = 1, name = "飞雹式", kind = "special", type = "attack",
    cost = 4,
    attack = { range = { 2, 3 }, damage = { aura = 2, life = 3 } },
    on_attack_after = function(ctx)
      if ctx:last_attack_side() ~= 1 then return end  -- 若对手用装承伤
      while ctx:freeze(ctx:opp(), 1) > 0 do end       -- 不断冻结直到没有空位
    end },

  { set = "konuru", form = "O", num = 2, name = "白风式", kind = "special", type = "action",
    cost = 2, response = true,
    on_play = function(ctx)
      if full(ctx, ctx:opp()) then
        local a = ctx:responding_attack()
        if a then a:negate() end
      else
        ctx:move("distance", "aura", 1, ctx:opp(), ctx:opp())
      end
    end },

  { set = "konuru", form = "O", num = 3, name = "吹雪式", kind = "special", type = "attack",
    cost = 0,
    attack = { range = { 3, 6 }, damage = { aura = 0 } },
    on_attack_after = function(ctx)
      -- 本牌的冻结不能触发下面的即再起（把自身作为 cause 传出）。
      ctx:freeze(ctx:opp(), 1, ctx:source_inst())
    end,
    triggers = {
      -- 即再起：敌装变满时
      { event = "armor_full",
        cond = function(ctx, ev)
          return ev:subject() == ctx:opp() and ev:card() ~= ctx:source_inst()
        end,
        run = function(ctx, ev) ctx:reset_special(ctx:source_inst()) end },
    } },

  -- 【纳1】可作为对应（对应时花费改为 4 气）。展开时：1距到虚，冻结对手1次。
  -- 弃置时：可以支付费用再次使用之，并将 4 片樱花结晶从虚移到这张牌上。
  { set = "konuru", form = "O", num = 4, name = "冥沼式", kind = "normal", type = "enhance",
    nagi = 1,
    respond = function(ctx) return ctx:flare(ctx:player()) >= 4 end,
    on_play = function(ctx)
      if ctx:responding_attack() ~= nil then  -- 作为对应打出：支付 4 气
        ctx:move("flare", "dust", 4, ctx:player(), ctx:player())
      end
    end,
    on_enter = function(ctx)
      ctx:move("distance", "dust", 1)
      ctx:freeze(ctx:opp(), 1)
    end,
    on_discard = function(ctx)
      local me = ctx:player()
      local self = ctx:source_inst()
      if ctx:flare(me) < 2 then return end            -- 费用：2 气
      if ctx:choose("冥沼式：支付2气再次使用？", { "是", "否" }) ~= 1 then return end
      ctx:move("flare", "dust", 2, me, me)            -- 支付 2 气
      ctx:return_enhance(self)                        -- 回到付与区
      ctx:move_to_card("dust", self, 4)               -- 4 片樱花结晶从虚移到这张牌上
      ctx:move("distance", "dust", 1)                 -- 再次结算展开时
      ctx:freeze(ctx:opp(), 1)
    end },

  ---------------------------------------------------------------------------
  -- 变格 A1 试炼
  ---------------------------------------------------------------------------
  { set = "konuru.A1", form = "A1", num = 6, name = "冰凌包覆", kind = "normal",
    type = "enhance", nagi = 0, full_power = true, ice_as_armor = true,
    on_enter = function(ctx)
      local me = ctx:player()
      ctx:move_to_card("dust", ctx:source_inst(), 5)  -- 5虚到这张牌上
      local n = 0
      while true do
        local got = ctx:freeze(me, 1)
        if got <= 0 then break end
        n = n + got
      end
      if n >= 2 then  -- 至少冻结了 2 次
        ctx:attack { range = { 2, 5 }, damage = { aura = 2, life = 2 },
                     keywords = { "no_normal_response" } }
      end
    end },

  { set = "konuru.A1", form = "A1", num = 1, name = "寒毒式", kind = "special", type = "attack",
    cost = 2,
    attack = { range = { 3, 4 }, damage = { aura = 2, life = 2 } },
    on_attack_after = function(ctx)
      local me = ctx:player()
      local got = ctx:freeze(me, 1)      -- 冻结自己1次
      if got > 0 then ctx:freeze(ctx:opp(), 1) end  -- 若成功冻结了，则冻结对手1次
    end,
    reset = { kind = "end_turn", cond = function(ctx)
      return ctx:ice(ctx:opp()) < ctx:ice(ctx:player())  -- 对手的冰晶比你少
    end } },

  { set = "konuru.A1", form = "A1", num = 3, name = "残烛式", kind = "special",
    type = "enhance",
    cost = function(ctx)
      local y = ctx:ice(ctx:player()) + ctx:ice(ctx:opp())
      return math.max(0, 3 + ctx:life(ctx:opp()) - y)
    end,
    nagi = function(ctx)
      local y = ctx:ice(ctx:player()) + ctx:ice(ctx:opp())
      return math.max(0, 6 + ctx:life(ctx:opp()) - y)
    end,
    on_discard = function(ctx) ctx:die(ctx:opp()) end },

}
