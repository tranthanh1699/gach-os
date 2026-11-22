-- Calculator: máy tính bỏ túi kiểu điện thoại (gach SDK v2).
--   - Ưu tiên toán tử: 2 + 3 x 4 = 14
--   - "=" lặp lại phép cuối: 5 + 3 = = -> 11
--   - Đổi toán tử khi chưa nhập số: 2 + x 3 = 6
--   - %: x/100, hoặc a +/- b% = a +/- a*b/100
--   - AC xóa hết, C chỉ xóa số đang nhập
--   - Tối đa 9 chữ số, nhóm hàng nghìn, số quá lớn/nhỏ hiển thị dạng 1.5e12
local ui, S = gach.ui, gach.ui.SYMBOL

local app = {}

-- ===================== Logic =====================
local MAX_DIGITS = 9
local LIMIT = 1e100

local st = {}
local function reset()
  st.entry, st.typing, st.has_entry = "0", false, false
  st.value = 0.0
  st.add_acc, st.add_op = 0.0, nil
  st.mul_acc, st.mul_op = 0.0, nil
  st.last_op, st.last_operand = nil, 0.0
  st.highlight, st.error = nil, false
end
reset()

local function current() return st.typing and (tonumber(st.entry) * 1.0) or st.value end

local function bad(v) return v ~= v or math.abs(v) >= LIMIT end

local function compute(a, op, b)
  local r
  if op == "+" then r = a + b
  elseif op == "-" then r = a - b
  elseif op == "*" then r = a * b
  elseif op == "/" then
    if b == 0 then st.error = true; return nil end
    r = a / b
  else r = b end
  if bad(r) then st.error = true; return nil end
  return r
end

local function set_result(v)
  if bad(v) then st.error = true; return end
  st.value, st.typing = v, false
end

local function digits_in(s) local _, n = s:gsub("%d", ""); return n end

local function input_digit(d)
  if st.error then reset() end
  if not st.typing then st.entry, st.typing = "0", true end
  if digits_in(st.entry) >= MAX_DIGITS then return end
  if st.entry == "0" then st.entry = d
  elseif st.entry == "-0" then st.entry = "-" .. d
  else st.entry = st.entry .. d end
  st.has_entry, st.highlight = true, nil
end

local function input_dot()
  if st.error then reset() end
  if not st.typing then st.entry, st.typing = "0", true end
  if not st.entry:find(".", 1, true) then st.entry = st.entry .. "." end
  st.has_entry, st.highlight = true, nil
end

local function toggle_sign()
  if st.error then return end
  if st.typing then
    st.entry = (st.entry:sub(1, 1) == "-") and st.entry:sub(2) or ("-" .. st.entry)
  elseif st.highlight then          -- vừa chọn toán tử: bắt đầu nhập số âm
    st.entry, st.typing, st.has_entry, st.highlight = "-0", true, true, nil
  else
    st.value = -st.value
  end
end

local function percent()
  if st.error then return end
  local v = current()
  if st.add_op and not st.mul_op then v = st.add_acc * v / 100 else v = v / 100 end
  set_result(v)
  st.highlight = nil
end

local function operator(op)
  if st.error then return end
  local v
  if not st.typing and st.highlight then       -- đổi toán tử vừa chọn
    if st.mul_op then v, st.mul_op = st.mul_acc, nil else v, st.add_op = st.add_acc, nil end
  else
    v = current()
  end
  st.typing = false
  if st.mul_op then
    v = compute(st.mul_acc, st.mul_op, v); if not v then return end
    st.mul_op = nil
  end
  if op == "*" or op == "/" then
    st.mul_acc, st.mul_op = v, op
  else
    if st.add_op then v = compute(st.add_acc, st.add_op, v); if not v then return end end
    st.add_acc, st.add_op = v, op
  end
  st.value, st.highlight = v, op
end

local function equals()
  if st.error then return end
  local v = current()
  st.typing = false
  if st.mul_op or st.add_op then
    st.last_op, st.last_operand = st.mul_op or st.add_op, v
    if st.mul_op then v = compute(st.mul_acc, st.mul_op, v); if not v then return end end
    if st.add_op then v = compute(st.add_acc, st.add_op, v); if not v then return end end
    st.mul_op, st.add_op = nil, nil
  elseif st.last_op then
    v = compute(v, st.last_op, st.last_operand); if not v then return end
  end
  set_result(v)
  st.highlight = nil
end

local function clear()
  if st.error or not st.has_entry then reset(); return end   -- AC
  st.entry, st.typing, st.has_entry, st.value = "0", false, false, 0.0  -- C
  st.highlight = st.mul_op or st.add_op
end

-- ===================== Hiển thị =====================
local function group(s)
  local sign, int, rest = s:match("^(-?)(%d*)(.*)$")
  int = int:reverse():gsub("(%d%d%d)", "%1,"):reverse()
  if int:sub(1, 1) == "," then int = int:sub(2) end
  return sign .. int .. rest
end

local function format_number(v)
  if v == 0 then return "0" end
  local s = string.format("%.9g", v)
  local mantissa, exp = s:match("^(.-)e([-+]%d+)$")
  if mantissa then return mantissa .. "e" .. tostring(tonumber(exp)) end
  return group(s)
end

local function display_text()
  if st.error then return "Error" end
  if st.typing then return group(st.entry) end
  return format_number(st.value)
end

-- ===================== Giao diện =====================
local BLACK, WHITE = 0x000000, 0xFFFFFF
local DIGIT_BG, FUNC_BG, OP_BG = 0x333333, 0xA5A5A5, 0xFF9F0A

local display, clear_btn
local op_buttons = {}   -- op -> { btn = handle, marks = {box handles của ký hiệu vẽ tay} }

local function refresh()
  if st.error then st.highlight, st.add_op, st.mul_op, st.has_entry = nil, nil, nil, false end
  ui.set_text(display, display_text())
  ui.set_text(clear_btn, st.has_entry and "C" or "AC")
  for op, item in pairs(op_buttons) do
    local selected = (st.highlight == op)
    ui.set_bg(item.btn, selected and WHITE or OP_BG)
    ui.set_color(item.btn, selected and OP_BG or WHITE)
    for _, mark in ipairs(item.marks) do ui.set_bg(mark, selected and OP_BG or WHITE) end
  end
end

local function key(row, label, bg, fg, grow, action)
  local btn = ui.button(row, label, function() action(); refresh() end)
  ui.set_size(btn, 1, "100%")     -- rộng cơ sở 1px, phần còn lại chia theo grow -> nút đều nhau
  ui.set_grow(btn, grow or 1)
  ui.set_radius(btn, "round")
  ui.set_bg(btn, bg)
  ui.set_color(btn, fg)
  ui.set_font(btn, "medium")
  return btn
end

-- Ký hiệu chia (font hệ thống không có): chấm - gạch - chấm vẽ bằng box.
local function divide_mark(btn)
  local col = ui.column(btn)
  ui.set_size(col, "content", "content")
  ui.set_gap(col, 3)
  ui.center(col)
  local marks = {}
  for _, size in ipairs({ { 5, 5 }, { 18, 3 }, { 5, 5 } }) do
    local mark = ui.box(col)
    ui.set_size(mark, size[1], size[2])
    ui.set_radius(mark, "round")
    ui.set_bg(mark, WHITE)
    marks[#marks + 1] = mark
  end
  return marks
end

local function op_key(row, op, label)
  local btn = key(row, label, OP_BG, WHITE, 1, function() operator(op) end)
  local marks = {}
  if op == "/" then marks = divide_mark(btn) end
  op_buttons[op] = { btn = btn, marks = marks }
end

local function digit_key(row, d, grow)
  key(row, d, DIGIT_BG, WHITE, grow, function() input_digit(d) end)
end

function app.on_create(root)
  ui.set_bg(root, BLACK)
  ui.set_pad(root, 4)
  ui.set_gap(root, 4)

  display = ui.label(root, "0")
  ui.set_size(display, "100%", "content")
  ui.set_font(display, "large")
  ui.set_align(display, "right")
  ui.set_color(display, WHITE)

  local rows = {}
  for i = 1, 5 do
    rows[i] = ui.row(root)
    ui.set_size(rows[i], "100%", "content")
    ui.set_grow(rows[i], 1)
    ui.set_gap(rows[i], 4)
  end

  clear_btn = key(rows[1], "AC", FUNC_BG, BLACK, 1, clear)
  key(rows[1], "+/-", FUNC_BG, BLACK, 1, toggle_sign)
  key(rows[1], "%", FUNC_BG, BLACK, 1, percent)
  op_key(rows[1], "/", "")

  digit_key(rows[2], "7"); digit_key(rows[2], "8"); digit_key(rows[2], "9"); op_key(rows[2], "*", S.CLOSE)
  digit_key(rows[3], "4"); digit_key(rows[3], "5"); digit_key(rows[3], "6"); op_key(rows[3], "-", S.MINUS)
  digit_key(rows[4], "1"); digit_key(rows[4], "2"); digit_key(rows[4], "3"); op_key(rows[4], "+", S.PLUS)
  digit_key(rows[5], "0", 2)
  key(rows[5], ".", DIGIT_BG, WHITE, 1, input_dot)
  key(rows[5], "=", OP_BG, WHITE, 1, equals)

  refresh()
end

return app
