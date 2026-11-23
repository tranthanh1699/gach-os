-- 2048 (gach SDK v3): vuốt lên/xuống/trái/phải để dồn ô, hai ô cùng số chạm nhau gộp thành một.
--   - Mỗi lượt sinh một ô mới (90% là 2, 10% là 4); đạt ô 2048 là thắng, vẫn vuốt tiếp để lên điểm cao hơn
--   - Hết ô trống và không còn cặp gộp được là thua
--   - Ván đang chơi và điểm cao nhất được lưu lại (tắt máy mở lại vẫn chơi tiếp)
--   - Có 4 nút mũi tên cho ai thích bấm thay vì vuốt
local ui, S = gach.ui, gach.ui.SYMBOL

local app = {}

-- ===================== Logic =====================
local N = 4
local WIN_VALUE = 2048
local FOUR_CHANCE = 0.1

local grid = {}            -- grid[r][c] = 0 (trống) hoặc 2, 4, 8...
local score, best = 0, 0
local won, over = false, false

local function empty_cells()
  local cells = {}
  for r = 1, N do
    for c = 1, N do
      if grid[r][c] == 0 then cells[#cells + 1] = { r, c } end
    end
  end
  return cells
end

local function spawn()
  local cells = empty_cells()
  if #cells == 0 then return end
  local cell = cells[math.random(#cells)]
  grid[cell[1]][cell[2]] = (math.random() < FOUR_CHANCE) and 4 or 2
end

local function new_game()
  for r = 1, N do
    grid[r] = {}
    for c = 1, N do grid[r][c] = 0 end
  end
  score, won, over = 0, false, false
  spawn(); spawn()
end

-- Dồn một hàng 4 ô về phía đầu (chỉ số 1), gộp mỗi cặp tối đa một lần. Trả về hàng mới + điểm cộng.
local function slide_line(line)
  local values = {}
  for i = 1, N do
    if line[i] ~= 0 then values[#values + 1] = line[i] end
  end
  local out, gained, i = {}, 0, 1
  while i <= #values do
    if values[i + 1] and values[i] == values[i + 1] then
      local merged = values[i] * 2
      out[#out + 1] = merged
      gained = gained + merged
      i = i + 2
    else
      out[#out + 1] = values[i]
      i = i + 1
    end
  end
  for k = #out + 1, N do out[k] = 0 end
  return out, gained
end

-- Đọc / ghi hàng thứ k theo hướng: phần tử 1 luôn là ô ở phía đích (hướng vuốt tới).
local function get_line(dir, k)
  local line = {}
  for i = 1, N do
    if dir == "left" then line[i] = grid[k][i]
    elseif dir == "right" then line[i] = grid[k][N + 1 - i]
    elseif dir == "up" then line[i] = grid[i][k]
    else line[i] = grid[N + 1 - i][k] end           -- down
  end
  return line
end

local function set_line(dir, k, line)
  for i = 1, N do
    if dir == "left" then grid[k][i] = line[i]
    elseif dir == "right" then grid[k][N + 1 - i] = line[i]
    elseif dir == "up" then grid[i][k] = line[i]
    else grid[N + 1 - i][k] = line[i] end
  end
end

local function can_move()
  for r = 1, N do
    for c = 1, N do
      local v = grid[r][c]
      if v == 0 then return true end
      if c < N and grid[r][c + 1] == v then return true end
      if r < N and grid[r + 1][c] == v then return true end
    end
  end
  return false
end

local function has_tile(value)
  for r = 1, N do
    for c = 1, N do
      if grid[r][c] >= value then return true end
    end
  end
  return false
end

-- Một lượt chơi. Trả về true nếu bàn cờ thay đổi.
local function move(dir)
  if over then return false end
  local moved, gained = false, 0
  for k = 1, N do
    local before = get_line(dir, k)
    local after, g = slide_line(before)
    for i = 1, N do
      if before[i] ~= after[i] then moved = true end
    end
    set_line(dir, k, after)
    gained = gained + g
  end
  if not moved then return false end
  score = score + gained
  if score > best then best = score end
  spawn()
  if not won and has_tile(WIN_VALUE) then won = true end
  if not can_move() then over = true end
  return true
end

-- ===================== Lưu trữ =====================
-- "state" = "điểm|cờ thắng|16 ô cách nhau dấu phẩy" (<= 128 ký tự), "best" = điểm cao nhất.
local saved_best = 0
local function save()
  local cells = {}
  for r = 1, N do
    for c = 1, N do cells[#cells + 1] = tostring(grid[r][c]) end
  end
  local flags = won and "w" or ""
  gach.storage.set("state", score .. "|" .. flags .. "|" .. table.concat(cells, ","))
  if best ~= saved_best then              -- ghi thẻ SD chậm: chỉ ghi điểm cao nhất khi đổi
    gach.storage.set("best", tostring(best))
    saved_best = best
  end
end

local function load()
  best = tonumber(gach.storage.get("best") or "") or 0
  saved_best = best
  local raw = gach.storage.get("state")
  local s, flags, cells = (raw or ""):match("^(%d+)|(%a*)|([%d,]+)$")
  if not s then return false end
  local values = {}
  for v in cells:gmatch("%d+") do values[#values + 1] = tonumber(v) end
  if #values ~= N * N then return false end
  for r = 1, N do
    grid[r] = {}
    for c = 1, N do grid[r][c] = values[(r - 1) * N + c] end
  end
  score = tonumber(s)
  won = flags:find("w") ~= nil
  over = not can_move()
  return true
end

-- ===================== Giao diện =====================
local BG, BOARD_BG, EMPTY_BG = 0xFAF8EF, 0xBBADA0, 0xCDC1B4
local DARK_TEXT, LIGHT_TEXT, SCORE_BG, BUTTON_BG = 0x776E65, 0xF9F6F2, 0xBBADA0, 0x8F7A66
local TILE_COLORS = {
  [2] = 0xEEE4DA, [4] = 0xEDE0C8, [8] = 0xF2B179, [16] = 0xF59563, [32] = 0xF67C5F, [64] = 0xF65E3B,
  [128] = 0xEDCF72, [256] = 0xEDCC61, [512] = 0xEDC850, [1024] = 0xEDC53F, [2048] = 0xEDC22E,
}
local SUPER_TILE = 0x3C3A32       -- > 2048
local FONT_SIZES = { { "large", 28 }, { "medium", 20 }, { "small", 14 } }   -- tên cỡ, chiều cao dp

local tile_box, tile_label = {}, {}
local score_label, best_label, message
local tile_size = 40

-- Chọn cỡ chữ lớn nhất mà số vẫn vừa ô (mỗi chữ số rộng ~0.6 chiều cao font).
local function font_for(value)
  local digits = #tostring(value)
  for _, f in ipairs(FONT_SIZES) do
    if digits * f[2] * 0.6 <= tile_size * 0.85 then return f[1] end
  end
  return "small"
end

local function refresh()
  for r = 1, N do
    for c = 1, N do
      local v = grid[r][c]
      local box, label = tile_box[r][c], tile_label[r][c]
      if v == 0 then
        ui.set_bg(box, EMPTY_BG)
        ui.set_text(label, "")
      else
        ui.set_bg(box, TILE_COLORS[v] or SUPER_TILE)
        ui.set_color(label, (v <= 4) and DARK_TEXT or LIGHT_TEXT)
        ui.set_font(label, font_for(v))
        ui.set_text(label, tostring(v))
      end
    end
  end
  ui.set_text(score_label, tostring(score))
  ui.set_text(best_label, tostring(best))
  if over then ui.set_text(message, "Game over!")
  elseif won then ui.set_text(message, "You win! Keep going")
  else ui.set_text(message, "Swipe to play") end
end

local function play(dir)
  if move(dir) then
    save()
    refresh()
  end
end

local function score_box(parent, title)
  local box = ui.column(parent)
  ui.set_size(box, 1, "content")
  ui.set_grow(box, 1)
  ui.set_bg(box, SCORE_BG)
  ui.set_radius(box, 6)
  ui.set_pad(box, 4)
  ui.set_gap(box, 0)
  local t = ui.label(box, title)
  ui.set_size(t, "100%", "content")
  ui.set_align(t, "center")
  ui.set_color(t, 0xEEE4DA)
  local v = ui.label(box, "0")
  ui.set_size(v, "100%", "content")
  ui.set_align(v, "center")
  ui.set_font(v, "medium")
  ui.set_color(v, LIGHT_TEXT)
  return v
end

local function action_button(parent, text, fn)
  local b = ui.button(parent, text, fn)
  ui.set_bg(b, BUTTON_BG)
  ui.set_color(b, LIGHT_TEXT)
  ui.set_radius(b, 6)
  return b
end

local function build_board(parent, size)
  local gap = math.max(4, math.floor(size / 50))
  tile_size = math.floor((size - gap * (N + 1)) / N)
  local board = ui.column(parent)
  ui.set_size(board, size, size)
  ui.set_bg(board, BOARD_BG)
  ui.set_radius(board, 8)
  ui.set_pad(board, gap)
  ui.set_gap(board, gap)
  for r = 1, N do
    tile_box[r], tile_label[r] = {}, {}
    local row = ui.row(board)
    ui.set_size(row, "100%", tile_size)
    ui.set_gap(row, gap)
    for c = 1, N do
      local box = ui.box(row)
      ui.set_size(box, tile_size, tile_size)
      ui.set_radius(box, 5)
      local label = ui.label(box, "")
      ui.center(label)
      tile_box[r][c], tile_label[r][c] = box, label
    end
  end
end

local function build_panel(parent)
  local panel = ui.column(parent)
  ui.set_size(panel, 1, "100%")
  ui.set_grow(panel, 1)
  ui.set_gap(panel, 6)

  local title = ui.label(panel, "2048")
  ui.set_font(title, "large")
  ui.set_color(title, DARK_TEXT)

  local scores = ui.row(panel)
  ui.set_size(scores, "100%", "content")
  ui.set_gap(scores, 6)
  score_label = score_box(scores, "SCORE")
  best_label = score_box(scores, "BEST")

  message = ui.label(panel, "")
  ui.set_size(message, "100%", "content")
  ui.set_color(message, DARK_TEXT)

  local buttons = ui.row(panel)
  ui.set_size(buttons, "100%", "content")
  ui.set_gap(buttons, 6)
  local new_btn = action_button(buttons, "New Game", function()
    new_game(); save(); refresh()
  end)
  ui.set_grow(new_btn, 1)

  local arrows = ui.row(panel)
  ui.set_size(arrows, "100%", "content")
  ui.set_gap(arrows, 6)
  for _, a in ipairs({ { S.LEFT, "left" }, { S.UP, "up" }, { S.DOWN, "down" }, { S.RIGHT, "right" } }) do
    local b = action_button(arrows, a[1], function() play(a[2]) end)
    ui.set_grow(b, 1)
  end
end

function app.on_create(root)
  math.randomseed(gach.sys.uptime_ms())
  if not load() then new_game() end

  local width, height = ui.screen_size()
  ui.set_bg(root, BG)
  local pad = math.max(4, math.floor(height / 40))
  ui.set_pad(root, pad)

  local body = ui.row(root)
  ui.set_size(body, "100%", "100%")
  ui.set_grow(body, 1)
  ui.set_gap(body, pad * 2)

  -- Bàn cờ vuông lớn nhất vừa chiều cao, chừa tối thiểu 40% chiều ngang cho bảng điểm / nút.
  local size = math.min(height - pad * 2, math.floor(width * 0.6))
  build_board(body, size)
  build_panel(body)

  ui.on_swipe(root, play)
  refresh()
end

return app
