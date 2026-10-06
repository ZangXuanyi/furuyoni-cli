-- 25-Misora 观空
-- 【象征武器】弓（O）。
-- 【机制】瞄准点～双方每个回合结束时都可以（可选）把当前距记录为“瞄准点”。
--   自己的回合内若进行过攻击，则在主要阶段结束时移除瞄准点（回合结束仍可重新记录）。
--   追踪～带追踪词条的攻击在判定距离时参考瞄准点而非当前实际距离（含被对应导致
--   距离不符时的重新判定）；没有瞄准点就不能打出追踪牌。瞄准点是玩家属性，借用
--   追踪牌的玩家只要自己有瞄准点就能用。
-- 引擎侧支持：ctx:aim / ctx:set_aim / Attack:contains / AF_Tracking（"tracking"），
--   CardDef.distance_is_aim / no_advance_escape / distance_limit_03 / no_reuse。

return {

  ---------------------------------------------------------------------------
  -- 弓 O 常规牌
  ---------------------------------------------------------------------------
  -- 【4-7 2/1】若攻击距离包含瞄准点，此攻击的对装伤害改为 -/1。
  -- 攻击后：若当前距离等于瞄准点，1虚到自装。
  { set = "misora", form = "O", num = 1, name = "弓流", kind = "normal", type = "attack",
    attack = { range = { 4, 7 }, damage = { aura = 2, life = 1 } },
    on_play = function(ctx)
      local src = ctx:source_inst()
      ctx:next_attack_mod {
        match = function(c, atk)
          if atk:source_inst() ~= src then return false end
          local aim = c:aim(c:player())
          return aim >= 0 and atk:contains(aim)
        end,
        apply = function(c, atk) atk:no_aura_damage() end,
        this_turn = true,
      }
    end,
    on_attack_after = function(ctx)
      local aim = ctx:aim(ctx:player())
      if aim >= 0 and ctx:distance() == aim then
        ctx:move("dust", "aura", 1, ctx:player(), ctx:player())
      end
    end },

  -- 【2-4 2/1】攻击后：你可以令瞄准点的数值 +1 或 -1。
  { set = "misora", form = "O", num = 2, name = "引弓蹴", kind = "normal", type = "attack",
    attack = { range = { 2, 4 }, damage = { aura = 2, life = 1 } },
    on_attack_after = function(ctx)
      local me = ctx:player()
      local aim = ctx:aim(me)
      if aim < 0 then return end
      local pick = ctx:choose("引弓蹴：令瞄准点 ±1？", { "瞄准点 +1", "瞄准点 -1", "不变" })
      if pick == 1 then
        ctx:set_aim(me, aim + 1)
      elseif pick == 2 then
        ctx:set_aim(me, aim - 1)
      end
    end },

  -- 对应【2-5 1/1】攻击后：距 > 瞄准点：1距到虚；== ：1虚到装；< ：1虚到距。
  { set = "misora", form = "O", num = 3, name = "风口", kind = "normal", type = "attack",
    response = true,
    attack = { range = { 2, 5 }, damage = { aura = 1, life = 1 } },
    on_attack_after = function(ctx)
      local me = ctx:player()
      local aim = ctx:aim(me)
      if aim < 0 then return end
      local d = ctx:distance()
      if d > aim then
        ctx:move("distance", "dust", 1)
      elseif d == aim then
        ctx:move("dust", "aura", 1, me, me)
      else
        ctx:move("dust", "distance", 1)
      end
    end },

  -- 全力【5-15 5/1】追踪。攻击后：对手弃一张攻击牌；不能则展示手牌并盖伏其牌库顶3张。
  { set = "misora", form = "O", num = 4, name = "旋翎疑矢", kind = "normal", type = "attack",
    full_power = true,
    attack = { range = { 5, 15 }, damage = { aura = 5, life = 1 }, keywords = { "tracking" } },
    on_attack_after = function(ctx)
      local opp = ctx:opp()
      local pool = {}
      for _, i in ipairs(ctx:hand(opp)) do
        if ctx:is_attack(i) then pool[#pool + 1] = i end
      end
      if #pool > 0 then
        local sel = ctx:choose_cards_for(opp, "旋翎疑矢：弃置一张攻击牌", pool, 1, 1)
        for _, i in ipairs(sel) do ctx:discard_card(i) end
      else
        ctx:reveal_hand(opp)
        for _ = 1, 3 do ctx:cover_top(opp) end
      end
    end },

  -- 获得1集中力。本回合内，若当前距离与你的瞄准点皆位于你下一次进行的、非观空的、
  -- 对装伤害非 - 的通常牌攻击范围内，该攻击 +1/+1。
  { set = "misora", form = "O", num = 5, name = "精密化", kind = "normal", type = "action",
    on_play = function(ctx)
      ctx:gain_vigor(ctx:player(), 1)
      ctx:next_attack_mod {
        match = function(c, atk)
          if atk:attacker() ~= c:player() then return false end
          if not atk:from_normal() then return false end
          if atk:source_is_goddess("misora") then return false end
          if atk:aura_damage() == nil then return false end
          local aim = c:aim(c:player())
          if aim < 0 then return false end
          if not atk:contains(c:distance()) then return false end
          if not atk:contains(aim) then return false end
          return true
        end,
        apply = function(c, atk) atk:add { aura = 1, life = 1 } end,
        this_turn = true,
      }
    end },

  -- 从弃牌堆或游戏外未加入构筑的非全力攻击牌中选一张使用，该牌获得锁定；
  -- 若选的是游戏外的牌，使用后移出游戏。
  -- 注意：基本规则规定「本来就打不到的攻击牌不能被打出」，锁定只免除因被对应
  -- 导致的距离重判，因此候选要按当前距过滤（否则会用出打不到的攻击）。
  { set = "misora", form = "O", num = 6, name = "寻踪箭", kind = "normal", type = "action",
    on_play = function(ctx)
      local me = ctx:player()
      local pool, outside = {}, {}
      local function can_hit(i)
        local spec = ctx:card_attack_spec(i)
        local d = ctx:distance()
        for _, sp in ipairs(spec.range or {}) do
          if d >= sp[1] and d <= sp[2] then return true end
        end
        return false
      end
      local function consider(i, isOutside)
        if not ctx:is_attack(i) then return end
        if ctx:is_full_power(i) then return end
        if not can_hit(i) then return end
        pool[#pool + 1] = i
        if isOutside then outside[i] = true end
      end
      for _, i in ipairs(ctx:discard_pile(me)) do consider(i, false) end
      for _, i in ipairs(ctx:unchosen_normals(me)) do consider(i, true) end
      if #pool == 0 then return end
      local sel = ctx:choose_cards("寻踪箭：选择一张非全力攻击牌使用", pool, 1, 1)
      if #sel == 0 then return end
      local card = sel[1]
      ctx:next_attack_mod {
        match = function(c, atk) return atk:source_inst() == card end,
        apply = function(c, atk) atk:keyword("lock") end,
        this_turn = true,
      }
      ctx:use_card(card, false)
      if outside[card] then ctx:remove_from_game(card) end
    end },

  -- 终端。距离限制 0-3（打出时当前距必须 ∈[0,3]）。展开时：2敌装到距。
  -- 弃置时：本回合内当前距离 +1、达人距离 +1。
  { set = "misora", form = "O", num = 7, name = "空之翼", kind = "normal", type = "enhance",
    nagi = 2, terminal = true, limit_distance = { 0, 3 },
    on_enter = function(ctx) ctx:move("aura", "distance", 2, ctx:opp()) end,
    on_discard = function(ctx)
      ctx:add_temp_distance(ctx:player(), 1)
      ctx:add_temp_near_distance(ctx:player(), 1)
    end },

  ---------------------------------------------------------------------------
  -- 弓 O 切札
  ---------------------------------------------------------------------------
  -- （2）【2X+3 -/1】追踪。不可对。X = 这张牌上的樱花结晶数。
  -- 攻击后：若 X >= 2，对手盖伏他的整个牌库。
  -- 使用后：你的结束阶段开始时，1虚到这张牌上，然后把这张牌设为未使用状态。
  { set = "misora", form = "O", num = 1, name = "遥瞩霜际", kind = "special", type = "attack",
    cost = 2, keep_crystals_on_reset = true,
    attack = function(ctx)
      local x = ctx:crystals(ctx:source_inst())
      local d = 2 * x + 3
      return { range = { d, d }, damage = { life = 1 },
               keywords = { "tracking", "unrespondable" } }
    end,
    on_attack_after = function(ctx)
      if ctx:crystals(ctx:source_inst()) >= 2 then ctx:cover_deck(ctx:opp()) end
    end,
    triggers = {
      { event = "end_phase_start",
        cond = function(ctx, ev) return ev:subject() == ctx:player() end,
        run = function(ctx, ev)
          ctx:dust_to_card(ctx:source_inst(), 1)
          ctx:reset_special(ctx:source_inst())
        end },
    } },

  -- 【纳1】展开中：若你有瞄准点，则当前距离变为瞄准点的数值。
  -- 展开中：你不能前进或离脱。
  { set = "misora", form = "O", num = 2, name = "蔽目重云", kind = "special", type = "enhance",
    cost = 1, nagi = 1, distance_is_aim = true, no_advance_escape = true },

  -- 对应【纳3】展开时：若被对应的是通常牌，且你的瞄准点位于该被对应的攻击范围内，
  -- 则打消该攻击，然后将该攻击牌封印于此牌下。
  -- 弃置时：将此牌封印的牌置入对手的弃牌堆。
  { set = "misora", form = "O", num = 3, name = "惴息悬影", kind = "special", type = "enhance",
    cost = 2, nagi = 3, response = true,
    on_enter = function(ctx)
      local atk = ctx:responding_attack()
      if not atk then return end
      if not atk:from_normal() then return end
      local aim = ctx:aim(ctx:player())
      if aim < 0 or not atk:contains(aim) then return end
      atk:negate()
      local src = atk:source_inst()
      if src >= 0 then ctx:seal_card(ctx:source_inst(), src) end
    end,
    on_discard = function(ctx) ctx:return_sealed(ctx:source_inst()) end },

  -- 全力（5）【纳2】展开中：当前距离增大5（可以大于10）。
  -- 弃置时：1敌命到距、1敌装到距、1敌气到距。
  -- 使用后：此牌不能由其他牌的效果再次发动。
  { set = "misora", form = "O", num = 4, name = "观空穹仪", kind = "special", type = "enhance",
    cost = 5, nagi = 2, full_power = true, distance_mod = 5, no_reuse = true,
    on_discard = function(ctx)
      local opp = ctx:opp()
      ctx:move("life", "distance", 1, opp)
      ctx:move("aura", "distance", 1, opp)
      ctx:move("flare", "distance", 1, opp)
    end },

}
