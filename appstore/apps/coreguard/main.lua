-- Core Guard: thủ thành theo làn. Robot của trạm giữ lõi năng lượng trước đàn slime và bọ máy.
-- Engine gach.game (SDK v8): C++ di chuyển / animation / va chạm / vẽ; Lua chỉ làm luật chơi.
local ui, game, store = gach.ui, gach.game, gach.storage
local app = {}

-- ===================== Hằng số =====================
local ROWS, COLS = 5, 9
local SPRITE = 96                       -- Khung robot / địch (px).
local FXS = 48                          -- Khung hiệu ứng (px).
local CORE_W = 64                       -- Cột lõi bên trái (px).
local G_UNIT, G_BOLT, G_ENEMY, G_MINE, G_CELL = 1, 2, 3, 4, 5
local L_TILE, L_CORE, L_UNIT, L_ENEMY, L_BOLT, L_FX, L_CELL = 0, 1, 2, 3, 4, 5, 6
local FX_BOLT, FX_ICE, FX_CELL, FX_BOOM = 0, 1, 2, 4
local SHIELD_MAX = 3
local HUD_H = 96                        -- Chiều cao thanh trên (dp).
local START_ENERGY = 100
local SKY_CELL_EVERY = 9                -- Giây.
local CELL_VALUE = 25
local MAX_LEVEL = 5
local C = { bg = 0x0E1018, hud = 0x161A26, card = 0x222838, card_sel = 0x2E5BFF, text = 0xFFFFFF,
            dim = 0x8A93A8, gold = 0xFFD54A, red = 0xFF5A5A, shield = 0x5AD8FF, shield_off = 0x3A4152, tile_a = 0x1B2131, tile_b = 0x1F2638 }

-- kind -> thuộc tính. cd: hồi thẻ (s); fire: nhịp bắn (s); dmg: sát thương mỗi phát.
local ROBOT = {
  { key = "dynamo",  name = "Dynamo",  cost = 50,  cd = 6,  hp = 300, every = 11 },
  { key = "spark",   name = "Spark",   cost = 100, cd = 6,  hp = 300, fire = 1.4, dmg = 30 },
  { key = "frost",   name = "Frost",   cost = 150, cd = 8,  hp = 300, fire = 1.8, dmg = 22, slow = 3 },
  { key = "bulwark", name = "Bulwark", cost = 50,  cd = 20, hp = 2400 },
  { key = "mine",    name = "Pulse",   cost = 25,  cd = 25, hp = 300, arm = 8, dmg = 900 },
}
-- speed: phần chiều rộng ô mỗi giây; dps: sát thương lên robot mỗi giây.
local ENEMY = {
  { name = "Blob",    hp = 180,  speed = 0.20, dps = 40 },
  { name = "Shell",   hp = 480,  speed = 0.20, dps = 40 },
  { name = "Skitter", hp = 110,  speed = 0.42, dps = 30 },
  { name = "Brute",   hp = 1100, speed = 0.13, dps = 90 },
}
local LEVEL_KINDS = { { 1 }, { 1, 1, 3 }, { 1, 3, 2 }, { 1, 2, 3, 4 }, { 1, 2, 2, 3, 4 } }

-- ===================== Trạng thái =====================
local root
local S                 -- Trạng thái ván chơi (nil khi ở menu).
local hud = {}

local function unlocked()
  return tonumber(store.get("unlocked") or "1") or 1
end

-- ===================== Màn chơi: kế hoạch đợt địch =====================
local function make_plan(level)
  math.randomseed(level * 7919)
  local kinds = LEVEL_KINDS[level]
  local list, t = {}, 20
  for i = 1, 8 + level * 4 do
    t = t + math.max(3.5, 11 - level - i * 0.15)
    list[#list + 1] = { t = t, kind = kinds[math.random(#kinds)], row = math.random(ROWS) }
  end
  local final_t = t + 12
  for i = 1, 4 + level * 3 do
    list[#list + 1] = { t = final_t + i * 0.7, kind = kinds[math.random(#kinds)], row = math.random(ROWS) }
  end
  math.randomseed(gach.sys.uptime_ms())
  return list, final_t
end

-- ===================== Hình học sân =====================
local function cell_rect(r, c)
  return CORE_W + (c - 1) * S.cw, (r - 1) * S.ch
end

local function sprite_pos(r, c, size)
  local x, y = cell_rect(r, c)
  return x + (S.cw - size) // 2, y + (S.ch - size) // 2
end

local function cell_at(x, y)
  if x < CORE_W then return nil end
  local c = (x - CORE_W) // S.cw + 1
  local r = y // S.ch + 1
  if c < 1 or c > COLS or r < 1 or r > ROWS then return nil end
  return r, c
end

-- ===================== HUD =====================
local function refresh_energy()
  ui.set_text(hud.energy, tostring(S.energy))
  for k, card in ipairs(hud.cards) do
    local ready = S.card_cd[k] <= 0
    local can = ready and S.energy >= ROBOT[k].cost
    ui.style(card.box, { bg = (S.selected == k) and C.card_sel or (can and C.card or 0x161A22) })
  end
end

local function refresh_shield()
  for i = 1, SHIELD_MAX do
    ui.style(hud.pips[i], { bg = (i <= S.shield) and C.shield or C.shield_off })
  end
end

local function select_card(k)
  S.selected = (S.selected == k) and nil or k
  refresh_energy()
end

-- ===================== Sinh / xóa =====================
local function add_energy(n)
  S.energy = S.energy + n
  refresh_energy()
end

local function spawn_cell(x, y, target_y)
  local id = game.sprite({ sheet = S.sh_fx, frame = FX_CELL, x = x, y = y, layer = L_CELL, group = G_CELL })
  game.play(id, { from = FX_CELL, to = FX_CELL + 1, fps = 3 })
  if target_y then game.move_to(id, x, target_y, 2200) end
  game.life(id, 10000)
  S.cells[id] = true
end

local function place(k, r, c)
  local kind = ROBOT[k]
  local x, y = sprite_pos(r, c, SPRITE)
  local id = game.sprite({ sheet = S.sh_units, frame = (k - 1) * 2, x = x, y = y, layer = L_UNIT,
                           group = (kind.key == "mine") and G_MINE or G_UNIT })
  if kind.key == "dynamo" or kind.key == "frost" then
    game.play(id, { from = (k - 1) * 2, to = (k - 1) * 2 + 1, fps = 2 })
  end
  local u = { k = k, id = id, r = r, c = c, hp = kind.hp, timer = kind.every or kind.fire or kind.arm or 0,
              armed = false, flash = 0 }
  S.grid[r][c] = u
  S.units[id] = u
  S.energy = S.energy - kind.cost
  S.card_cd[k] = kind.cd
  S.selected = nil
  refresh_energy()
end

local function remove_unit(u)
  game.remove(u.id)
  S.units[u.id] = nil
  if S.grid[u.r][u.c] == u then S.grid[u.r][u.c] = nil end
end

local function walk(id)
  local e = S.enemies[id]
  if e then game.play(id, { from = (e.k - 1) * 3, to = (e.k - 1) * 3 + 1, fps = 4 }) end
end

local function spawn_enemy(k, r)
  local _, y = sprite_pos(r, 1, SPRITE)
  local id = game.sprite({ sheet = S.sh_enemies, frame = (k - 1) * 3, x = S.W + 4, y = y, layer = L_ENEMY,
                           group = G_ENEMY })
  S.enemies[id] = { k = k, r = r, hp = ENEMY[k].hp, vx = nil, slow_until = 0 }
  S.alive = S.alive + 1
  walk(id)
end

local function kill_enemy(id)
  if S.enemies[id] then
    S.enemies[id] = nil
    S.alive = S.alive - 1
    game.remove(id)
  end
end

local function boom(x, y)
  local id = game.sprite({ sheet = S.sh_fx, frame = FX_BOOM, x = x - FXS // 2, y = y - FXS // 2, layer = L_FX })
  game.play(id, { from = FX_BOOM, to = FX_BOOM + 1, fps = 8, loop = false })
  game.life(id, 300)
end

local function hurt(id, dmg, slow)
  local e = S.enemies[id]
  if not e then return end
  e.hp = e.hp - dmg
  if slow then e.slow_until = S.clock + slow; e.vx = nil end
  if e.hp <= 0 then
    local x, y = game.get(id)
    boom(x + SPRITE // 2, y + SPRITE // 2)
    kill_enemy(id)
    return
  end
  -- Khung trúng đòn 1/8 s rồi đi tiếp (animation chạy native, done gọi lại walk).
  local hit = (e.k - 1) * 3 + 2
  game.play(id, { from = hit, to = hit, fps = 8, loop = false, done = walk })
end

-- ===================== Kết thúc ván =====================
local show_menu

local function finish(won)
  if S.over then return end
  S.over = true
  game.pause(true)
  if won and S.level >= unlocked() and S.level < MAX_LEVEL then
    store.set("unlocked", tostring(S.level + 1))
  end
  ui.alert({ title = won and "Station secured!" or "Core lost",
             message = won and ("Level " .. S.level .. " cleared.") or "The swarm reached the core.",
             buttons = { "Menu", won and (S.level < MAX_LEVEL and "Next level" or "Play again") or "Retry" },
             fn = function(i)
               local level = S.level
               if i == 2 then
                 app.start_level((won and level < MAX_LEVEL) and level + 1 or level)
               else
                 show_menu()
               end
             end })
end

-- ===================== Vòng logic =====================
local function enemy_in_row_ahead(r, x)
  for id, e in pairs(S.enemies) do
    if e.r == r then
      local ex = game.get(id)
      if ex and ex > x and ex < S.W then return true end
    end
  end
  return false
end

local function fire(u, frame)
  local kind = ROBOT[u.k]
  local x, y = sprite_pos(u.r, u.c, FXS)
  local id = game.sprite({ sheet = S.sh_fx, frame = frame, x = x + SPRITE // 3, y = y - 4, vx = S.cw * 4.5,
                           layer = L_BOLT, group = G_BOLT })
  game.life(id, math.floor((S.W - x) / (S.cw * 4.5) * 1000) + 100)
  S.bolts[id] = { dmg = kind.dmg, slow = kind.slow, r = u.r }
  game.set(u.id, { frame = (u.k - 1) * 2 + 1 })
  u.flash = 0.12
end

local function update_units(dt)
  for _, u in pairs(S.units) do
    local kind = ROBOT[u.k]
    u.timer = u.timer - dt
    if u.flash > 0 then
      u.flash = u.flash - dt
      if u.flash <= 0 then game.set(u.id, { frame = (u.k - 1) * 2 }) end
    end
    if kind.key == "dynamo" and u.timer <= 0 then
      u.timer = kind.every
      local x, y = sprite_pos(u.r, u.c, FXS)
      spawn_cell(x + 18, y - 18)
    elseif (kind.key == "spark" or kind.key == "frost") and u.timer <= 0 then
      local ux = sprite_pos(u.r, u.c, SPRITE)
      if enemy_in_row_ahead(u.r, ux) then
        u.timer = kind.fire
        fire(u, kind.key == "spark" and FX_BOLT or FX_ICE)
      end
    elseif kind.key == "mine" and not u.armed and u.timer <= 0 then
      u.armed = true
      game.set(u.id, { frame = (u.k - 1) * 2 + 1 })
    end
  end
end

local function update_enemies(dt)
  for id, e in pairs(S.enemies) do
    local x = game.get(id)
    if not x then
      S.enemies[id] = nil
      S.alive = S.alive - 1
    elseif x < CORE_W - SPRITE // 3 then
      kill_enemy(id)
      S.shield = S.shield - 1
      refresh_shield()
      ui.toast("Core hit! Shield " .. S.shield .. "/" .. SHIELD_MAX, 1200)
      if S.shield <= 0 then finish(false) return end
    else
      local def = ENEMY[e.k]
      local slowed = S.clock < e.slow_until
      -- Ô trước mặt (mép trái thân địch).
      local r, c = cell_at(x + SPRITE // 4, (e.r - 1) * S.ch + 1)
      local u = r and S.grid[r][c]
      local vx
      if u and not (ROBOT[u.k].key == "mine" and u.armed) then
        vx = 0
        u.hp = u.hp - def.dps * dt * (slowed and 0.5 or 1)
        if u.hp <= 0 then remove_unit(u) end
      else
        vx = -def.speed * S.cw * (slowed and 0.5 or 1)
      end
      if vx ~= e.vx then
        e.vx = vx
        game.set(id, { vx = vx })
      end
    end
  end
end

local function on_update(dt_ms)
  if S.over then return end
  local dt = dt_ms / 1000
  S.clock = S.clock + dt
  -- Hồi thẻ.
  local changed = false
  for k = 1, #ROBOT do
    if S.card_cd[k] > 0 then
      S.card_cd[k] = math.max(0, S.card_cd[k] - dt)
      ui.set_value(hud.cards[k].bar, math.floor(100 * (1 - S.card_cd[k] / ROBOT[k].cd)))
      if S.card_cd[k] == 0 then changed = true end
    end
  end
  if changed then refresh_energy() end
  -- Địch theo kế hoạch.
  while S.next <= #S.plan and S.plan[S.next].t <= S.clock do
    local p = S.plan[S.next]
    spawn_enemy(p.kind, p.row)
    S.next = S.next + 1
  end
  if not S.final_shown and S.clock >= S.final_t - 2 then
    S.final_shown = true
    ui.toast("Final wave incoming!", 2000)
  end
  -- Năng lượng rơi từ trên.
  S.sky = S.sky - dt
  if S.sky <= 0 then
    S.sky = SKY_CELL_EVERY
    local r, c = math.random(ROWS), math.random(COLS)
    local x, y = sprite_pos(r, c, FXS)
    spawn_cell(x, -FXS, y)
  end
  update_units(dt)
  update_enemies(dt)
  if not S.over and S.next > #S.plan and S.alive <= 0 then finish(true) end
end

-- ===================== Chạm =====================
local function on_touch(phase, x, y)
  if phase ~= "down" or S.over then return end
  local cell = game.pick(x, y, G_CELL)
  if cell and S.cells[cell] then
    S.cells[cell] = nil
    game.life(cell, 0)
    add_energy(CELL_VALUE)
    return
  end
  local r, c = cell_at(x, y)
  if not r then return end
  local k = S.selected
  if k == "remove" then
    if S.grid[r][c] then remove_unit(S.grid[r][c]) end
    S.selected = nil
    refresh_energy()
  elseif k and not S.grid[r][c] and S.card_cd[k] <= 0 and S.energy >= ROBOT[k].cost then
    place(k, r, c)
  end
end

local function on_bolt_hit(b, e)
  local bolt = S.bolts[b]
  local en = S.enemies[e]
  if not bolt or not en or bolt.r ~= en.r then return end
  S.bolts[b] = nil
  game.remove(b)
  hurt(e, bolt.dmg, bolt.slow)
end

local function on_mine_hit(m, e)
  local u = S.units[m]
  if not u or not u.armed or not S.enemies[e] or S.enemies[e].r ~= u.r then return end
  local mx = sprite_pos(u.r, u.c, SPRITE)
  local _, my = sprite_pos(u.r, u.c, SPRITE)
  boom(mx + SPRITE // 2, my + SPRITE // 2)
  remove_unit(u)
  for id, en in pairs(S.enemies) do
    local ex = game.get(id)
    if en.r == u.r and ex and math.abs(ex - mx) < S.cw * 1.2 then hurt(id, ROBOT[u.k].dmg) end
  end
end

-- ===================== Dựng màn chơi =====================
local function build_hud(parent)
  -- ui.row xuống dòng khi tràn: tổng chiều rộng phải < màn hình (853 dp trên JC8012P4A1C):
  -- lề 16 + năng lượng 88 + thẻ 5 x 72 + Remove ~118 + khiên ~64 + khe 8 x 6 ~= 700 dp.
  local bar = ui.row(parent)
  ui.style(bar, { width = "100%", height = HUD_H, pad = 8, gap = 6, bg = C.hud, align = "center", scroll = false })
  local en = ui.row(bar)
  ui.style(en, { gap = 4, align = "center", width = 88, scroll = false })
  ui.image(en, "img/cell.png", { w = 32, h = 32 })
  hud.energy = ui.label(en, "0")
  ui.style(hud.energy, { color = C.gold, font = "large" })
  hud.cards = {}
  for k, kind in ipairs(ROBOT) do
    -- 80 dp = lề 2 x 4 + icon 36 + khe 2 + giá (chữ nhỏ ~17) + khe 2 + thanh 8: vừa khít, không cuộn.
    local box = ui.column(bar)
    ui.style(box, { width = 72, height = 80, pad = 4, gap = 2, radius = 10, bg = C.card, align = "center",
                    scroll = false })
    ui.image(box, "img/icon_" .. kind.key .. ".png", { w = 36, h = 36 })
    local cost = ui.label(box, tostring(kind.cost))
    ui.style(cost, { color = C.gold, font = "small" })
    local cd = ui.bar(box, 0, 100, 100)
    ui.on(box, "click", function() if S and not S.over then select_card(k) end end)
    hud.cards[k] = { box = box, bar = cd }
  end
  ui.button(bar, { text = "Remove", icon = ui.SYMBOL.TRASH, variant = "tinted", size = "sm" }, function()
    if S and not S.over then S.selected = (S.selected == "remove") and nil or "remove"; refresh_energy() end
  end)
  -- Khiên lõi: 3 ô (sáng = còn, xám = mất).
  local shield = ui.row(bar)
  -- ui.row mặc định rộng 100 % cha: phải đặt "content", không thì cụm khiên chiếm cả hàng và rớt dòng.
  ui.style(shield, { width = "content", gap = 4, align = "center", scroll = false })
  ui.image(shield, "img/core.png", { w = 16, h = 24 })
  hud.pips = {}
  for i = 1, SHIELD_MAX do
    hud.pips[i] = ui.box(shield)
    ui.style(hud.pips[i], { width = 12, height = 24, radius = 4, bg = C.shield })
  end
end

function app.start_level(level)
  ui.clear(root)
  ui.style(root, { pad = 0, gap = 0 }) -- Menu đặt lề 24: scene phải chiếm đúng cả màn, không tràn.
  hud = {}
  S = { level = level, energy = START_ENERGY, shield = SHIELD_MAX, clock = 0, next = 1, sky = 4,
        alive = 0, over = false, grid = {}, units = {}, enemies = {}, bolts = {}, cells = {},
        card_cd = {}, selected = nil }
  for r = 1, ROWS do S.grid[r] = {} end
  for k = 1, #ROBOT do S.card_cd[k] = 0 end
  S.plan, S.final_t = make_plan(level)

  build_hud(root)
  local sw, sh = ui.screen_size()
  local _, w, h = game.scene(root, sw, sh - HUD_H, { bg = C.bg, fps = 30 })
  S.W, S.H = w, h
  S.cw = (w - CORE_W) // COLS
  S.ch = h // ROWS
  S.sh_units = game.sheet("img/units.png", SPRITE, SPRITE)
  S.sh_enemies = game.sheet("img/enemies.png", SPRITE, SPRITE)
  S.sh_fx = game.sheet("img/fx.png", FXS, FXS)
  S.sh_core = game.sheet("img/core.png", 64, SPRITE)
  for r = 1, ROWS do
    for c = 1, COLS do
      local x, y = cell_rect(r, c)
      game.sprite({ x = x + 2, y = y + 2, w = S.cw - 4, h = S.ch - 4, layer = L_TILE,
                    color = ((r + c) % 2 == 0) and C.tile_a or C.tile_b })
    end
    game.sprite({ sheet = S.sh_core, x = 0, y = (r - 1) * S.ch + (S.ch - SPRITE) // 2, layer = L_CORE })
  end

  game.on_overlap(G_BOLT, G_ENEMY, on_bolt_hit)
  game.on_overlap(G_MINE, G_ENEMY, on_mine_hit)
  game.on_touch(on_touch)
  game.on_update(on_update)
  ui.toast("Level " .. level, 1500)
  refresh_shield()
  refresh_energy()
end

-- ===================== Menu =====================
show_menu = function()
  S = nil
  ui.clear(root)
  ui.style(root, { bg = C.bg, pad = 24, gap = 16, align = "center" })
  local title = ui.label(root, "CORE GUARD")
  ui.style(title, { color = C.text, font = "large" })
  local sub = ui.label(root, "Place robots on the grid. Stop the swarm before it reaches the core.")
  ui.style(sub, { color = C.dim })
  local art = ui.row(root)
  ui.style(art, { gap = 12, align = "center" })
  for _, kind in ipairs(ROBOT) do ui.image(art, "img/icon_" .. kind.key .. ".png", { w = 72, h = 72 }) end
  local levels = ui.row(root)
  ui.style(levels, { gap = 12, align = "center" })
  local open = unlocked()
  for level = 1, MAX_LEVEL do
    local b = ui.button(levels, { text = "Level " .. level, variant = (level <= open) and "filled" or "gray" },
      function() app.start_level(level) end)
    ui.set_enabled(b, level <= open)
  end
  local help = ui.label(root, "Tap a robot card, then a tile.  Tap falling energy cells to collect them.")
  ui.style(help, { color = C.dim })
end

-- Kiểm thử trên máy tính (giả lập gach.*): chọn thẻ như khi chạm thẻ trên HUD.
function app._select(k)
  if S and not S.over then select_card(k) end
end

-- ===================== Vòng đời =====================
function app.on_create(r)
  root = r
  show_menu()
end

function app.on_back()
  if S and not S.over then
    game.pause(true)
    ui.alert({ title = "Leave level?", message = "Progress in this level is lost.", buttons = { "Stay", "Leave" },
               destructive = true, fn = function(i)
                 if i == 2 then show_menu() else game.pause(false) end
               end })
    return true
  end
  if S then show_menu() return true end
  return false
end

return app
