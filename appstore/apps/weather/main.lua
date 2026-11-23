-- Weather (gach SDK v5): thời tiết hiện tại + dự báo theo giờ / 7 ngày cho thành phố chọn từ danh sách.
-- Nguồn dữ liệu: Open-Meteo (https://open-meteo.com, miễn phí, không cần API key). Làm mới mỗi 15 phút.
local ui, timer, net, json, storage = gach.ui, gach.timer, gach.net, gach.json, gach.storage

local app = {}

-- ===================== Dữ liệu tĩnh =====================
-- Font hệ thống chỉ có ASCII: tên thành phố viết không dấu.
local CITIES = {
  { "Ha Noi", 21.03, 105.85 },     { "Ho Chi Minh City", 10.82, 106.63 }, { "Da Nang", 16.05, 108.22 },
  { "Hai Phong", 20.86, 106.68 },  { "Hue", 16.46, 107.59 },              { "Can Tho", 10.04, 105.79 },
  { "Da Lat", 11.94, 108.44 },     { "Nha Trang", 12.24, 109.19 },        { "Tokyo", 35.68, 139.69 },
  { "Seoul", 37.57, 126.98 },      { "Singapore", 1.35, 103.82 },         { "Bangkok", 13.76, 100.50 },
  { "London", 51.51, -0.13 },      { "Paris", 48.86, 2.35 },              { "New York", 40.71, -74.01 },
  { "Sydney", -33.87, 151.21 },
}

local API = "https://api.open-meteo.com/v1/forecast?timezone=auto"
local MAIN_FIELDS = "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code," ..
  "wind_speed_10m,wind_direction_10m&hourly=temperature_2m,weather_code,precipitation_probability,is_day" ..
  "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset,uv_index_max,daylight_duration" ..
  "&forecast_days=7&forecast_hours=24"

local REFRESH_MS = 15 * 60 * 1000
local RETRY_MS = 30 * 1000
local HOURLY_STEP = 3              -- một ô mỗi 3 giờ
local SLOTS = 7                    -- số ô trong hàng giờ / tuần

-- ===================== Giao diện =====================
local C = {
  bg = 0x000000, card = 0x1C1C1E, tile = 0x2C2C2E, text = 0xFFFFFF, dim = 0x8E8E93,
  accent = 0x0A84FF, teal = 0x30D5C8, pill = 0x3A3A3C, pill_on = 0x0A84FF,
  sun = 0xFF9F0A, sun_core = 0xFFC53D, moon = 0xE5E5EA, cloud = 0xC7CDD6, cloud_dark = 0x6E7681,
  rain = 0x5AC8FA, bolt = 0xFFD60A, snow = 0xFFFFFF, fog = 0x8E8E93,
  ok = 0x30D158, warn = 0xFF9F0A, crit = 0xFF453A, purple = 0xBF5AF2,
}
local PAD, GAP, RADIUS = 12, 10, 16
local ICON_W, ICON_H = 130, 110

local state = {
  city = 1, unit = "C", mode = "today",
  main = nil, updated = nil, error = nil,
}
local w = {}
local tick = nil

-- ===================== Tiện ích =====================
local function clamp_int(x) return math.floor(x + 0.5) end

local function temp(c)
  if c == nil then return "--" end
  if state.unit == "F" then c = c * 9 / 5 + 32 end
  return string.format("%d°", clamp_int(c))
end

local WEEKDAY = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" }
local WEEKDAY_LONG = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" }
local MONTH = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" }

-- "2026-11-23..." -> năm, tháng, ngày, thứ (1 = Chủ nhật). Thuật toán Sakamoto.
local function parse_date(s)
  local y, m, d = tonumber(s:sub(1, 4)), tonumber(s:sub(6, 7)), tonumber(s:sub(9, 10))
  local t = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 }
  local yy = (m < 3) and (y - 1) or y
  local wd = (yy + yy // 4 - yy // 100 + yy // 400 + t[m] + d) % 7
  return y, m, d, wd + 1
end

local function hhmm(s) return s:sub(12, 16) end

-- Mã WMO -> nhóm icon + mô tả.
local function condition(code)
  if code == 0 then return "clear", "Clear sky" end
  if code == 1 then return "partly", "Mostly clear" end
  if code == 2 then return "partly", "Partly cloudy" end
  if code == 3 then return "cloudy", "Cloudy" end
  if code == 45 or code == 48 then return "fog", "Fog" end
  if code >= 51 and code <= 57 then return "rain", "Drizzle" end
  if code >= 61 and code <= 67 then return "rain", (code >= 65) and "Heavy rain" or "Rain" end
  if code >= 71 and code <= 77 then return "snow", "Snow" end
  if code >= 80 and code <= 82 then return "rain", "Showers" end
  if code == 85 or code == 86 then return "snow", "Snow showers" end
  if code >= 95 then return "thunder", "Thunderstorm" end
  return "cloudy", "Cloudy"
end

local function dot_color(kind, is_day)
  if kind == "clear" then return is_day and C.sun or C.moon end
  if kind == "partly" then return is_day and C.sun_core or C.cloud end
  if kind == "rain" then return C.rain end
  if kind == "thunder" then return C.purple end
  if kind == "snow" then return C.snow end
  return C.cloud_dark
end

local function compass(deg)
  local names = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" }
  return names[(clamp_int(deg / 45) % 8) + 1]
end

local function uv_level(uv)
  if uv < 3 then return "Low", C.ok end
  if uv < 6 then return "Moderate", C.bolt end
  if uv < 8 then return "High", C.warn end
  if uv < 11 then return "Very high", C.crit end
  return "Extreme", C.purple
end

-- ===================== Khối dựng =====================
local function label(parent, text, color, font)
  local l = ui.label(parent, text or "")
  ui.set_color(l, color or C.text)
  if font then ui.set_font(l, font) end
  return l
end

local function card(parent, color)
  local c = ui.column(parent)
  ui.set_bg(c, color or C.card)
  ui.set_radius(c, RADIUS)
  ui.set_pad(c, PAD)
  ui.set_gap(c, GAP)
  return c
end

local function grow(h, n, height)
  ui.set_size(h, 0, height or "content")
  ui.set_grow(h, n or 1)
  return h
end

local function blob(parent, x, y, bw, bh, color)
  local b = ui.box(parent)
  ui.set_size(b, bw, bh)
  ui.set_radius(b, "round")
  ui.set_bg(b, color)
  ui.set_pos(b, x, y)
end

local function cloud(parent, x, y, s, color)
  blob(parent, x, y + clamp_int(26 * s), clamp_int(92 * s), clamp_int(36 * s), color)
  blob(parent, x + clamp_int(12 * s), y + clamp_int(8 * s), clamp_int(42 * s), clamp_int(42 * s), color)
  blob(parent, x + clamp_int(34 * s), y, clamp_int(54 * s), clamp_int(54 * s), color)
end

-- Icon lớn vẽ bằng khối bo tròn trong hộp ICON_W x ICON_H (dp).
local function draw_icon(box, kind, is_day)
  ui.clear(box)
  if kind == "clear" then
    if is_day then
      blob(box, 22, 8, 92, 92, C.sun)
      blob(box, 36, 22, 64, 64, C.sun_core)
    else
      blob(box, 26, 12, 84, 84, C.moon)
      blob(box, 56, 2, 72, 72, C.card)       -- che bớt -> trăng khuyết
    end
  elseif kind == "partly" then
    blob(box, 52, 4, 66, 66, is_day and C.sun or C.moon)
    cloud(box, 4, 40, 1.15, C.cloud)
  elseif kind == "fog" then
    for i = 0, 2 do blob(box, 12 + i * 6, 28 + i * 24, 100, 12, C.fog) end
  else
    local dark = (kind == "thunder") or (kind == "rain")
    cloud(box, 10, 6, 1.2, dark and C.cloud_dark or C.cloud)
    if kind == "rain" then
      for i = 0, 2 do blob(box, 30 + i * 26, 80 + (i % 2) * 8, 9, 20, C.rain) end
    elseif kind == "thunder" then
      blob(box, 46, 80, 9, 24, C.bolt)
      blob(box, 70, 86, 9, 20, C.bolt)
    elseif kind == "snow" then
      for i = 0, 2 do blob(box, 30 + i * 26, 84 + (i % 2) * 8, 12, 12, C.snow) end
    end
  end
end

local function pill_button(parent, text, fn)
  local b = ui.button(parent, text, fn)
  ui.set_bg(b, C.pill)
  ui.set_radius(b, "round")
  ui.set_pad(b, 6)
  return b
end

-- ===================== Dựng dashboard =====================
local function build_main_card(parent)
  local c = card(parent)
  ui.set_size(c, "100%", "content")
  local r = ui.row(c)
  local left = grow(ui.column(r), 1)
  w.day = label(left, "", C.text, "large")
  w.date = label(left, "", C.dim)
  w.temp = label(left, "--", C.text, "large")
  w.hilo = label(left, "", C.dim)
  local right = ui.column(r)
  ui.set_size(right, "content", "content")
  w.icon = ui.box(right)
  ui.set_size(w.icon, ICON_W, ICON_H)
  w.cond = label(right, "", C.text, "medium")
  w.feels = label(right, "", C.dim)
end

local function set_mode(mode)
  state.mode = mode
  ui.set_bg(w.tab_today, mode == "today" and C.pill_on or C.pill)
  ui.set_bg(w.tab_week, mode == "week" and C.pill_on or C.pill)
  app.render()
end

local function build_forecast_card(parent)
  local c = card(parent)
  ui.set_size(c, "100%", 0)
  ui.set_grow(c, 1)
  local tabs = ui.row(c)
  ui.set_gap(tabs, 6)
  w.tab_today = pill_button(tabs, "  Today  ", function() set_mode("today") end)
  w.tab_week = pill_button(tabs, "  Week  ", function() set_mode("week") end)
  local r = ui.row(c)
  ui.set_size(r, "100%", 0)
  ui.set_grow(r, 1)
  local slots = grow(ui.row(r), 1, "100%")
  ui.set_gap(slots, 6)
  w.slots = {}
  for i = 1, SLOTS do
    local p = grow(ui.column(slots), 1, "100%")
    ui.set_bg(p, C.tile)
    ui.set_radius(p, 14)
    ui.set_pad(p, 6)
    ui.set_gap(p, 8)
    local dot = ui.box(p)
    ui.set_size(dot, 18, 18)
    ui.set_radius(dot, "round")
    local l = label(p, "")
    ui.set_align(l, "center")
    w.slots[i] = { dot = dot, text = l }
  end
  local sun = ui.column(r)
  ui.set_size(sun, "content", "100%")
  ui.set_gap(sun, 4)
  label(sun, "Sunrise", C.dim)
  w.sunrise = label(sun, "--:--", C.text, "medium")
  label(sun, "Sunset", C.dim)
  w.sunset = label(sun, "--:--", C.text, "medium")
  label(sun, "Daylight", C.dim)
  w.daylight = label(sun, "--", C.text)
end

local function tile(parent, title)
  local t = grow(card(parent, C.tile), 1, "100%")
  ui.set_pad(t, 10)
  ui.set_gap(t, 6)
  label(t, title, C.dim)
  return t
end

local function build_highlights(parent)
  local c = card(parent)
  ui.set_size(c, "100%", 0)
  ui.set_grow(c, 1)
  label(c, "Today's highlights", C.text, "medium")
  local r1, r2 = ui.row(c), ui.row(c)
  for _, r in ipairs({ r1, r2 }) do
    ui.set_size(r, "100%", 0)
    ui.set_grow(r, 1)
    ui.set_gap(r, GAP)
  end

  local rain = tile(r1, "Chance of rain")
  w.rain_chart = ui.chart(rain, 24, 0, 100)
  ui.set_size(w.rain_chart, "100%", 0)
  ui.set_grow(w.rain_chart, 1)
  w.rain_series = ui.chart_series(w.rain_chart, C.rain)
  w.rain_value = label(rain, "--", C.text, "medium")

  local uv = tile(r1, "UV index")
  w.uv_arc = ui.arc(uv, 0, 11, 8)
  ui.set_size(w.uv_arc, 100, 100)
  w.uv_value = label(w.uv_arc, "--", C.text, "medium")
  ui.center(w.uv_value)
  w.uv_level = label(uv, "", C.dim)

  local wind = tile(r2, "Wind")
  w.wind = label(wind, "--", C.text, "medium")
  w.wind_dir = label(wind, "", C.dim)

  local hum = tile(r2, "Humidity")
  w.hum = label(hum, "--", C.text, "medium")
  w.hum_bar = ui.bar(hum, 0, 100)
  ui.set_accent(w.hum_bar, C.teal)
  w.hum_text = label(hum, "", C.dim)
end

local function build_dashboard()
  ui.clear(w.body)
  w.picker = nil
  local left = grow(ui.column(w.body), 3, "100%")
  ui.set_gap(left, GAP)
  build_main_card(left)
  build_forecast_card(left)
  local right = grow(ui.column(w.body), 2, "100%")
  ui.set_gap(right, GAP)
  build_highlights(right)
  set_mode(state.mode)
end

-- ===================== Hiển thị dữ liệu =====================
local function render_main(m)
  local cur, daily = m.current, m.daily
  local _, mo, d, wd = parse_date(cur.time)
  local y = parse_date(cur.time)
  ui.set_text(w.day, WEEKDAY_LONG[wd])
  ui.set_text(w.date, string.format("%d %s %d  -  %s", d, MONTH[mo], y, hhmm(cur.time)))
  ui.set_text(w.temp, temp(cur.temperature_2m) .. state.unit)
  ui.set_text(w.hilo, string.format("High: %s   Low: %s", temp(daily.temperature_2m_max[1]),
                                    temp(daily.temperature_2m_min[1])))
  local kind, text = condition(cur.weather_code)
  draw_icon(w.icon, kind, cur.is_day == 1)
  ui.set_text(w.cond, text)
  ui.set_text(w.feels, "Feels like " .. temp(cur.apparent_temperature))

  -- Hàng giờ / tuần.
  local hourly = m.hourly
  for i = 1, SLOTS do
    local s = w.slots[i]
    local k, txt, is_day = nil, "", true
    if state.mode == "today" then
      local h = 1 + (i - 1) * HOURLY_STEP
      if hourly.time[h] then
        k = condition(hourly.weather_code[h])
        is_day = hourly.is_day[h] == 1
        local name = (i == 1) and "Now" or (hourly.time[h]:sub(12, 13) .. "h")
        txt = name .. "\n" .. temp(hourly.temperature_2m[h])
      end
    elseif daily.time[i] then
      k = condition(daily.weather_code[i])
      local _, _, _, dwd = parse_date(daily.time[i])
      txt = ((i == 1) and "Today" or WEEKDAY[dwd]) .. "\n" .. temp(daily.temperature_2m_max[i]) ..
            "\n" .. temp(daily.temperature_2m_min[i])
    end
    ui.set_text(s.text, txt)
    ui.set_bg(s.dot, k and dot_color(k, is_day) or C.tile)
  end
  ui.set_text(w.sunrise, hhmm(daily.sunrise[1]))
  ui.set_text(w.sunset, hhmm(daily.sunset[1]))
  local secs = daily.daylight_duration[1] or 0
  ui.set_text(w.daylight, string.format("%dh %02dm", math.floor(secs / 3600), math.floor(secs / 60) % 60))

  -- Highlights.
  for h = 1, 24 do
    local p = hourly.precipitation_probability[h]
    if p then ui.chart_push(w.rain_chart, w.rain_series, p) end
  end
  ui.set_text(w.rain_value, string.format("%d%% now", hourly.precipitation_probability[1] or 0))
  local uv = daily.uv_index_max[1] or 0
  local level, color = uv_level(uv)
  ui.set_value(w.uv_arc, clamp_int(math.min(uv, 11)))
  ui.set_accent(w.uv_arc, color)
  ui.set_text(w.uv_value, string.format("%.1f", uv))
  ui.set_text(w.uv_level, level)
  ui.set_text(w.wind, string.format("%.1f km/h", cur.wind_speed_10m))
  ui.set_text(w.wind_dir, string.format("From %s (%d°)", compass(cur.wind_direction_10m), cur.wind_direction_10m))
  local hum = cur.relative_humidity_2m
  ui.set_text(w.hum, hum .. "%")
  ui.set_value(w.hum_bar, hum)
  ui.set_text(w.hum_text, (hum < 30 and "Dry") or (hum <= 60 and "Comfortable") or "Humid")
end

local function render_status()
  ui.set_text(w.city, CITIES[state.city][1] .. "  " .. gach.ui.SYMBOL.DOWN)
  ui.set_text(w.unit, (state.unit == "C") and "°C" or "°F")
  if state.error then
    ui.set_text(w.status, state.error)
    ui.set_color(w.status, C.warn)
  elseif state.updated then
    ui.set_text(w.status, "Updated " .. state.updated)
    ui.set_color(w.status, C.dim)
  else
    ui.set_text(w.status, "Loading...")
    ui.set_color(w.status, C.dim)
  end
end

function app.render()
  if not w.status then return end
  render_status()
  if w.picker then return end
  if state.main then render_main(state.main) end
end

-- ===================== Mạng =====================
local fetch_main

local function fail(reason)
  print("weather: " .. reason)
  state.error = reason
  app.render()
end

fetch_main = function()
  local city = CITIES[state.city]
  local url = string.format("%s&latitude=%.2f&longitude=%.2f%s", API, city[2], city[3], MAIN_FIELDS)
  local ok, why = net.get(url, function(code, body)
    if code ~= 200 then
      fail((code == 0) and ("Network: " .. body) or ("Server error " .. code))
      return
    end
    local data, err = json.decode(body)
    if type(data) ~= "table" or not data.current then
      fail("Bad data: " .. tostring(err))
      return
    end
    state.main, state.error = data, nil
    state.updated = hhmm(data.current.time)
    print(string.format("weather: %s %.1fC code=%d (%d bytes)", CITIES[state.city][1], data.current.temperature_2m,
                        data.current.weather_code, #body))
    app.render()
  end)
  if why == "busy" then
    return true -- Yêu cầu trước còn chạy và sẽ tự cập nhật màn hình: không báo lỗi.
  end
  if not ok then
    fail((why == "offline") and "Offline - connect Wi-Fi in Settings" or ("Network " .. tostring(why)))
  end
  return ok
end

local function schedule(ms)
  if tick then timer.cancel(tick) end
  tick = timer.every(ms, function()
    if fetch_main() then schedule(REFRESH_MS) else schedule(RETRY_MS) end
  end)
end

local function refresh_now()
  schedule(fetch_main() and REFRESH_MS or RETRY_MS)
end

-- ===================== Chọn thành phố / đơn vị =====================
function app.select_city(idx)
  state.city = idx
  state.main, state.updated, state.error = nil, nil, nil
  storage.set("city", tostring(idx))
  build_dashboard()
  app.render()
  refresh_now()
end

local function open_picker()
  if w.picker then build_dashboard(); app.render(); return end
  ui.clear(w.body)
  w.picker = ui.list(w.body)
  for i, city in ipairs(CITIES) do
    ui.list_add(w.picker, ((i == state.city) and "> " or "   ") .. city[1], function() app.select_city(i) end)
  end
end

local function toggle_unit()
  state.unit = (state.unit == "C") and "F" or "C"
  storage.set("unit", state.unit)
  app.render()
end

-- ===================== Vòng đời =====================
function app.on_create(root)
  state.city = tonumber(storage.get("city") or "") or 1
  if not CITIES[state.city] then state.city = 1 end
  state.unit = (storage.get("unit") == "F") and "F" or "C"

  ui.set_bg(root, C.bg)
  ui.set_pad(root, PAD)
  ui.set_gap(root, GAP)

  local header = ui.row(root)
  ui.set_gap(header, GAP)
  w.city = pill_button(header, "", open_picker)
  ui.set_bg(w.city, C.accent)
  w.status = grow(label(header, "", C.dim), 1)
  ui.button(header, gach.ui.SYMBOL.REFRESH, refresh_now)
  w.unit = pill_button(header, "", toggle_unit)

  w.body = ui.row(root)
  ui.set_size(w.body, "100%", 0)
  ui.set_grow(w.body, 1)
  ui.set_gap(w.body, GAP)
  build_dashboard()
  app.render()
end

function app.on_resume()
  -- Quay lại sau lâu (hoặc lần đầu) -> tải ngay; còn mới thì chỉ hẹn giờ.
  refresh_now()
end

function app.on_pause()
  if tick then timer.cancel(tick); tick = nil end
end

function app.on_back()
  if w.picker then
    build_dashboard()
    app.render()
    return true
  end
  return false
end

return app
