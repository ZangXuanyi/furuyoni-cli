-- Test fixture: a minimal but complete custom goddess ("模造"), used to exercise
-- dynamic content loading and the custom rules pack.
-- 7 常规 + 4 切札 (O) + 1 异相 (A1, replacing O-N1).
return {

  { set = "customx", form = "O", num = 1, name = "伪斩", kind = "normal", type = "attack",
    attack = { range = {3, 4}, damage = { aura = 2, life = 1 } } },
  { set = "customx", form = "O", num = 2, name = "伪突", kind = "normal", type = "attack",
    attack = { range = {1, 2}, damage = { aura = 1, life = 2 } } },
  { set = "customx", form = "O", num = 3, name = "伪构", kind = "normal", type = "action",
    on_play = function(ctx) ctx:move("dust", "aura", 1, ctx:player(), ctx:player()) end },
  { set = "customx", form = "O", num = 4, name = "伪壁", kind = "normal", type = "enhance",
    nagi = 2 },
  { set = "customx", form = "O", num = 5, name = "伪应", kind = "normal", type = "attack",
    response = true,
    attack = { range = {0, 5}, damage = { aura = 1, life = 1 } } },
  { set = "customx", form = "O", num = 6, name = "伪力", kind = "normal", type = "attack",
    full_power = true,
    attack = { range = {2, 5}, damage = { aura = 3, life = 2 } } },
  { set = "customx", form = "O", num = 7, name = "伪步", kind = "normal", type = "action",
    on_play = function(ctx) ctx:move("distance", "dust", 1) end },

  { set = "customx", form = "O", num = 1, name = "伪奥义", kind = "special", type = "attack",
    cost = 2,
    attack = { range = {2, 4}, damage = { aura = 3, life = 3 } } },
  { set = "customx", form = "O", num = 2, name = "伪礼", kind = "special", type = "action",
    cost = 1,
    on_play = function(ctx) ctx:draw(ctx:player(), 1) end },
  { set = "customx", form = "O", num = 3, name = "伪盾", kind = "special", type = "action",
    cost = 2, response = true,
    on_play = function(ctx)
      local a = ctx:responding_attack()
      if a then a:add { aura = -1 } end
    end },
  { set = "customx", form = "O", num = 4, name = "伪终", kind = "special", type = "attack",
    cost = 4,
    attack = { range = {3, 5}, damage = { aura = 4, life = 4 } } },

  { set = "customx.A1", form = "A1", num = 1, name = "伪斩·改", kind = "normal", type = "attack",
    attack = { range = {2, 3}, damage = { aura = 3, life = 1 } } },
}
