-- Counter: minh họa UI, sự kiện, timer và lưu trữ (gach SDK v1).
local ui, storage, timer = gach.ui, gach.storage, gach.timer

local app = {}
local count = tonumber(storage.get("count")) or 0
local value_label, uptime_label, tick

local function refresh()
  ui.set_text(value_label, tostring(count))
end

local function change(delta)
  count = count + delta
  storage.set("count", tostring(count))
  refresh()
end

function app.on_create(root)
  ui.label(root, "Tap to count (saved on SD)")
  value_label = ui.label(root, "")
  ui.set_font(value_label, "large")

  local row = ui.row(root)
  ui.button(row, "-1", function() change(-1) end)
  ui.button(row, "+1", function() change(1) end)
  ui.button(row, "Reset", function() count = 0; change(0) end)

  uptime_label = ui.label(root, "")
  ui.set_color(uptime_label, 0x8B949E)
  refresh()
end

function app.on_resume()
  tick = timer.every(1000, function()
    ui.set_text(uptime_label, string.format("Uptime: %d s", gach.sys.uptime_ms() // 1000))
  end)
end

function app.on_pause()
  if tick then timer.cancel(tick); tick = nil end
end

return app
