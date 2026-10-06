-- 21-Kamuwi 神居
-- 【象征武器】剑（O）。
-- 【机制】诅咒：诅咒 >= 16 或 命 == 0 时死亡。你的回合开始时，命 5-9 被诅咒 1 次；命 < 5 被诅咒 2 次。
--   打出一些牌时，可以诅咒自身来换取强化（形如"诅咒1：..."，均为可选）。

-- 诅咒 n（可选），返回是否真的诅咒了。
local function self_curse(ctx, n, prompt)
  if ctx:curse(ctx:player()) + n >= 16 then
    -- 诅咒会致死也仍然可选（玩家自己判断）
  end
  if ctx:choose(prompt, { "诅咒" .. n, "不诅咒" }) ~= 1 then return false end
  ctx:add_curse(ctx:player(), n)
  return true
end

return {

  ---------------------------------------------------------------------------
  -- 剑 O
  ---------------------------------------------------------------------------
  { set = "kamuwi", form = "O", num = 1, name = "红刃", kind = "normal", type = "attack",
    on_play = function(ctx)
      if self_curse(ctx, 1, "红刃：诅咒1，本牌获得+1/+1？") then ctx:store_int("c", 1) end
    end,
    attack = function(ctx)
      if ctx:load_int("c", 0) == 1 then
        return { range = { 3, 3 }, damage = { aura = 4, life = 2 } }
      end
      return { range = { 3, 3 }, damage = { aura = 3, life = 1 } }
    end },

  { set = "kamuwi", form = "O", num = 2, name = "散华刃", kind = "normal", type = "attack",
    on_play = function(ctx)
      if self_curse(ctx, 1, "散华刃：诅咒1，本牌获得+1/+0？") then ctx:store_int("c1", 1) end
      if self_curse(ctx, 1, "散华刃：再诅咒1，攻击后 1敌装到自装？") then ctx:store_int("c2", 1) end
    end,
    attack = function(ctx)
      if ctx:load_int("c1", 0) == 1 then
        return { range = { 3, 4 }, damage = { aura = 3, life = 1 } }
      end
      return { range = { 3, 4 }, damage = { aura = 2, life = 1 } }
    end,
    on_attack_after = function(ctx)
      if ctx:load_int("c2", 0) ~= 1 then return end
      if ctx:aura(ctx:opp()) <= 4 then  -- 若敌装不大于 4
        ctx:move("aura", "aura", 1, ctx:opp(), ctx:player())
      end
    end },

  { set = "kamuwi", form = "O", num = 3, name = "四剑乱刃", kind = "normal", type = "attack",
    zenkai = true,
    attack = { range = { 2, 4 }, damage = { aura = 2, life = 1 } },
    on_play = function(ctx)
      -- 诅咒1 & 全开：攻击后进行 3 次攻击
      if ctx:zenkai() and self_curse(ctx, 1, "四剑乱刃（全开）：诅咒1 以发动追加攻击？") then
        ctx:store_int("c", 1)
      end
    end,
    on_attack_after = function(ctx)
      if ctx:load_int("c", 0) ~= 1 then return end
      for _ = 1, 3 do
        ctx:attack { range = { 2, 4 }, damage = { aura = 1, life = 1 } }
      end
    end },

  -- 对应【1-4 1/1】仅限对应打出。诅咒2 / 诅咒4 二选一：打消被对应攻击的伤害。
  { set = "kamuwi", form = "O", num = 4, name = "格杀", kind = "normal", type = "attack",
    response = true, response_only = true,
    attack = { range = { 1, 4 }, damage = { aura = 1, life = 1 } },
    on_play = function(ctx)
      local a = ctx:responding_attack()
      if not a then return end
      local opts = {}
      if not a:from_special() and not a:source_full_power() then
        opts[#opts + 1] = "诅咒2：打消其伤害（非切牌且非全力）"
      end
      opts[#opts + 1] = "诅咒4：打消其伤害（任何攻击）"
      opts[#opts + 1] = "不诅咒"
      local pick = ctx:choose("格杀：选择", opts)
      if pick == 1 and #opts == 3 then
        if self_curse(ctx, 2, "格杀：诅咒2？") then a:negate_damage() end
      elseif (pick == 1 and #opts == 2) or (pick == 2 and #opts == 3) then
        if self_curse(ctx, 4, "格杀：诅咒4？") then a:negate_damage() end
      end
    end },

  { set = "kamuwi", form = "O", num = 5, name = "织荆", kind = "normal", type = "action",
    on_play = function(ctx)
      local d = ctx:distance()
      if d >= 5 then
        ctx:move("distance", "dust", 2)
      elseif d <= 1 then
        ctx:move("dust", "distance", 2)
      else
        -- 本回合内你下一次非神居的攻击牌 +1/+0 且通常牌不可对
        ctx:next_attack_mod {
          match = function(c2, atk)
            return atk:attacker() == c2:player() and atk:from_normal() and
                   not atk:source_is_goddess("kamuwi")
          end,
          apply = function(c2, atk)
            atk:add { aura = 1 }
            atk:keyword("no_normal_response")
          end,
          this_turn = true,
        }
      end
    end },

  { set = "kamuwi", form = "O", num = 6, name = "血晶乱流", kind = "normal", type = "action",
    full_power = true,
    on_play = function(ctx)
      local opts = { "攻击【5-9 4/1】", "若距大于等于5则2距到虚", "攻击【2-4 2/2】", "2虚到自装" }
      local sel = ctx:choose_options("血晶乱流：选择2项（按从左到右顺序执行）", opts, 2, 2)
      table.sort(sel)
      local me = ctx:player()
      for _, i in ipairs(sel) do
        if i == 0 then
          ctx:attack { range = { 5, 9 }, damage = { aura = 4, life = 1 } }
        elseif i == 1 then
          if ctx:distance() >= 5 then ctx:move("distance", "dust", 2) end
        elseif i == 2 then
          ctx:attack { range = { 2, 4 }, damage = { aura = 2, life = 2 } }
        else
          ctx:move("dust", "aura", 2, me, me)
        end
      end
    end },

  -- 【纳2】破绽。诅咒1：展开时攻击【3 2/2】锁定。进入敌装的结晶改为进虚。
  { set = "kamuwi", form = "O", num = 7, name = "血飞沫", kind = "normal", type = "enhance",
    nagi = 2, breakable = true, deny_enemy_aura = true,
    on_expanded = function(ctx)
      if self_curse(ctx, 1, "血飞沫：诅咒1，进行攻击【3 2/2】锁定？") then
        ctx:attack { range = { 3, 3 }, damage = { aura = 2, life = 2 }, keywords = { "lock" } }
      end
    end },

  ---------------------------------------------------------------------------
  -- 切
  ---------------------------------------------------------------------------
  { set = "kamuwi", form = "O", num = 1, name = "灯", kind = "special", type = "action",
    cost = 5,
    on_play = function(ctx)
      if self_curse(ctx, 4, "灯：诅咒4，以未使用状态获得切牌「晓」？") then
        ctx:gain_extra("晓")
      end
    end },

  -- 【纳4】诅咒2：展开时攻击【3-4 3/3】通常牌不可对，并对自命造成1伤害。本牌弃置前对手不会死亡。
  { set = "kamuwi", form = "O", num = 2, name = "阡", kind = "special", type = "enhance",
    cost = 3, nagi = 4, protects_enemy = true,
    on_enter = function(ctx)
      if not self_curse(ctx, 2, "阡：诅咒2，进行攻击【3-4 3/3】？") then return end
      ctx:attack { range = { 3, 4 }, damage = { aura = 3, life = 3 },
                   keywords = { "no_normal_response" } }
      ctx:lose_life(ctx:player(), 1)  -- 并对自命造成 1 伤害
    end },

  { set = "kamuwi", form = "O", num = 3, name = "尸", kind = "special", type = "attack",
    cost = 1, response = true,
    attack = { range = { 0, 6 }, damage = { aura = 0, life = 0 } },
    on_attack_after = function(ctx)
      -- 本回合内，对手下一次攻击必须额外弃置一张该女神的牌作为费用
      ctx:set_extra_attack_cost(ctx:opp(), "kamuwi")
    end,
    triggers = {
      -- 即再起：诅咒变为 6 或变为 12
      { event = "cursed",
        cond = function(ctx, ev)
          if ev:subject() ~= ctx:player() then return false end
          local c = ctx:curse(ctx:player())
          return c == 6 or c == 12
        end,
        run = function(ctx, ev) ctx:reset_special(ctx:source_inst()) end },
    } },

  { set = "kamuwi", form = "O", num = 4, name = "理", kind = "special", type = "action",
    cost = 3,
    on_play = function(ctx)
      local me = ctx:player()
      for _ = 1, 3 do
        local opts, acts = {}, {}
        opts[#opts + 1], acts[#acts + 1] = "1距到虚", "dist"
        if ctx:life(me) <= 8 then
          opts[#opts + 1], acts[#acts + 1] = "诅咒1，1自装到自命", "heal"
        end
        opts[#opts + 1], acts[#acts + 1] = "结束", "stop"
        local pick = ctx:choose("理：选择（至多3次，可重复）", opts)
        if pick < 1 or pick > #acts then break end
        local k = acts[pick]
        if k == "dist" then
          ctx:move("distance", "dust", 1)
        elseif k == "heal" then
          ctx:add_curse(me, 1)
          ctx:move("aura", "life", 1, me, me)
        else
          break
        end
      end
    end },

  -- 晓: 防止对应；攻击后移出游戏。
  { set = "kamuwi", form = "O", num = 901, name = "晓", kind = "special", type = "attack",
    cost = 6, extra = true,
    attack = { range = { 3, 7 }, damage = { aura = 6, life = 4 },
               keywords = { "overwhelm", "prevent_response" } },
    on_attack_after = function(ctx) ctx:remove_card(ctx:source_inst()) end },

}
