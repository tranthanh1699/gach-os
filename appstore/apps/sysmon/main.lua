-- System Monitor (gach SDK v4): bảng điều khiển vận hành, cập nhật mỗi giây.
--   CPU     : tải từng core (cung tròn) + lịch sử 60 s
--   MEMORY  : RAM nội, PSRAM, heap LVGL (thanh) + lịch sử % sử dụng
--   STORAGE : firmware trong phân vùng app, thẻ SD
--   SYSTEM  : chip, xung nhịp, số task, nhiệt độ, tốc độ vẽ lại, Wi-Fi
local ui, timer = gach.ui, gach.timer

local app = {}

-- ===================== Giao diện: màu & kích thước (dp) =====================
local C = {
  bg = 0x000000, card = 0x1C1C1E, text = 0xFFFFFF, dim = 0x8E8E93,
  core0 = 0x0A84FF, core1 = 0xBF5AF2,
  ram = 0x30D158, psram = 0xFF9F0A, lvgl = 0x64D2FF,
  flash = 0xFFD60A, sd = 0xFF375F,
  warn = 0xFF9F0A, crit = 0xFF453A, ok = 0x30D158,
}
local PAD, GAP, RADIUS = 12, 10, 14
local ARC_SIZE, ARC_WIDTH = 104, 10
local HISTORY = 60          -- số điểm lịch sử (1 điểm / giây)
local PERIOD_MS = 1000
local WARN_PCT, CRIT_PCT = 60, 85

local w = {}                -- handle widget cần cập nhật
local tick = nil

-- ===================== Định dạng =====================
local KB, MB, GB = 1024, 1024 * 1024, 1024 * 1024 * 1024

local function fmt_bytes(n)
  if n >= GB then return string.format("%.1f GB", n / GB) end
  if n >= MB then return string.format("%.1f MB", n / MB) end
  return string.format("%d KB", n // KB)
end

local function fmt_used(used, total)
  return fmt_bytes(used) .. " / " .. fmt_bytes(total)
end

local function pct(used, total)
  if not total or total <= 0 then return 0 end
  return math.floor(used * 100 / total + 0.5)
end

local function fmt_uptime(ms)
  local s = ms // 1000
  local d, h, m = s // 86400, (s // 3600) % 24, (s // 60) % 60
  if d > 0 then return string.format("%dd %02dh %02dm", d, h, m) end
  return string.format("%02d:%02d:%02d", h, m, s % 60)
end

-- Màu theo ngưỡng tải: bình thường giữ màu riêng, cao -> cam, rất cao -> đỏ.
local function level_color(p, normal)
  if p >= CRIT_PCT then return C.crit end
  if p >= WARN_PCT then return C.warn end
  return normal
end

local function signal_text(rssi)
  local q = (rssi >= -55 and "Excellent") or (rssi >= -67 and "Good") or (rssi >= -75 and "Fair") or "Weak"
  return string.format("%d dBm  %s", rssi, q)
end

-- ===================== Khối dựng =====================
local function label(parent, text, color, font)
  local l = ui.label(parent, text or "")
  ui.set_color(l, color or C.text)
  if font then ui.set_font(l, font) end
  return l
end

-- Thẻ bo góc có tiêu đề nhỏ màu xám (kiểu iOS).
local function card(parent, title)
  local c = ui.column(parent)
  ui.set_bg(c, C.card)
  ui.set_radius(c, RADIUS)
  ui.set_pad(c, PAD)
  ui.set_gap(c, GAP)
  label(c, title, C.dim)
  return c
end

-- Cột chia đều chiều ngang của hàng chứa nó.
local function flex_column(parent)
  local col = ui.column(parent)
  ui.set_size(col, 0, "100%")
  ui.set_grow(col, 1)
  ui.set_gap(col, GAP)
  return col
end

-- Dòng "tên ........ giá trị".
local function kv(parent, name)
  local r = ui.row(parent)
  ui.set_gap(r, 4)
  local n = label(r, name, C.dim)
  ui.set_grow(n, 1)
  local v = label(r, "-")
  ui.set_align(v, "right")
  return v
end

-- Nhóm thanh: "tên  giá trị" phía trên, thanh màu phía dưới.
local function meter(parent, name, color)
  local value = kv(parent, name)
  local bar = ui.bar(parent, 0, 100)
  ui.set_accent(bar, color)
  return { value = value, bar = bar, color = color }
end

local function set_meter(m, used, total)
  if not total or total <= 0 then
    ui.set_text(m.value, "n/a")
    ui.set_value(m.bar, 0)
    return
  end
  local p = pct(used, total)
  ui.set_text(m.value, string.format("%s  %d%%", fmt_used(used, total), p))
  ui.set_value(m.bar, p)
  ui.set_accent(m.bar, level_color(p, m.color))
end

-- Cung tròn tải một core, phần trăm ở giữa, nhãn bên dưới.
local function core_gauge(parent, name, color)
  local col = flex_column(parent)
  ui.set_size(col, 0, "content")
  local arc = ui.arc(col, 0, 100, ARC_WIDTH)
  ui.set_size(arc, ARC_SIZE, ARC_SIZE)
  ui.set_accent(arc, color)
  local value = label(arc, "--", C.text, "medium")
  ui.center(value)
  label(col, name, color)
  return { arc = arc, value = value, color = color }
end

-- ===================== Các thẻ =====================
local function build_cpu(parent)
  local c = card(parent, "CPU")
  ui.set_size(c, "100%", 0)
  ui.set_grow(c, 1)
  local gauges = ui.row(c)
  w.core = { core_gauge(gauges, "Core 0", C.core0), core_gauge(gauges, "Core 1", C.core1) }
  w.cpu_caption = label(c, "Load history (60 s)", C.dim)
  w.cpu_chart = ui.chart(c, HISTORY, 0, 100)
  ui.set_size(w.cpu_chart, "100%", 0)
  ui.set_grow(w.cpu_chart, 1)
  w.cpu_series = { ui.chart_series(w.cpu_chart, C.core0), ui.chart_series(w.cpu_chart, C.core1) }
end

local function build_memory(parent)
  local c = card(parent, "MEMORY")
  ui.set_size(c, "100%", 0)
  ui.set_grow(c, 1)
  w.ram = meter(c, "Internal RAM", C.ram)
  w.psram = meter(c, "PSRAM", C.psram)
  w.lvgl = meter(c, "UI heap (LVGL)", C.lvgl)
  w.ram_detail = label(c, "", C.dim)
  w.mem_chart = ui.chart(c, HISTORY, 0, 100)
  ui.set_size(w.mem_chart, "100%", 0)
  ui.set_grow(w.mem_chart, 1)
  w.mem_series = { ui.chart_series(w.mem_chart, C.ram), ui.chart_series(w.mem_chart, C.psram) }
end

local function build_storage(parent)
  local c = card(parent, "STORAGE")
  ui.set_size(c, "100%", "content")
  w.flash = meter(c, "Firmware", C.flash)
  w.sd = meter(c, "SD card", C.sd)
end

local function build_system(parent)
  local c = card(parent, "SYSTEM")
  ui.set_size(c, "100%", 0)
  ui.set_grow(c, 1)
  w.chip = kv(c, "Chip")
  w.flash_size = kv(c, "Flash")
  w.tasks = kv(c, "Tasks")
  w.temp = kv(c, "Temperature")
  w.fps = kv(c, "Redraws")
  w.wifi = kv(c, "Wi-Fi")
  w.rssi = kv(c, "Signal")
  w.ip = kv(c, "IP address")
end

-- ===================== Cập nhật =====================
local function update_cpu(s)
  local load = s.cpu.load
  for i = 1, 2 do
    local g, p = w.core[i], load and load[i]
    if p then
      ui.set_value(g.arc, p)
      ui.set_accent(g.arc, level_color(p, g.color))
      ui.set_text(g.value, p .. "%")
      ui.chart_push(w.cpu_chart, w.cpu_series[i], p)
    else
      ui.set_text(g.value, (i > s.cpu.cores) and "off" or "n/a")
    end
  end
  ui.set_text(w.chip, string.format("%s  %d cores @ %d MHz", string.upper(s.chip), s.cpu.cores, s.cpu.mhz))
  ui.set_text(w.tasks, s.cpu.tasks and tostring(s.cpu.tasks) or "n/a")
end

local function update_memory(s)
  local ram, psram, lv = s.ram, s.psram, s.lvgl
  if ram then
    set_meter(w.ram, ram.total - ram.free, ram.total)
    ui.set_text(w.ram_detail, string.format("Low-water %s   Largest block %s",
                                            fmt_bytes(ram.min), fmt_bytes(ram.largest)))
    ui.chart_push(w.mem_chart, w.mem_series[1], pct(ram.total - ram.free, ram.total))
  end
  if psram then
    set_meter(w.psram, psram.total - psram.free, psram.total)
    ui.chart_push(w.mem_chart, w.mem_series[2], pct(psram.total - psram.free, psram.total))
  else
    set_meter(w.psram, 0, 0)
  end
  if lv then set_meter(w.lvgl, lv.total - lv.free, lv.total) end
end

local function update_storage(s)
  local f = s.flash
  if f then
    set_meter(w.flash, f.app_used, f.app_size)
    ui.set_text(w.flash_size, fmt_bytes(f.total))
  end
  if s.sd then
    set_meter(w.sd, s.sd.total - s.sd.free, s.sd.total)
  else
    ui.set_text(w.sd.value, "not inserted")
    ui.set_value(w.sd.bar, 0)
  end
end

local function update_system(s)
  ui.set_text(w.uptime, "Uptime " .. fmt_uptime(s.uptime_ms))
  if s.temp then
    ui.set_text(w.temp, string.format("%.1f °C", s.temp))
    ui.set_color(w.temp, (s.temp >= 70 and C.crit) or (s.temp >= 55 and C.warn) or C.text)
  else
    ui.set_text(w.temp, "n/a")
  end
  ui.set_text(w.fps, string.format("%.0f / s", s.fps))
  local wifi = s.wifi
  if not wifi then
    ui.set_text(w.wifi, "not available")
  elseif wifi.connected then
    ui.set_text(w.wifi, wifi.ssid)
    ui.set_color(w.wifi, C.ok)
  else
    ui.set_text(w.wifi, "disconnected")
    ui.set_color(w.wifi, C.dim)
  end
  ui.set_text(w.rssi, (wifi and wifi.rssi) and signal_text(wifi.rssi) or "-")
  ui.set_text(w.ip, (wifi and wifi.ip) or "-")
end

local function refresh()
  local s = gach.sys.stats()
  update_cpu(s)
  update_memory(s)
  update_storage(s)
  update_system(s)
end

-- ===================== Vòng đời =====================
function app.on_create(root)
  ui.set_bg(root, C.bg)
  ui.set_pad(root, PAD)
  ui.set_gap(root, GAP)

  local header = ui.row(root)
  local title = label(header, "System Monitor", C.text, "large")
  ui.set_grow(title, 1)
  w.uptime = label(header, "", C.dim)

  local body = ui.row(root)
  ui.set_size(body, "100%", 0)
  ui.set_grow(body, 1)
  ui.set_gap(body, GAP)
  build_cpu(flex_column(body))
  build_memory(flex_column(body))
  local right = flex_column(body)
  build_storage(right)
  build_system(right)

  refresh()
end

function app.on_resume()
  if not tick then tick = timer.every(PERIOD_MS, refresh) end
end

function app.on_pause()
  if tick then timer.cancel(tick); tick = nil end
end

return app
