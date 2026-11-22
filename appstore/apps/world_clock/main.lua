-- World Clock: giờ hiện tại (cần Wi-Fi để đồng bộ NTP) ở vài thành phố.
local ui, timer = gach.ui, gach.timer

local app = {}
local CITIES = {
  { "Ha Noi", 7 }, { "Tokyo", 9 }, { "London", 0 }, { "New York", -5 },
}
local rows, clock_label, tick = {}, nil, nil

local function two(n) return string.format("%02d", n) end

local function refresh()
  local t = gach.sys.time()
  if not t then
    ui.set_text(clock_label, "Time not synced - connect Wi-Fi")
    return
  end
  ui.set_text(clock_label, string.format("%d-%s-%s %s:%s:%s", t.year, two(t.month), two(t.day),
                                          two(t.hour), two(t.min), two(t.sec)))
  -- t là giờ địa phương của máy; t.utc_offset (phút) cho biết múi giờ đang chọn trong Settings.
  local utc_hour = t.hour - t.utc_offset // 60
  for i, city in ipairs(CITIES) do
    local h = (utc_hour + city[2]) % 24
    ui.set_text(rows[i], string.format("%-9s %s:%s", city[1], two(h), two(t.min)))
  end
end

function app.on_create(root)
  clock_label = ui.label(root, "")
  ui.set_font(clock_label, "medium")
  local list = ui.list(root)
  for i, city in ipairs(CITIES) do
    rows[i] = ui.list_add(list, city[1])
  end
  ui.button(root, "Close", function() gach.app.exit() end)
  refresh()
end

function app.on_resume()
  tick = timer.every(1000, refresh)
end

function app.on_pause()
  if tick then timer.cancel(tick); tick = nil end
end

return app
