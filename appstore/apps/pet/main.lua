-- Pet (gach SDK v6): nuôi một sinh vật nhỏ lớn dần theo thời gian thật.
--   Trứng (2 phút) -> Baby (tới 2 giờ) -> Child (tới 24 giờ) -> Adult
--   Chỉ số 0..100 giảm dần theo giờ (kể cả khi app đóng, tính theo giờ NTP): no, vui, năng lượng, sạch
--   Chạm pet: nhảy + tim bay; vuốt: vuốt ve. Feed / Play / Clean / Sleep / Medicine.
-- Nhân vật vẽ bằng các khối bo tròn (box + set_pos), chuyển động bằng ui.animate.
local ui, timer, storage, S = gach.ui, gach.timer, gach.storage, gach.ui.SYMBOL

local app = {}

-- ===================== Cấu hình =====================
local SPECIES = {
  { id = "mochi", name = "Mochi", trait = "Fluffy & cuddly", body = 0xF5C28B, accent = 0xE8A15F, belly = 0xFFE7CC,
    shape = "cat" },
  { id = "pip", name = "Pip", trait = "Cheerful songbird", body = 0xFFD93D, accent = 0xFF8C42, belly = 0xFFF3B0,
    shape = "bird" },
  { id = "glob", name = "Glob", trait = "Wobbly & curious", body = 0x5DD39E, accent = 0x3BA776, belly = 0xA8F0C6,
    shape = "slime" },
  { id = "pebble", name = "Pebble", trait = "Tiny stone dragon", body = 0x9AA5B1, accent = 0x6C7A89, belly = 0xD5DBE1,
    shape = "dragon" },
}
local STAGES = {           -- tuổi tối thiểu (giây), tỷ lệ kích thước
  { name = "Egg", min = 0, scale = 0.62 },
  { name = "Baby", min = 120, scale = 0.62 },
  { name = "Child", min = 2 * 3600, scale = 0.82 },
  { name = "Adult", min = 24 * 3600, scale = 1.0 },
}
-- Thay đổi chỉ số mỗi giờ (âm = giảm).
local DECAY_AWAKE = { hunger = -12, happy = -8, energy = -6, clean = -5 }
local DECAY_SLEEP = { hunger = -6, happy = 0, energy = 22, clean = -2 }
local OFFLINE_CAP = 48 * 3600          -- tính bù tối đa 2 ngày khi app đóng
local SICK_LEVEL, CURED_LEVEL = 10, 25
local TICK_MS = 1000
local SAVE_EVERY = 30                  -- giây
local PET_BASE = 210                   -- dp: khung nhân vật ở giai đoạn Adult

local C = {
  bg = 0x000000, panel = 0x1C1C1E, card = 0x2C2C2E, text = 0xFFFFFF, dim = 0x8E8E93,
  day = 0x24476B, night = 0x0E1328, floor_day = 0x2F5D3A, floor_night = 0x16261C,
  eye = 0x1B1B1F, white = 0xFFFFFF, cheek = 0xFF8FA3, heart = 0xFF4D6D, dirt = 0x6B4F2A,
  bubble = 0xBDE7FF, food = 0xFF9F0A, moon = 0xF4F1C9,
  hunger = 0xFF9F0A, happy = 0xFF375F, energy = 0x64D2FF, clean = 0x30D158,
}

-- ===================== Trạng thái =====================
local pet = nil         -- { species, name, born, last, hunger, happy, energy, clean, sick, sleep }
local w = {}            -- widget
local parts = nil       -- bộ phận nhân vật đang vẽ
local tick_id, since_save, blink_in, zzz_in, wobble_in = nil, 0, 3, 0, 4
local busy_anim = false -- đang nhảy / ăn: tạm dừng nhịp thở

-- ===================== Thời gian =====================
local function days_from_civil(y, m, d)
  y = (m <= 2) and (y - 1) or y
  local era = (y >= 0 and y or y - 399) // 400
  local yoe = y - era * 400
  local mp = (m + 9) % 12
  local doy = (153 * mp + 2) // 5 + d - 1
  local doe = yoe * 365 + yoe // 4 - yoe // 100 + doy
  return era * 146097 + doe - 719468
end

-- Giây UTC hiện tại (số thực để không tràn số nguyên 32 bit của Lua trên máy), nil nếu chưa đồng bộ NTP.
local function now()
  local t = gach.sys.time()
  if not t then return nil end
  local local_secs = days_from_civil(t.year, t.month, t.day) * 86400.0 + t.hour * 3600 + t.min * 60 + t.sec
  return local_secs - (t.utc_offset or 0) * 60
end

-- Đồng hồ dự phòng khi chưa có NTP: chỉ đếm lúc app chạy.
local mono_base, mono_start = nil, nil
local REAL_EPOCH_MIN = 1.0e9           -- < 2001: mốc của đồng hồ dự phòng, không phải giờ thật

local function clock()
  local real = now()
  if real then
    -- Vừa có NTP sau một đoạn chạy bằng đồng hồ dự phòng: dời mốc, không coi khoảng nhảy là thời gian trôi qua.
    if pet and pet.last < REAL_EPOCH_MIN then
      local shift = real - pet.last
      pet.born, pet.last = pet.born + shift, real
    end
    return real
  end
  if not mono_base then mono_base, mono_start = (pet and pet.last) or 0, gach.sys.uptime_ms() end
  return mono_base + (gach.sys.uptime_ms() - mono_start) / 1000
end

local function local_hour()
  local t = gach.sys.time()
  return t and t.hour or 12
end

local function fmt_age(secs)
  secs = math.floor(secs)
  local d, h, m = secs // 86400, (secs // 3600) % 24, (secs // 60) % 60
  if d > 0 then return string.format("%dd %dh", d, h) end
  if h > 0 then return string.format("%dh %02dm", h, m) end
  return string.format("%dm %02ds", m, secs % 60)
end

-- ===================== Lưu / đọc =====================
local function clamp(v) return math.max(0, math.min(100, v)) end

local function save()
  if not pet then return end
  pet.last = clock()
  storage.set("pet", string.format("%s|%s|%.0f|%.0f|%d|%d|%d|%d|%d|%d", pet.species, pet.name, pet.born, pet.last,
    math.floor(pet.hunger), math.floor(pet.happy), math.floor(pet.energy), math.floor(pet.clean),
    pet.sick and 1 or 0, pet.sleep and 1 or 0))
  since_save = 0
end

local function load()
  local raw = storage.get("pet")
  if not raw then return nil end
  local f = {}
  for part in (raw .. "|"):gmatch("([^|]*)|") do f[#f + 1] = part end
  if #f < 10 then return nil end
  return { species = f[1], name = f[2], born = tonumber(f[3]) or 0, last = tonumber(f[4]) or 0,
           hunger = tonumber(f[5]) or 80, happy = tonumber(f[6]) or 80, energy = tonumber(f[7]) or 80,
           clean = tonumber(f[8]) or 80, sick = f[9] == "1", sleep = f[10] == "1" }
end

local function species_of(id)
  for _, s in ipairs(SPECIES) do if s.id == id then return s end end
  return SPECIES[1]
end

local function stage_index()
  local age = clock() - pet.born
  local idx = 1
  for i, st in ipairs(STAGES) do if age >= st.min then idx = i end end
  return idx
end

-- ===================== Mô phỏng =====================
local function apply_time(seconds)
  if seconds <= 0 or stage_index() == 1 then return end -- Trứng không đói.
  local hours = math.min(seconds, OFFLINE_CAP) / 3600
  local rate = pet.sleep and DECAY_SLEEP or DECAY_AWAKE
  for k, per_hour in pairs(rate) do pet[k] = clamp(pet[k] + per_hour * hours) end
  if pet.sleep and pet.energy >= 100 then pet.sleep = false end -- Ngủ đủ thì tự dậy.
  if not pet.sleep and pet.energy <= 0 then pet.sleep = true end -- Kiệt sức thì tự ngủ để hồi.
  if pet.hunger < SICK_LEVEL or pet.clean < SICK_LEVEL then pet.sick = true end
  if pet.sick then pet.happy = clamp(pet.happy - 4 * hours) end
end

local function mood()
  if pet.sleep then return "Sleeping", 0 end
  if pet.sick then return "Sick", -2 end
  local avg = (pet.hunger + pet.happy + pet.energy + pet.clean) / 4
  if avg >= 70 then return "Happy", 1 end
  if avg >= 40 then return "OK", 0 end
  return "Sad", -1
end

-- ===================== Vẽ =====================
local function shape(parent, x, y, bw, bh, color, radius)
  local b = ui.box(parent)
  ui.style(b, { width = math.floor(bw), height = math.floor(bh), bg = color, radius = radius or "round" })
  ui.set_pos(b, math.floor(x), math.floor(y))
  return b
end

local function clear_parts()
  if parts and parts.root then ui.delete(parts.root) end
  parts = nil
end

-- Nhân vật trong khung vuông size x size (dp); trả về các bộ phận cần đổi theo tâm trạng.
local function draw_pet(parent, sp, stage, size, cx, floor_y)
  local p = {}
  local s = size / 100            -- 1 đơn vị = 1% khung
  local root = ui.box(parent)
  ui.style(root, { width = math.floor(size), height = math.floor(size) })
  ui.set_pos(root, math.floor(cx - size / 2), math.floor(floor_y - size))
  p.root, p.size, p.x, p.y = root, size, math.floor(cx - size / 2), math.floor(floor_y - size)

  if stage == 1 then              -- Trứng: hình bầu dục có đốm.
    shape(root, 22 * s, 8 * s, 56 * s, 76 * s, 0xFFF8E7)
    shape(root, 34 * s, 28 * s, 12 * s, 10 * s, sp.body)
    shape(root, 56 * s, 46 * s, 14 * s, 12 * s, sp.accent)
    shape(root, 38 * s, 62 * s, 10 * s, 8 * s, sp.body)
    p.egg = true
    return p
  end

  -- Thân + bộ phận riêng của từng loài.
  if sp.shape == "cat" then
    shape(root, 20 * s, 4 * s, 18 * s, 22 * s, sp.accent, 6)      -- tai
    shape(root, 62 * s, 4 * s, 18 * s, 22 * s, sp.accent, 6)
    shape(root, 10 * s, 14 * s, 80 * s, 80 * s, sp.body)
    shape(root, 84 * s, 62 * s, 16 * s, 10 * s, sp.accent)        -- đuôi
  elseif sp.shape == "bird" then
    shape(root, 44 * s, 4 * s, 10 * s, 18 * s, sp.accent)         -- túm lông
    shape(root, 14 * s, 16 * s, 72 * s, 76 * s, sp.body)
    shape(root, 4 * s, 46 * s, 20 * s, 28 * s, sp.accent)         -- cánh
    shape(root, 76 * s, 46 * s, 20 * s, 28 * s, sp.accent)
  elseif sp.shape == "slime" then
    shape(root, 8 * s, 30 * s, 84 * s, 64 * s, sp.body, math.floor(28 * s))
    shape(root, 22 * s, 18 * s, 56 * s, 40 * s, sp.body)
    shape(root, 26 * s, 34 * s, 12 * s, 8 * s, 0xE6FFF1)           -- vệt sáng
  else -- dragon
    shape(root, 26 * s, 6 * s, 12 * s, 16 * s, sp.accent, 4)      -- sừng
    shape(root, 62 * s, 6 * s, 12 * s, 16 * s, sp.accent, 4)
    shape(root, 12 * s, 16 * s, 76 * s, 78 * s, sp.body, math.floor(30 * s))
    shape(root, 82 * s, 56 * s, 18 * s, 12 * s, sp.accent)        -- đuôi
    shape(root, 4 * s, 40 * s, 14 * s, 22 * s, sp.accent, 6)      -- cánh nhỏ
  end
  shape(root, 30 * s, 56 * s, 40 * s, 30 * s, sp.belly)           -- bụng

  -- Mắt: tròng trắng + con ngươi (con ngươi đổi chiều cao khi chớp / ngủ).
  p.eye_h = math.floor(14 * s)
  for i, ex in ipairs({ 30, 56 }) do
    shape(root, ex * s, 32 * s, 16 * s, 18 * s, C.white)
    p["eye" .. i] = shape(root, (ex + 4) * s, 35 * s, 9 * s, p.eye_h, C.eye)
  end
  p.cheek1 = shape(root, 20 * s, 50 * s, 12 * s, 7 * s, C.cheek)
  p.cheek2 = shape(root, 68 * s, 50 * s, 12 * s, 7 * s, C.cheek)
  if sp.shape == "bird" then
    p.mouth = shape(root, 44 * s, 50 * s, 12 * s, 8 * s, sp.accent, 3) -- mỏ
  else
    p.mouth = shape(root, 44 * s, 52 * s, 12 * s, 6 * s, C.eye)
  end
  p.s = s
  p.bird = sp.shape == "bird"
  return p
end

-- Mắt / miệng / má theo tâm trạng.
local function apply_face()
  if not parts or parts.egg then return end
  local _, m = mood()
  local key = tostring(m) .. (pet.sleep and "s" or "")
  if w.face_key == key then return end      -- Không đổi: khỏi vẽ lại.
  w.face_key = key
  local s = parts.s
  local eye_h = pet.sleep and math.max(2, math.floor(2 * s)) or parts.eye_h
  for i = 1, 2 do ui.style(parts["eye" .. i], { height = eye_h }) end
  local show_cheek = (m >= 1) and not pet.sleep
  ui.set_visible(parts.cheek1, show_cheek)
  ui.set_visible(parts.cheek2, show_cheek)
  if parts.bird then
    ui.style(parts.mouth, { height = math.floor((m >= 1 and 10 or 6) * s) })
  elseif m >= 1 then
    ui.style(parts.mouth, { width = math.floor(18 * s), height = math.floor(9 * s), radius = "round" })
  elseif m == 0 then
    ui.style(parts.mouth, { width = math.floor(12 * s), height = math.max(2, math.floor(3 * s)), radius = 2 })
  else
    ui.style(parts.mouth, { width = math.floor(9 * s), height = math.floor(9 * s), radius = "round" })
  end
end

local function stage_geometry()
  local st = STAGES[stage_index()]
  local size = math.floor(PET_BASE * st.scale)
  return size, w.stage_w // 2, w.floor_y
end

local function redraw_pet()
  clear_parts()
  local size, cx, fy = stage_geometry()
  parts = draw_pet(w.pet_layer, species_of(pet.species), stage_index(), size, cx, fy)
  parts.stage = stage_index()
  busy_anim = false   -- Hiệu ứng nhảy của nhân vật cũ đã bị hủy cùng nó.
  w.face_key = nil
  ui.on(parts.root, "click", function() app.poke() end)
  ui.on(parts.root, "swipe", function() app.pet_stroke() end)
  apply_face()
end

-- ===================== Hiệu ứng =====================
local function float_up(color, x, y, size, rise, ms)
  local b = shape(w.fx_layer, x, y, size, size, color)
  ui.animate(b, { y = math.floor(y - rise), opa = 0, ms = ms or 900, ease = "out", done = function() ui.delete(b) end })
end

local function hearts(n)
  local cx = parts.x + parts.size // 2
  for i = 1, n do
    float_up(C.heart, cx - 40 + math.random(0, 80), parts.y + 10, 14, 70 + math.random(0, 30), 800 + i * 120)
  end
end

local function bubbles(n)
  for _ = 1, n do
    float_up(C.bubble, parts.x + math.random(0, parts.size), parts.y + math.random(parts.size // 3, parts.size),
      math.random(10, 20), 60, 1000)
  end
end

local function zzz()
  local l = ui.label(w.fx_layer, "Z")
  ui.style(l, { color = C.white, font = "medium" })
  local x, y = parts.x + parts.size - 10, parts.y
  ui.set_pos(l, x, y)
  ui.animate(l, { x = x + 30, y = y - 50, opa = 0, ms = 1800, done = function() ui.delete(l) end })
end

local function bob()
  if not parts or busy_anim then return end
  local base = parts.y
  ui.animate(parts.root, { y = base - 4, ms = 900, ease = "in_out", done = function()
    if parts and not busy_anim then ui.animate(parts.root, { y = base, ms = 900, ease = "in_out" }) end
  end })
end

local function jump(height, done)
  if not parts then return end
  busy_anim = true
  local base, root = parts.y, parts.root
  ui.animate(root, { y = base - height, ms = 220, ease = "out", done = function()
    ui.animate(root, { y = base, ms = 380, ease = "bounce", done = function()
      busy_anim = false
      if done then done() end
    end })
  end })
end

local function blink()
  if not parts or parts.egg or pet.sleep then return end
  for i = 1, 2 do
    local eye = parts["eye" .. i]
    ui.animate(eye, { h = 2, ms = 90, done = function()
      if parts and parts["eye" .. i] == eye then ui.animate(eye, { h = parts.eye_h, ms = 110 }) end
    end })
  end
end

local function wobble_egg()
  if not parts or not parts.egg then return end
  local x0, root = parts.x, parts.root
  ui.animate(root, { x = x0 - 6, ms = 120, done = function()
    ui.animate(root, { x = x0 + 6, ms = 160, done = function() ui.animate(root, { x = x0, ms = 120 }) end })
  end })
end

local function draw_dirt()
  local spots = (pet.clean < 25 and 3) or (pet.clean < 50 and 2) or (pet.clean < 70 and 1) or 0
  if stage_index() == 1 then spots = 0 end
  if w.spots == spots then return end
  w.spots = spots
  ui.clear(w.dirt_layer)
  for i = 1, spots do
    shape(w.dirt_layer, w.stage_w // 2 + (i % 2 == 0 and 1 or -1) * (PET_BASE // 2 + i * 14), w.floor_y - 12,
      26 - i * 3, 14, C.dirt)
  end
end

local function set_scene()
  local night = pet.sleep or local_hour() >= 19 or local_hour() < 6
  if w.night == night then return end
  w.night = night
  ui.style(w.stage, { bg = night and C.night or C.day })
  ui.style(w.floor, { bg = night and C.floor_night or C.floor_day })
  ui.set_visible(w.moon, night)
  ui.set_visible(w.sun, not night)
end

-- ===================== Panel =====================
local function say(text)
  ui.set_text(w.speech, text)
  ui.set_visible(w.speech_box, true)
  ui.animate(w.speech_box, { opa = 100, ms = 150 })
  w.speech_until = gach.sys.uptime_ms() + 2500
end

local function stat_color(v, base)
  if v < 20 then return 0xFF453A end
  return base
end

local function refresh_panel()
  local sp = species_of(pet.species)
  local idx = stage_index()
  local label, _ = mood()
  ui.set_text(w.name, pet.name)
  ui.set_text(w.sub, string.format("%s  -  %s  -  %s old", sp.name, STAGES[idx].name, fmt_age(clock() - pet.born)))
  ui.set_text(w.mood, (idx == 1) and "Mood: Waiting to hatch..." or ("Mood: " .. label))
  for _, k in ipairs({ "hunger", "happy", "energy", "clean" }) do
    ui.set_value(w.bars[k], math.floor(pet[k]))
    ui.set_accent(w.bars[k], stat_color(pet[k], C[k]))
    ui.set_text(w.values[k], tostring(math.floor(pet[k])))
  end
  local egg = idx == 1
  for _, b in ipairs(w.actions) do ui.set_enabled(b, not egg) end
  ui.set_visible(w.medicine, pet.sick)
  ui.set_text(w.sleep_btn, pet.sleep and "Wake up" or "Sleep")
  if egg then
    local left = math.max(0, math.ceil(STAGES[2].min - (clock() - pet.born)))
    ui.set_text(w.hint, string.format("Hatching in %ds - tap the egg to keep it warm!", left))
  else
    ui.set_text(w.hint, "Tap to play, swipe to pet")
  end
end

-- ===================== Hành động =====================
local function act(fn)
  if not pet or stage_index() == 1 then return end
  fn()
  pet.happy, pet.hunger, pet.energy, pet.clean = clamp(pet.happy), clamp(pet.hunger), clamp(pet.energy),
    clamp(pet.clean)
  apply_face()
  draw_dirt()
  refresh_panel()
  save()
end

local function wake_if_sleeping()
  if pet.sleep then
    pet.sleep = false
    pet.happy = pet.happy - 5
    set_scene()
    say("Yawn... you woke me up!")
    return true
  end
  return false
end

function app.poke()
  if stage_index() == 1 then
    wobble_egg()
    say("*wiggle*")
    return
  end
  act(function()
    if wake_if_sleeping() then return end
    pet.happy = pet.happy + 4
    jump(36)
    hearts(3)
    say(({ "Hehe!", "Again!", "Yay!", "Boop!" })[math.random(4)])
  end)
end

function app.pet_stroke()
  if stage_index() == 1 then return end
  act(function()
    if wake_if_sleeping() then return end
    pet.happy = pet.happy + 6
    hearts(5)
    say("Purr... that feels nice")
  end)
end

local function feed()
  act(function()
    wake_if_sleeping()
    if pet.hunger >= 95 then
      pet.happy = pet.happy - 3
      say("I'm full!")
      return
    end
    pet.hunger, pet.happy = pet.hunger + 25, pet.happy + 4
    local fx = parts.x + parts.size // 2 - 8
    local snack = shape(w.fx_layer, fx, parts.y - 50, 16, 16, C.food)
    ui.animate(snack, { y = parts.y + parts.size // 2, opa = 0, ms = 600, ease = "in", done = function()
      ui.delete(snack)
      jump(14)
    end })
    say("Yum yum!")
  end)
end

local function play()
  act(function()
    wake_if_sleeping()
    if pet.energy < 15 then
      say("Too tired to play...")
      return
    end
    if pet.sick then
      say("I don't feel well...")
      return
    end
    pet.happy, pet.energy, pet.hunger = pet.happy + 20, pet.energy - 12, pet.hunger - 6
    jump(60, function() jump(30) end)
    hearts(6)
    say("Wheee!")
  end)
end

local function clean()
  act(function()
    wake_if_sleeping()
    pet.clean, pet.happy = 100, pet.happy + 3
    bubbles(10)
    say("Squeaky clean!")
  end)
end

local function toggle_sleep()
  act(function()
    if pet.sleep then
      pet.sleep = false
      say("Good morning!")
    else
      pet.sleep = true
      say("Good night...")
    end
    set_scene()
  end)
end

local function medicine()
  act(function()
    if not pet.sick then return end
    pet.sick = false
    pet.hunger = math.max(pet.hunger, CURED_LEVEL)
    pet.clean = math.max(pet.clean, CURED_LEVEL)
    pet.happy = pet.happy + 5
    bubbles(4)
    say("I feel better, thanks!")
  end)
end

-- ===================== Màn hình chính =====================
local function bar_row(parent, title, key)
  local r = ui.row(parent)
  ui.style(r, { width = "100%", gap = 8, align = "center" })
  local t = ui.label(r, title)
  ui.style(t, { color = C.dim, width = 86 })
  local b = ui.bar(r, 0, 100, 50)
  ui.style(b, { grow = 1, width = 0 })
  local v = ui.label(r, "")
  ui.style(v, { color = C.text, width = 34, text_align = "right" })
  w.bars[key], w.values[key] = b, v
end

local function build_main(root)
  ui.clear(root)
  w = { root = root, bars = {}, values = {}, actions = {} }
  local sw, sh = ui.screen_size()
  local body = ui.row(root)
  ui.style(body, { width = "100%", height = "100%", gap = 12 })

  -- Sân khấu
  local panel_w = 300
  w.stage_w = sw - panel_w - 12 - 2 * 12
  w.stage_h = sh - 2 * 12
  w.floor_y = math.floor(w.stage_h * 0.8)
  w.stage = ui.box(body)
  ui.style(w.stage, { width = w.stage_w, height = w.stage_h, radius = 18, bg = C.day })
  w.sun = shape(w.stage, w.stage_w - 90, 30, 46, 46, 0xFFD166)
  w.moon = shape(w.stage, w.stage_w - 90, 30, 40, 40, C.moon)
  w.floor = shape(w.stage, 0, w.floor_y - 6, w.stage_w, w.stage_h - w.floor_y + 6, C.floor_day, 0)
  w.dirt_layer = ui.box(w.stage)
  ui.style(w.dirt_layer, { width = w.stage_w, height = w.stage_h })
  w.pet_layer = ui.box(w.stage)
  ui.style(w.pet_layer, { width = w.stage_w, height = w.stage_h })
  w.fx_layer = ui.box(w.stage)
  ui.style(w.fx_layer, { width = w.stage_w, height = w.stage_h })
  w.speech_box = ui.box(w.stage)
  ui.style(w.speech_box, { bg = C.white, radius = 14, pad_x = 14, pad_y = 8, width = "content", height = "content" })
  ui.set_pos(w.speech_box, 16, 16)
  w.speech = ui.label(w.speech_box, "")
  ui.style(w.speech, { color = 0x1C1C1E, font = "medium" })
  ui.set_visible(w.speech_box, false)

  -- Panel
  local side = ui.column(body)
  ui.style(side, { width = panel_w, height = "100%", bg = C.panel, radius = 18, pad = 16, gap = 10 })
  w.name = ui.label(side, "")
  ui.style(w.name, { font = "large", color = C.text })
  w.sub = ui.label(side, "")
  ui.style(w.sub, { color = C.dim })
  w.mood = ui.label(side, "")
  ui.style(w.mood, { color = C.text, font = "medium" })
  bar_row(side, "Food", "hunger")
  bar_row(side, "Fun", "happy")
  bar_row(side, "Energy", "energy")
  bar_row(side, "Clean", "clean")

  local grid = ui.row(side)
  ui.style(grid, { width = "100%", gap = 8 })
  local function action(text, icon, fn, variant)
    local b = ui.button(grid, { text = text, icon = icon, variant = variant or "tinted" }, fn)
    ui.style(b, { width = "48%" })
    w.actions[#w.actions + 1] = b
    return b
  end
  action("Feed", S.PLUS, feed)
  action("Play", S.PLAY, play)
  action("Clean", S.TINT, clean)
  w.sleep_btn = action("Sleep", S.POWER, toggle_sleep)
  w.medicine = ui.button(side, { text = "Medicine", icon = S.CHARGE, variant = "destructive" }, medicine)
  ui.style(w.medicine, { width = "100%" })
  w.hint = ui.label(side, "")
  ui.style(w.hint, { color = C.dim })
  local spacer = ui.box(side)
  ui.style(spacer, { grow = 1, width = "100%" })
  local new_btn = ui.button(side, { text = "New pet", icon = S.REFRESH, variant = "plain", size = "sm" }, function()
    ui.alert { title = "Start over?", message = pet.name .. " will be released to the wild.",
               buttons = { "Cancel", "New pet" }, destructive = true,
               fn = function(i) if i == 2 then app.adopt_screen() end end }
  end)
  ui.style(new_btn, { width = "100%" })

  redraw_pet()
  set_scene()
  draw_dirt()
  refresh_panel()
end

-- ===================== Chọn pet =====================
function app.adopt_screen()
  clear_parts()
  local root = w.root
  ui.clear(root)
  w = { root = root }
  storage.set("pet", "")
  pet = nil
  local chosen = 1

  local title = ui.label(root, "Choose your pet")
  ui.style(title, { font = "large", color = C.text })
  local hint = ui.label(root, "Each pet hatches from an egg and grows up over real time.")
  ui.style(hint, { color = C.dim })

  local cards_row = ui.row(root)
  ui.style(cards_row, { width = "100%", gap = 12 })
  local cards = {}
  local function highlight()
    for i, c in ipairs(cards) do ui.style(c, { border = i == chosen and 3 or 0 }) end
  end
  for i, sp in ipairs(SPECIES) do
    local card = ui.column(cards_row)
    ui.style(card, { grow = 1, width = 0, bg = C.panel, radius = 18, pad = 12, gap = 6, border_color = sp.accent,
                     align = "center" })
    local preview = ui.box(card)
    ui.style(preview, { width = 140, height = 140 })
    draw_pet(preview, sp, 4, 130, 70, 136)
    local n = ui.label(card, sp.name)
    ui.style(n, { font = "medium", color = C.text })
    local t = ui.label(card, sp.trait)
    ui.style(t, { color = C.dim })
    ui.on(card, "click", function() chosen = i; highlight() end)
    cards[i] = card
  end
  highlight()

  local form = ui.row(root)
  ui.style(form, { width = "100%", gap = 12, align = "center" })
  local name = ui.textfield(form, { placeholder = "Name your pet", max = 14, width = 320 })
  local adopt = ui.button(form, { text = "Adopt", icon = S.OK, size = "lg" }, function()
    local sp = SPECIES[chosen]
    local typed = ui.get_text(name):gsub("|", "")
    local t = clock()
    pet = { species = sp.id, name = (typed ~= "" and typed or sp.name), born = t, last = t,
            hunger = 80, happy = 80, energy = 80, clean = 100, sick = false, sleep = false }
    save()
    build_main(root)
    say("An egg! It's moving...")
  end)
  return adopt
end

-- ===================== Nhịp 1 giây =====================
local function tick()
  if not pet then return end
  local t = clock()
  local before_stage = parts and parts.stage
  apply_time(t - pet.last)
  pet.last = t
  since_save = since_save + 1
  if since_save >= SAVE_EVERY then save() end

  local idx = stage_index()
  if parts and before_stage ~= idx then
    redraw_pet()
    if idx == 2 then say("It hatched! Hello, " .. pet.name .. "!") else say(pet.name .. " grew up!") end
    if idx > 1 then hearts(6) end
  end
  if w.speech_until and gach.sys.uptime_ms() > w.speech_until then
    w.speech_until = nil
    ui.animate(w.speech_box, { opa = 0, ms = 250, done = function() ui.set_visible(w.speech_box, false) end })
  end

  blink_in, zzz_in, wobble_in = blink_in - 1, zzz_in - 1, wobble_in - 1
  if parts and parts.egg and wobble_in <= 0 then wobble_egg(); wobble_in = math.random(3, 6) end
  if pet.sleep and zzz_in <= 0 then zzz(); zzz_in = 2 end
  if blink_in <= 0 then blink(); blink_in = math.random(3, 6) end
  if (t % 2) < 1 then bob() end
  apply_face()
  set_scene()
  draw_dirt()
  refresh_panel()
end

-- ===================== Vòng đời =====================
function app.on_create(root)
  math.randomseed(gach.sys.uptime_ms())
  ui.style(root, { bg = C.bg, pad = 12, gap = 12 })
  w.root = root
  pet = load()
  if pet and pet.species ~= "" then
    apply_time(clock() - pet.last) -- Thời gian đã trôi qua khi app đóng.
    save()                          -- Lưu ngay: đóng app sớm cũng không mất phần tính bù.
    build_main(root)
    local _, m = mood()
    say((m >= 1 and "Welcome back!") or (pet.sick and "I don't feel well...") or "I missed you!")
  else
    app.adopt_screen()
  end
end

function app.on_resume()
  if not tick_id then tick_id = timer.every(TICK_MS, tick) end
end

function app.on_pause()
  if tick_id then timer.cancel(tick_id); tick_id = nil end
  save()
end

function app.on_destroy()
  save()
end

return app
