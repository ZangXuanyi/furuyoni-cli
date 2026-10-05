-- 06-Yukihi 雪灯
-- 机制 变貌：回合结束时可选切换武器（伞/簪）。同一张攻击牌在两种武器下效果不同。
-- 开局为伞。当前武器对对手可见。O=伞/簪；A1=社交。

-- 将范围规范化为 span 列表；两形态可用时取并集（无常其心：仅该牌取两距离并集）。
local function spans(r)
  if type(r[1]) == "table" then return r else return { r } end
end

local function pick(ctx, u, h)
  local um = ctx:umbrella(ctx:player())
  local cur = um and u or h
  local other = um and h or u
  local range = cur.range
  if ctx:shared_range(ctx:player()) then
    local out = {}
    for _, s in ipairs(spans(cur.range)) do out[#out + 1] = { s[1], s[2] } end
    for _, s in ipairs(spans(other.range)) do out[#out + 1] = { s[1], s[2] } end
    range = out
  end
  return { range = range, damage = cur.damage, keywords = cur.keywords }
end

return {

  ---------------------------------------------------------------------------
  -- 本格 O 伞/簪
  ---------------------------------------------------------------------------
  { set = "yukihi", form = "O", num = 1, name = "藏针/含针", kind = "normal", type = "attack",
    attack = function(ctx)
      return pick(ctx, { range = {4, 6}, damage = { aura = 3, life = 1 } },
                  { range = {0, 2}, damage = { aura = 1, life = 2 } })
    end },

  { set = "yukihi", form = "O", num = 2, name = "预演/猫骗", kind = "normal", type = "attack",
    attack = function(ctx)
      return pick(ctx, { range = {5, 6}, damage = { aura = 1, life = 1 } },
                  { range = {0, 2}, damage = { aura = 1, life = 1 } })
    end,
    on_attack_after = function(ctx)
      if ctx:umbrella(ctx:player()) then
        ctx:to_hand(ctx:source_inst())  -- 伞：回到手牌
        ctx:switch_weapon(ctx:player())
      end
    end },

  { set = "yukihi", form = "O", num = 3, name = "拒/引", kind = "normal", type = "attack",
    attack = function(ctx)
      return pick(ctx, { range = {2, 5}, damage = { aura = 1, life = 1 } },
                  { range = {0, 2}, damage = { aura = 1, life = 1 } })
    end,
    on_attack_after = function(ctx)
      if ctx:umbrella(ctx:player()) then
        if ctx:choose("拒/引", { "1 虚到距", "1 距到虚" }) == 1 then
          ctx:move("dust", "distance", 1)
        else
          ctx:move("distance", "dust", 1)
        end
      else
        ctx:move("distance", "dust", 2)
      end
    end },

  { set = "yukihi", form = "O", num = 4, name = "挥舞/突刺", kind = "normal", type = "attack",
    full_power = true,
    attack = function(ctx)
      return pick(ctx, { range = {4, 6}, damage = { aura = 5 } },
                  { range = {0, 2}, damage = { life = 2 } })
    end },

  { set = "yukihi", form = "O", num = 5, name = "伞飞转", kind = "normal", type = "action",
    on_play = function(ctx)
      ctx:switch_weapon(ctx:player())
      ctx:move("dust", "aura", 1, ctx:player(), ctx:player())
    end,
    triggers = {
      { event = "weapon_switched",
        cond = function(ctx, ev)
          return ctx:card_zone(ctx:source_inst()) == "hand" and ev:card() ~= ctx:source_inst()
        end,
        run = function(ctx, ev)
          -- 展示（无机械影响），然后 1 虚到自装
          ctx:move("dust", "aura", 1, ctx:player(), ctx:player())
        end },
    } },

  { set = "yukihi", form = "O", num = 6, name = "闪回/潜行", kind = "normal", type = "action",
    response = true,
    on_play = function(ctx)
      if ctx:umbrella(ctx:player()) then
        ctx:move("dust", "distance", 1)
      else
        ctx:move("distance", "dust", 1)
      end
    end },

  { set = "yukihi", form = "O", num = 7, name = "结缘", kind = "normal", type = "enhance",
    nagi = 2,
    on_enter = function(ctx)
      if ctx:umbrella(ctx:player()) then
        ctx:move("distance", "dust", 1)
      else
        ctx:move("dust", "distance", 1)
      end
    end,
    on_discard = function(ctx)
      if ctx:umbrella(ctx:player()) then
        ctx:move("dust", "distance", 1)
      else
        ctx:move("distance", "dust", 1)
      end
    end },

  { set = "yukihi", form = "O", num = 1, name = "纷扬如雪", kind = "special", type = "attack",
    cost = 2,
    attack = function(ctx)
      return pick(ctx, { range = {4, 5}, damage = { aura = 3, life = 1 } },
                  { range = {0, 2}, damage = { aura = 0, life = 0 } })
    end,
    on_attack_after = function(ctx)
      if ctx:umbrella(ctx:player()) then ctx:gain_vigor(ctx:player(), 1) end
    end,
    reset = { kind = "immediate", on = "weapon_switched" } },

  { set = "yukihi", form = "O", num = 2, name = "明灭如灯", kind = "special", type = "attack",
    cost = 5,
    attack = function(ctx)
      return pick(ctx, { range = {4, 6}, damage = { aura = 0, life = 0 } },
                  { range = {0, 0}, damage = { aura = 4, life = 5 } })
    end },

  { set = "yukihi", form = "O", num = 3, name = "无常其心", kind = "special", type = "enhance",
    nagi = 7, full_power = true },

  { set = "yukihi", form = "O", num = 4, name = "复返其身", kind = "special", type = "action",
    cost = 1, response = true,
    on_play = function(ctx)
      ctx:switch_weapon(ctx:player())
      ctx:move("dust", "aura", 1, ctx:player(), ctx:player())
    end },

  ---------------------------------------------------------------------------
  -- 变格 A1 社交
  ---------------------------------------------------------------------------
  { set = "yukihi.A1", form = "A1", num = 2, name = "声援/威吓", kind = "normal", type = "attack",
    attack = function(ctx)
      local bonus = ctx:enhance_crystal_total(ctx:player()) >= 4
      local u = { range = {3, 5}, damage = { aura = 2, life = bonus and 2 or 1 } }
      local h = { range = {1, 2}, damage = { aura = bonus and 2 or 1, life = 2 } }
      return pick(ctx, u, h)
    end },

  { set = "yukihi.A1", form = "A1", num = 4, name = "纬丝/经纱", kind = "normal", type = "attack",
    attack = function(ctx)
      return pick(ctx, { range = {2, 8}, damage = { aura = 1, life = 1 } },
                  { range = {0, 4}, damage = { aura = 0, life = 0 } })
    end,
    on_attack_after = function(ctx)
      if ctx:umbrella(ctx:player()) then
        ctx:next_attack_mod {
          match = function(ctx2, atk) return atk:source_goddess() ~= "yukihi" end,
          apply = function(ctx2, atk)
            atk:extend_near(1)
            atk:extend_far(1)
          end,
        }
        ctx:to_deck_bottom(ctx:source_inst())
      else
        if ctx:cards_played_this_turn(ctx:player()) == 1 then
          local pool = {}
          for _, inst in ipairs(ctx:discard_pile(ctx:player())) do
            if not ctx:is_full_power(inst) and not ctx:card_is_goddess(inst, "yukihi") then
              pool[#pool + 1] = inst
            end
          end
          if #pool > 0 then
            local sel = ctx:choose_cards("经纱：从弃牌堆使用一张非雪灯非全力牌", pool, 0, 1)
            for _, inst in ipairs(sel) do ctx:use_card(inst, false) end
          end
        end
      end
    end },

  { set = "yukihi.A1", form = "A1", num = 2, name = "翩然如织", kind = "special", type = "enhance",
    nagi = 1,
    triggers = {
      { event = "normal_card_used",
        cond = function(ctx, ev) return ctx:umbrella(ctx:player()) and ev:first() end,
        run = function(ctx, ev)
          ctx:gain_vigor(ctx:player(), 1)
          ctx:dust_to_card(ctx:source_inst(), 3)
        end },
      { event = "turn_start",
        cond = function(ctx, ev)
          return (not ctx:umbrella(ctx:player())) and ev:subject() == ctx:player()
        end,
        run = function(ctx, ev) ctx:attack { range = {0, 5}, damage = { aura = 2, life = 2 } } end },
    } },

}
