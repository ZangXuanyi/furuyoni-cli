-- 19-Megumi 泷河希
-- 【象征武器】唐棹（O）。终端（A1）。
-- 【机制】耕种～游戏开始时获得「土壤」（种子 / 植株），并把 5 个绿色结晶放到种子。
--   打出任何付与牌时，先把 1 个绿色结晶从「种子」移到「植株」；若该牌有词条「生长X」，
--   可以至多 X 个「植株」移到该牌上（每次都要询问，可以不移动）。
--   绿色结晶视作樱花结晶（维持 / 消耗 / 按结晶数计算的效果），但移除时先移除樱花、
--   再移除绿色；被移除的绿色回到「种子」（假想树在游戏中时回到假想树）。
-- 【假想树】1 格 + 2 格 + 3 格。放第 N+1 层的前置是第 N 层至少有 1 个（不必满）。
--   主要阶段开始时脱落 1 个种结晶，按从下往上的顺序落回土壤（散华时）。
--
-- 引擎接口（本柱新增）：
--   ctx:seeds(p) / plants(p) / seed_to_plant(p,n) / attach_green(inst,n) / detach_green(inst,n)
--   ctx:green(inst) / card_crystal_count(inst) / total_green_on_enhances(p) / green_zones(p)
--   ctx:tree_active(p) / tree_enter(p) / tree_slot(p,i) / tree_occupied(p) / tree_place(p)
--   ctx:tree_fall(p,n) / unchosen_cuts(p) / tree_use_cut(inst)
--   ctx:used_generated_attack(p) / is_borrowed(inst) / card_owner(inst) / set_next_growth(p,x)

-- 让“本张正在结算的攻击牌”获得一次性修正。
local function boost_self(ctx, fn)
  ctx:self_boost(fn)
end

return {

  ---------------------------------------------------------------------------
  -- 唐棹 O（常规）
  ---------------------------------------------------------------------------
  -- 【4-8 2/1】若你的种子是空的，此攻击获得+1/+1。
  { set = "megumi", form = "O", num = 1, name = "收割", kind = "normal", type = "attack",
    attack = { range = { 4, 8 }, damage = { aura = 2, life = 1 } },
    on_play = function(ctx)
      if ctx:seeds(ctx:player()) == 0 then
        boost_self(ctx, function(_, a) a:add { aura = 1, life = 1 } end)
      end
    end },

  -- 【4-5 2/1】若你的任意能力牌上有绿色结晶，此攻击获得+1/+1。
  { set = "megumi", form = "O", num = 2, name = "打场", kind = "normal", type = "attack",
    attack = { range = { 4, 5 }, damage = { aura = 2, life = 1 } },
    on_play = function(ctx)
      if ctx:total_green_on_enhances(ctx:player()) > 0 then
        boost_self(ctx, function(_, a) a:add { aura = 1, life = 1 } end)
      end
    end },

  -- 【3-5 2/1】本回合你的下一张非希的付与牌获得【生长2】。
  { set = "megumi", form = "O", num = 3, name = "脱粒", kind = "normal", type = "attack",
    attack = { range = { 3, 5 }, damage = { aura = 2, life = 1 } },
    on_play = function(ctx)
      ctx:set_next_growth(ctx:player(), 2)
    end },

  -- 对应【2-4 1/1】攻击后：若此攻击对对手造成伤害，本应到虚/敌气的结晶改为到距；
  -- 若你有生效中的付与牌，对手畏缩。
  { set = "megumi", form = "O", num = 4, name = "开荒", kind = "normal", type = "attack",
    response = true,
    attack = { range = { 2, 4 }, damage = { aura = 1, life = 1 }, keywords = { "to_distance" } },
    on_attack_after = function(ctx)
      if #ctx:enhances(ctx:player()) > 0 then ctx:cower(ctx:opp()) end
    end },

  -- 【纳1,生长1】展开时：1虚到距。展开中：当前距离增加 X（X = 本牌上的绿色结晶数）。
  { set = "megumi", form = "O", num = 5, name = "芦苇", kind = "normal", type = "enhance",
    nagi = 1, growth = 1, green_distance = true,
    on_expand = function(ctx) ctx:move("dust", "distance", 1) end },

  -- 【纳1,生长2】展开中：对手回合开始时进行攻击【3-5 2/1】；你的回合开始时进行攻击【1-3 2/1】。
  -- 弃置时：对手畏缩。
  { set = "megumi", form = "O", num = 6, name = "凤仙", kind = "normal", type = "enhance",
    nagi = 1, growth = 2,
    triggers = {
      { event = "turn_start",
        cond = function(ctx, ev) return ev:subject() == ctx:player() end,
        run = function(ctx)
          ctx:attack { range = { 1, 3 }, damage = { aura = 2, life = 1 } }
        end },
      { event = "turn_start",
        cond = function(ctx, ev) return ev:subject() ~= ctx:player() end,
        run = function(ctx)
          ctx:attack { range = { 3, 5 }, damage = { aura = 2, life = 1 } }
        end },
    },
    on_discard = function(ctx) ctx:cower(ctx:opp()) end },

  -- 全力【纳0,生长2】展开时：执行一次基本动作。
  -- 展开中：你的回合结束时执行一次基本动作；每当对手第一次将距中的樱花结晶移出距时，
  --         1虚到距；对手的回合内不能移除这张牌上的结晶。
  { set = "megumi", form = "O", num = 7, name = "蔷薇", kind = "normal", type = "enhance",
    nagi = 0, growth = 2, full_power = true, keep_crystals_on_opp_turn = true,
    on_expand = function(ctx) ctx:free_basics(ctx:player(), 1) end,
    triggers = {
      { event = "turn_start",
        run = function(ctx) ctx:store_int("out", 0) end },
      { event = "turn_end",
        cond = function(ctx, ev) return ev:subject() == ctx:player() end,
        run = function(ctx) ctx:free_basics(ctx:player(), 1) end },
      { event = "distance_changed",
        cond = function(ctx, ev)
          return ev:first() and ev:subject() ~= ctx:player() and ctx:load_int("out", 0) == 0
        end,
        run = function(ctx)
          ctx:store_int("out", 1)
          ctx:move("dust", "distance", 1)
        end },
    } },

  ---------------------------------------------------------------------------
  -- 唐棹 O（切札）
  ---------------------------------------------------------------------------
  -- 【3-7 1/1】使你的一个种子变为植株。再起：你的植株是空的。
  { set = "megumi", form = "O", num = 1, name = "因果律之根", kind = "special", type = "attack",
    cost = 1,
    attack = { range = { 3, 7 }, damage = { aura = 1, life = 1 } },
    on_play = function(ctx) ctx:seed_to_plant(ctx:player(), 1) end,
    reset = { kind = "end_turn",
              cond = function(ctx) return ctx:plants(ctx:player()) == 0 end } },

  -- 对应【纳2,生长1】展开时：被对应的攻击获得 -X/-0（X = 你所有付与牌上绿色结晶总数）。
  -- 展开中：每当对手的回合开始时进行攻击【1-5 X/1】。
  { set = "megumi", form = "O", num = 2, name = "可能性之枝", kind = "special", type = "enhance",
    cost = 3, nagi = 2, growth = 1, response = true,
    on_expand = function(ctx)
      local a = ctx:responding_attack()
      if not a then return end
      local x = ctx:total_green_on_enhances(ctx:player())
      if x > 0 then a:add { aura = -x } end
    end,
    triggers = {
      { event = "turn_start",
        cond = function(ctx, ev)
          return ev:subject() ~= ctx:player() and ctx:enhance_active(ctx:source_inst())
        end,
        run = function(ctx)
          local x = ctx:total_green_on_enhances(ctx:player())
          ctx:attack { range = { 1, 5 }, damage = { aura = x, life = 1 } }
        end },
    } },

  -- 【纳2】这张牌上的任何结晶不能被除了每回合开始的固定 -1 或本牌以外的其他手段移除。
  -- 展开中：其它所有付与牌上的樱花/绿色结晶要被移除时，改为移到这张牌上。
  -- 展开中：这张牌上有 5 个绿色结晶时，该回合结束时进行攻击【5 5/5】锁定、不可被打消；
  --         然后将所有绿色结晶移到种子，樱花结晶移到虚。
  { set = "megumi", form = "O", num = 3, name = "终结之果实", kind = "special", type = "enhance",
    cost = 4, nagi = 2, crystal_immune = true, redirect_crystals = true,
    triggers = {
      { event = "turn_end",
        cond = function(ctx, ev)
          local inst = ctx:source_inst()
          return ev:subject() == ctx:player() and ctx:green(inst) >= 5
             and ctx:enhance_active(inst)
        end,
        run = function(ctx)
          local inst = ctx:source_inst()
          ctx:attack { range = { 5, 5 }, damage = { aura = 5, life = 5 },
                       keywords = { "lock", "no_negate" } }
          local g = ctx:green(inst)
          if g > 0 then ctx:detach_green(inst, g) end
          ctx:empty_card(inst)
        end },
    } },

  -- 【纳0,生长5】展开中：每个回合内你进行的第一次对装伤害不大于 3 的攻击获得+1/+1。
  { set = "megumi", form = "O", num = 4, name = "泷河希之掌", kind = "special", type = "enhance",
    cost = 3, nagi = 0, growth = 5,
    triggers = {
      { event = "turn_start",
        run = function(ctx) ctx:store_int("boost", 0) end },
      { event = "attack_declared",
        cond = function(ctx, ev)
          if ev:attacker() ~= ctx:player() then return false end
          if ctx:load_int("boost", 0) == 1 then return false end
          if not ctx:enhance_active(ctx:source_inst()) then return false end
          local a = ev:attack()
          if not a then return false end
          local ad = a:aura_damage()
          return ad ~= nil and ad <= 3
        end,
        run = function(ctx, ev)
          ctx:store_int("boost", 1)
          local a = ev:attack()
          if a then a:add { aura = 1, life = 1 } end
        end },
    } },

  ---------------------------------------------------------------------------
  -- 终端 A1
  ---------------------------------------------------------------------------
  -- A1-N2 假想打击：【纳3】展开时/破弃时：进行【5-6 3/1】的攻击。
  { set = "megumi.A1", form = "A1", num = 2, name = "假想打击", kind = "normal", type = "enhance",
    nagi = 3,
    on_expand = function(ctx)
      ctx:attack { range = { 5, 6 }, damage = { aura = 3, life = 1 } }
    end,
    on_discard = function(ctx)
      ctx:attack { range = { 5, 6 }, damage = { aura = 3, life = 1 } }
    end },

  -- A1-S3 须臾景之叶（2）：对应【4-7 2/X】。X 等于你绿色结晶存在的区域数（土壤/付与区/假想树）。
  -- 本卡牌只有在你使用过衍生攻击的回合才可以使用。攻击后：1虚到自装。
  { set = "megumi.A1", form = "A1", num = 3, name = "须臾景之叶", kind = "special",
    type = "attack", cost = 2, response = true,
    attack = function(ctx)
      return { range = { 4, 7 }, damage = { aura = 2, life = ctx:green_zones(ctx:player()) } }
    end,
    playable = function(ctx) return ctx:used_generated_attack(ctx:player()) end,
    respond = function(ctx) return ctx:used_generated_attack(ctx:player()) end,
    on_attack_after = function(ctx) ctx:move("dust", "aura", 1) end },

  -- A1-S4 未然境之掌（2）：正常使用时将假想树加入游戏，并把土壤中的 1 个种子放到假想树上。
  -- 使用后：你可以免费使用这张牌；若如此做，结算完毕后将本卡牌移出游戏。
  -- 假想树的 6 格效果（散华时 / 开花中）挂在这张牌上；它移出游戏后触发器仍然生效。
  { set = "megumi.A1", form = "A1", num = 4, name = "未然境之掌", kind = "special",
    type = "action", cost = 2, trigger_from_removed = true,
    on_play = function(ctx)
      local inst = ctx:source_inst()
      if ctx:card_owner(inst) ~= ctx:player() then return end  -- 被偷的不生效
      ctx:tree_enter(ctx:player())
      ctx:tree_place(ctx:player())
      if ctx:choose("未然境之掌：免费再使用一次（结算后移出游戏）？",
                    { "使用", "不使用" }) == 1 then
        ctx:tree_place(ctx:player())
        ctx:remove_card(inst)
      end
    end,
    triggers = {
      -- 2-1【散华时】进行攻击【3-6 3/1】
      { event = "tree_fell",
        cond = function(ctx, ev)
          return ev:subject() == ctx:player() and ev:card() == 1
             and ctx:tree_active(ctx:player())
        end,
        run = function(ctx)
          ctx:attack { range = { 3, 6 }, damage = { aura = 3, life = 1 } }
        end },
      -- 2-2【散华时】进行攻击【0-3 1/1】。1虚到距。
      { event = "tree_fell",
        cond = function(ctx, ev)
          return ev:subject() == ctx:player() and ev:card() == 2
             and ctx:tree_active(ctx:player())
        end,
        run = function(ctx)
          ctx:attack { range = { 0, 3 }, damage = { aura = 1, life = 1 } }
          ctx:move("dust", "distance", 1)
        end },
      -- 3-1【开花中】在你的回合中，第一次对对手命造成伤害时，基本动作装附或前进一次。
      { event = "enemy_life_damaged",
        cond = function(ctx, ev)
          return ev:subject() == ctx:player() and ev:first() and ctx:is_my_turn()
             and ctx:tree_active(ctx:player()) and ctx:tree_slot(ctx:player(), 3) == 1
        end,
        run = function(ctx) ctx:free_basics_of(ctx:player(), 1, { "aura", "advance" }) end },
      -- 3-2【散华时】公开使用一张构筑时没有选择的、非付与、非全力的切牌，使用后移出游戏。
      { event = "tree_fell",
        cond = function(ctx, ev)
          return ev:subject() == ctx:player() and ev:card() == 4
             and ctx:tree_active(ctx:player())
        end,
        run = function(ctx)
          local cuts = {}
          for _, inst in ipairs(ctx:unchosen_cuts(ctx:player())) do
            if ctx:card_name(inst) ~= "未然境之掌" then cuts[#cuts + 1] = inst end
          end
          if #cuts == 0 then return end
          local names = {}
          for i, inst in ipairs(cuts) do names[i] = ctx:card_name(inst) end
          local pick = ctx:choose("散华时（3-2）：公开使用一张未选择的切牌", names)
          local inst = cuts[pick]
          if inst then ctx:tree_use_cut(inst) end
        end },
      -- 3-3【开花中】在你的回合中，第一次对对手装造成伤害时，基本动作离脱或后退一次。
      { event = "enemy_aura_damaged",
        cond = function(ctx, ev)
          return ev:subject() == ctx:player() and ev:first() and ctx:is_my_turn()
             and ctx:tree_active(ctx:player()) and ctx:tree_slot(ctx:player(), 5) == 1
        end,
        run = function(ctx) ctx:free_basics_of(ctx:player(), 1, { "escape", "retreat" }) end },
    } },

}
