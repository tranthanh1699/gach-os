-- Calculator: máy tính kiểu điện thoại / máy tính bảng (gach SDK v3).
--   - Ưu tiên toán tử: 2 + 3 x 4 = 14, 2 x 3 ^ 2 = 18
--   - "=" lặp lại phép cuối: 5 + 3 = = -> 11
--   - Đổi toán tử khi chưa nhập số: 2 + x 3 = 6
--   - %: x/100, hoặc a +/- b% = a +/- a*b/100
--   - AC xóa hết, C chỉ xóa số đang nhập, <- xóa chữ số cuối
--   - Màn rộng (máy tính bảng): thêm phím khoa học + lịch sử (lưu lại sau khi tắt máy, chạm để dùng lại)
local ui, S = gach.ui, gach.ui.SYMBOL

local app = {}

-- ===================== Logic =====================
local MAX_DIGITS = 12
local LIMIT = 1e100
local MAX_FACTORIAL = 170
local PREC = { ["+"] = 1, ["-"] = 1, ["*"] = 2, ["/"] = 2, ["^"] = 3 }
local OP_TEXT = { ["+"] = "+", ["-"] = "-", ["*"] = "x", ["/"] = "/", ["^"] = "^" }

local st = {}
local function reset()
  st.entry, st.typing, st.has_entry = "0", false, false
  st.value = 0.0
  st.stack = {}                 -- { {v = số, op = toán tử}, ... } chờ tính theo độ ưu tiên
  st.last_op, st.last_operand = nil, 0.0
  st.highlight, st.error = nil, false
end
reset()
st.ans, st.deg = 0.0, true

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
  elseif op == "^" then r = a ^ b
  else r = b end
  if bad(r) then st.error = true; return nil end
  return r
end

local function set_result(v)
  if bad(v) then st.error = true; return end
  st.value, st.typing = v, false
end

-- Đặt một số làm toán hạng hiện tại (hằng số, kết quả hàm, lịch sử).
local function set_operand(v)
  if st.error then reset() end
  if bad(v) then st.error = true; return end
  st.value, st.typing, st.has_entry, st.highlight = v, false, true, nil
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

local function backspace()
  if st.error then reset(); return end
  if not st.typing then return end
  st.entry = st.entry:sub(1, -2)
  if st.entry == "" or st.entry == "-" then st.entry = "0" end
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

local function top() return st.stack[#st.stack] end

local function percent()
  if st.error then return end
  local v = current()
  local t = top()
  if t and (t.op == "+" or t.op == "-") then v = t.v * v / 100 else v = v / 100 end
  set_operand(v)
end

-- Tính dồn các phép trong ngăn xếp có độ ưu tiên >= min_prec ("^" kết hợp phải nên dùng > cho chính nó).
local function reduce(v, min_prec, right_assoc_op)
  while #st.stack > 0 do
    local t = top()
    local p = PREC[t.op]
    if p < min_prec or (right_assoc_op and p == min_prec and t.op == right_assoc_op) then break end
    v = compute(t.v, t.op, v); if not v then return nil end
    st.stack[#st.stack] = nil
  end
  return v
end

local function operator(op)
  if st.error then return end
  local v
  if not st.typing and st.highlight then       -- đổi toán tử vừa chọn: lấy lại toán hạng trái
    v = top().v
    st.stack[#st.stack] = nil
  else
    v = current()
  end
  st.typing = false
  v = reduce(v, PREC[op], op == "^" and "^" or nil); if not v then return end
  st.stack[#st.stack + 1] = { v = v, op = op }
  st.value, st.highlight = v, op
end

-- ---- Lịch sử ----
local HISTORY_MAX = 12
local history = {}               -- { {text = "2+3 = 5", value = 5}, ... } mới nhất ở cuối
local on_history_changed         -- gán ở phần giao diện

local function format_number(v)
  if v == 0 then return "0" end
  local s = string.format("%.10g", v)
  local mantissa, exp = s:match("^(.-)e([-+]%d+)$")
  if mantissa then return mantissa .. "e" .. tostring(tonumber(exp)) end
  local sign, int, rest = s:match("^(-?)(%d*)(.*)$")
  int = int:reverse():gsub("(%d%d%d)", "%1,"):reverse()
  if int:sub(1, 1) == "," then int = int:sub(2) end
  return sign .. int .. rest
end

local function expression_text(final_operand)
  local parts = {}
  for _, t in ipairs(st.stack) do
    parts[#parts + 1] = format_number(t.v) .. " " .. OP_TEXT[t.op]
  end
  if final_operand then parts[#parts + 1] = format_number(final_operand) end
  return table.concat(parts, " ")
end

-- Lưu kiểu bộ đệm vòng: mỗi phép tính chỉ ghi 1 ô + 2 chỉ số (ghi thẻ SD chậm, tránh ghi lại cả 12 ô).
--   hc = số mục, hp = ô sẽ ghi kế tiếp (1..HISTORY_MAX), h<i> = "biểu thức|giá trị"
local history_next = 1

local function load_history()
  local count = tonumber(gach.storage.get("hc") or "") or 0
  history_next = tonumber(gach.storage.get("hp") or "") or 1
  if count > HISTORY_MAX or history_next < 1 or history_next > HISTORY_MAX then count, history_next = 0, 1 end
  for k = count, 1, -1 do                       -- cũ nhất trước
    local slot = (history_next - k - 1) % HISTORY_MAX + 1
    local raw = gach.storage.get("h" .. slot)
    local text, value = (raw or ""):match("^(.*)|([^|]+)$")
    if text and tonumber(value) then history[#history + 1] = { text = text, value = tonumber(value) } end
  end
end

local function clear_history()
  history, history_next = {}, 1
  gach.storage.set("hc", "0")
  gach.storage.set("hp", "1")
end

local function add_history(text, value)
  history[#history + 1] = { text = text, value = value }
  while #history > HISTORY_MAX do table.remove(history, 1) end
  gach.storage.set("h" .. history_next, text .. "|" .. string.format("%.17g", value))
  history_next = history_next % HISTORY_MAX + 1
  gach.storage.set("hp", tostring(history_next))
  gach.storage.set("hc", tostring(#history))
  if on_history_changed then on_history_changed() end
end

local function equals()
  if st.error then return end
  local v = current()
  local expr
  st.typing = false
  if #st.stack > 0 then
    st.last_op, st.last_operand = top().op, v
    expr = expression_text(v)
    v = reduce(v, 0); if not v then return end
  elseif st.last_op then
    expr = format_number(v) .. " " .. OP_TEXT[st.last_op] .. " " .. format_number(st.last_operand)
    v = compute(v, st.last_op, st.last_operand); if not v then return end
  end
  set_result(v)
  st.highlight = nil
  if expr and not st.error then
    st.ans = v
    add_history(expr .. " = " .. format_number(v), v)
  end
end

local function clear()
  if st.error or not st.has_entry then reset(); return end   -- AC
  st.entry, st.typing, st.has_entry, st.value = "0", false, false, 0.0  -- C
  local t = top()
  st.highlight = t and t.op or nil
end

-- ---- Hàm khoa học (áp lên số đang hiển thị) ----
local function factorial(v)
  if v < 0 or v ~= math.floor(v) or v > MAX_FACTORIAL then return 0 / 0 end
  local r = 1.0
  for i = 2, v do r = r * i end
  return r
end

local function angle(v) return st.deg and math.rad(v) or v end

local UNARY = {
  sq = function(v) return v * v end,
  cube = function(v) return v * v * v end,
  sqrt = function(v) return v >= 0 and math.sqrt(v) or 0 / 0 end,
  inv = function(v) return v ~= 0 and 1 / v or 0 / 0 end,
  sin = function(v) return math.sin(angle(v)) end,
  cos = function(v) return math.cos(angle(v)) end,
  tan = function(v) return math.tan(angle(v)) end,
  ln = function(v) return v > 0 and math.log(v) or 0 / 0 end,
  log = function(v) return v > 0 and math.log(v, 10) or 0 / 0 end,
  exp = function(v) return math.exp(v) end,
  pow10 = function(v) return 10 ^ v end,
  fact = factorial,
  abs = function(v) return math.abs(v) end,
}

local function unary(name)
  if st.error then return end
  local r = UNARY[name](current())
  if math.abs(r) < 1e-15 then r = 0.0 end     -- sin(180°) = 1.2e-16 -> 0
  set_operand(r)
end

-- ===================== Hiển thị =====================
local function display_text()
  if st.error then return "Error" end
  if st.typing then
    local sign, int, rest = st.entry:match("^(-?)(%d*)(.*)$")
    int = int:reverse():gsub("(%d%d%d)", "%1,"):reverse()
    if int:sub(1, 1) == "," then int = int:sub(2) end
    return sign .. int .. rest
  end
  return format_number(st.value)
end

-- ===================== Giao diện =====================
local BLACK, WHITE, MUTED = 0x000000, 0xFFFFFF, 0x8E8E93
local DIGIT_BG, FUNC_BG, OP_BG, SCI_BG, PANEL_BG = 0x333333, 0xA5A5A5, 0xFF9F0A, 0x1F1F1F, 0x1C1C1E
local WIDE_MIN_WIDTH = 480      -- dp: từ đây dùng bố cục khoa học + lịch sử
local GAP = 6

local wide = false
local display, expr_label, clear_btn, deg_btn, history_list
local op_buttons = {}   -- op -> { btn = handle, marks = {box handles của ký hiệu vẽ tay} }

local function refresh()
  if st.error then st.highlight, st.stack, st.has_entry = nil, {}, false end
  ui.set_text(display, display_text())
  ui.set_text(expr_label, st.error and "" or expression_text(nil))
  ui.set_text(clear_btn, st.has_entry and "C" or "AC")
  if deg_btn then ui.set_text(deg_btn, st.deg and "Deg" or "Rad") end
  for op, item in pairs(op_buttons) do
    local selected = (st.highlight == op)
    ui.set_bg(item.btn, selected and WHITE or OP_BG)
    ui.set_color(item.btn, selected and OP_BG or WHITE)
    for _, mark in ipairs(item.marks) do ui.set_bg(mark, selected and OP_BG or WHITE) end
  end
end

local function key(row, label, bg, fg, grow, action, font)
  local btn = ui.button(row, label, function() action(); refresh() end)
  ui.set_size(btn, 1, "100%")     -- rộng cơ sở 1, phần còn lại chia theo grow -> nút đều nhau
  ui.set_grow(btn, grow or 1)
  ui.set_radius(btn, wide and 14 or "round")   -- màn rộng: phím bo góc thay vì viên thuốc bị kéo dài
  ui.set_bg(btn, bg)
  ui.set_color(btn, fg)
  ui.set_font(btn, font or "medium")
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

local function sci_key(row, label, action)
  return key(row, label, SCI_BG, WHITE, 1, action, "small")
end

local function rebuild_history()
  if not history_list then return end
  ui.clear(history_list)
  if #history == 0 then
    ui.list_add(history_list, "No calculations yet")
    return
  end
  for i = #history, 1, -1 do             -- mới nhất ở trên
    local h = history[i]
    ui.list_add(history_list, h.text, function() set_operand(h.value); refresh() end)
  end
end
on_history_changed = rebuild_history

local function build_history(parent)
  local panel = ui.column(parent)
  ui.set_size(panel, "30%", "100%")
  ui.set_bg(panel, PANEL_BG)
  ui.set_radius(panel, 14)
  ui.set_pad(panel, 8)
  ui.set_gap(panel, 6)

  local header = ui.row(panel)
  ui.set_size(header, "100%", "content")
  local title = ui.label(header, "History")
  ui.set_font(title, "medium")
  ui.set_color(title, WHITE)
  ui.set_grow(title, 1)
  local clear_hist = ui.button(header, S.TRASH, function()
    clear_history()
    rebuild_history()
  end)
  ui.set_bg(clear_hist, SCI_BG)
  ui.set_color(clear_hist, MUTED)
  ui.set_radius(clear_hist, "round")

  history_list = ui.list(panel)
  ui.set_bg(history_list, PANEL_BG)
  rebuild_history()
end

local function build_display(parent)
  expr_label = ui.label(parent, "")
  ui.set_size(expr_label, "100%", "content")
  ui.set_align(expr_label, "right")
  ui.set_color(expr_label, MUTED)

  display = ui.label(parent, "0")
  ui.set_size(display, "100%", "content")
  ui.set_font(display, "large")
  ui.set_align(display, "right")
  ui.set_color(display, WHITE)
end

local function build_keypad(parent)
  local rows = {}
  for i = 1, 5 do
    rows[i] = ui.row(parent)
    ui.set_size(rows[i], "100%", "content")
    ui.set_grow(rows[i], 1)
    ui.set_gap(rows[i], GAP)
  end

  if wide then
    sci_key(rows[1], S.BACKSPACE, backspace)
    deg_btn = sci_key(rows[1], "Deg", function() st.deg = not st.deg end)
    sci_key(rows[1], "pi", function() set_operand(math.pi) end)
    sci_key(rows[1], "e", function() set_operand(math.exp(1)) end)
    sci_key(rows[2], "x^2", function() unary("sq") end)
    sci_key(rows[2], "x^3", function() unary("cube") end)
    sci_key(rows[2], "x^y", function() operator("^") end)
    sci_key(rows[2], "sqrt", function() unary("sqrt") end)
    sci_key(rows[3], "sin", function() unary("sin") end)
    sci_key(rows[3], "cos", function() unary("cos") end)
    sci_key(rows[3], "tan", function() unary("tan") end)
    sci_key(rows[3], "1/x", function() unary("inv") end)
    sci_key(rows[4], "ln", function() unary("ln") end)
    sci_key(rows[4], "log", function() unary("log") end)
    sci_key(rows[4], "e^x", function() unary("exp") end)
    sci_key(rows[4], "10^x", function() unary("pow10") end)
    sci_key(rows[5], "Ans", function() set_operand(st.ans) end)
    sci_key(rows[5], "Rand", function() set_operand(math.random()) end)
    sci_key(rows[5], "n!", function() unary("fact") end)
    sci_key(rows[5], "|x|", function() unary("abs") end)
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
end

function app.on_create(root)
  local width = ui.screen_size()
  wide = width >= WIDE_MIN_WIDTH
  math.randomseed(gach.sys.uptime_ms())
  load_history()

  ui.set_bg(root, BLACK)
  ui.set_pad(root, wide and 10 or 4)
  ui.set_gap(root, wide and 10 or 4)

  local calc = root
  if wide then
    -- Máy tính bảng: [lịch sử] [màn hình + bàn phím khoa học]. Root là cột -> thêm một hàng chiếm hết.
    local body = ui.row(root)
    ui.set_size(body, "100%", "100%")
    ui.set_grow(body, 1)
    ui.set_gap(body, 12)
    build_history(body)
    calc = ui.column(body)
    ui.set_size(calc, 1, "100%")
    ui.set_grow(calc, 1)
    ui.set_gap(calc, GAP)
  end

  build_display(calc)
  build_keypad(calc)
  refresh()
end

-- Nhấn Back khi đang nhập: xóa chữ số cuối thay vì thoát app (tiện trên màn nhỏ không có phím <-).
function app.on_back()
  if st.typing and st.entry ~= "0" then
    backspace()
    refresh()
    return true
  end
  return false
end

return app
