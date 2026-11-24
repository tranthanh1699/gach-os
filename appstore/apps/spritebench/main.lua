-- Sprite Bench: thêm sprite (tối đa 1000) bay nảy trong scene gach.game, đo fps / tick / update.
-- C++ di chuyển + chạy animation; Lua chỉ đảo hướng khi chạm mép.
local ui, game = gach.ui, gach.game
local app = {}

local SIZE, MAX = 32, 1000
local W, H = 0, 0
local ids = {}
local hud

local function add(n)
  for _ = 1, n do
    if #ids >= MAX then return end
    local id = game.sprite({ sheet = 0, x = math.random(0, W - SIZE), y = math.random(0, H - SIZE),
                             vx = math.random(-240, 240), vy = math.random(-240, 240), layer = math.random(0, 3) })
    game.play(id, { from = 0, to = 3, fps = 8 })
    ids[#ids + 1] = id
  end
end

local function bounce()
  local right, bottom = W - SIZE, H - SIZE
  for i = 1, #ids do
    local id = ids[i]
    local x, y, _, vx, vy = game.get(id)
    if x < 0 then game.set(id, { x = 0, vx = -vx })
    elseif x > right then game.set(id, { x = right, vx = -vx }) end
    if y < 0 then game.set(id, { y = 0, vy = -vy })
    elseif y > bottom then game.set(id, { y = bottom, vy = -vy }) end
  end
end

function app.on_create(root)
  ui.style(root, { bg = 0x000000, pad = 0, gap = 0 })
  local bar = ui.row(root)
  ui.style(bar, { width = "100%", pad = 8, gap = 12, bg = 0x1C1C1E })
  ui.button(bar, { text = "+100" }, function() add(100) end)
  ui.button(bar, { text = "Clear", variant = "tinted" }, function() game.clear(); ids = {} end)
  hud = ui.label(bar, "")
  local sw, sh = ui.screen_size()
  local _, w, h = game.scene(root, sw, sh - 70, { bg = 0x0B0B14, fps = 30 })
  W, H = w, h
  game.sheet("img/orbs.png", SIZE, SIZE)
  add(100)
  -- Tự tăng: mỗi 4 s thêm 100 sprite tới 1000, ghi số đo vào log (gach log filter bench).
  local acc, ramp = 0, 0
  game.on_update(function(dt)
    bounce()
    acc, ramp = acc + dt, ramp + dt
    if acc >= 500 then
      acc = 0
      local st = game.stats()
      local text = string.format("%d sprites | %d fps | tick %.2f ms | update %.2f ms | draw %.2f ms",
        st.sprites, st.fps, st.tick_us / 1000, st.update_us / 1000, st.draw_us / 1000)
      ui.set_text(hud, text)
      if ramp >= 4000 then
        ramp = 0
        gach.log.i("bench " .. text)
        if #ids < MAX then add(100) end
      end
    end
  end)
end

return app
