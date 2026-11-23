-- UI Kit (gach SDK v6): mẫu sống cho người viết app. Mỗi trang minh họa một nhóm API; dòng cuối ghi sự kiện.
local ui, timer, S = gach.ui, gach.timer, gach.ui.SYMBOL

local app = {}

local C = { bg = 0x000000, card = 0x1C1C1E, text = 0xFFFFFF, dim = 0x8E8E93, accent = 0x0A84FF,
            green = 0x30D158, orange = 0xFF9F0A, purple = 0xBF5AF2, pink = 0xFF375F }
local PAGES = { "Buttons", "Inputs", "Feedback", "Motion" }

local w = {}
local timers = {}

-- ===================== Tiện ích =====================
local function log(text)
  ui.set_text(w.log, "Event:  " .. text)
end

local function stop_timers()
  for _, id in ipairs(timers) do timer.cancel(id) end
  timers = {}
end

local function label(parent, text, color, font)
  local l = ui.label(parent, text)
  ui.style(l, { color = color or C.text, font = font or "small" })
  return l
end

local function card(parent, title)
  local c = ui.column(parent)
  ui.style(c, { bg = C.card, radius = 14, pad = 14, gap = 10, width = "100%" })
  if title then label(c, title, C.dim) end
  return c
end

local function row(parent, gap)
  local r = ui.row(parent)
  ui.style(r, { gap = gap or 10, align = "center" })
  return r
end

-- ===================== Trang: Buttons =====================
local function page_buttons(body)
  local variants = card(body, "VARIANTS  (variant=...)")
  local r = row(variants)
  for _, v in ipairs({ "filled", "tinted", "gray", "outline", "plain", "destructive" }) do
    ui.button(r, { text = v, variant = v }, function() log("click " .. v) end)
  end

  local sizes = card(body, "SIZES, ICONS, SHAPES")
  local r2 = row(sizes)
  ui.button(r2, { text = "Small", size = "sm" }, function() log("click sm") end)
  ui.button(r2, { text = "Medium", size = "md" }, function() log("click md") end)
  ui.button(r2, { text = "Large", size = "lg" }, function() log("click lg") end)
  ui.button(r2, { icon = S.PLUS, text = "Add item", variant = "tinted" }, function() log("click add") end)
  ui.button(r2, { icon = S.SETTINGS, variant = "gray", shape = "pill" }, function() log("click settings") end)
  ui.button(r2, { text = "Custom color", color = C.green, shape = "pill" }, function() log("click custom") end)

  local states = card(body, "ENABLED / DISABLED  (set_enabled)")
  local r3 = row(states)
  local target = ui.button(r3, { text = "Send", icon = S.OK }, function() log("click send") end)
  local sw = ui.switch(r3, true)
  label(r3, "Button enabled", C.dim)
  ui.on(sw, "change", function(on)
    ui.set_enabled(target, on)
    log("switch -> " .. tostring(on))
  end)

  local ev = card(body, "EVENTS  (ui.on: press, release, click, long_press)")
  local pad = ui.box(ev)
  ui.style(pad, { bg = 0x2C2C2E, radius = 12, width = "100%", height = 70, pressed_bg = 0x3A3A3C })
  local hint = label(pad, "Tap, hold or press here", C.dim)
  ui.center(hint)
  for _, name in ipairs({ "press", "release", "click", "long_press" }) do
    ui.on(pad, name, function() log(name) end)
  end
end

-- ===================== Trang: Inputs =====================
local function page_inputs(body)
  local text = card(body, "TEXT FIELDS  (textfield, submit, change)")
  local name = ui.textfield(text, { placeholder = "Your name", max = 32 })
  ui.on(name, "submit", function(value) ui.toast("Hello, " .. (value ~= "" and value or "stranger") .. "!") end)
  ui.on(name, "focus", function() log("focus name") end)
  local pass = ui.textfield(text, { placeholder = "Password", password = true, max = 32 })
  ui.on(pass, "change", function(value) log("password length " .. #value) end)

  local choice = card(body, "CHOICES")
  local r = row(choice, 16)
  local sw = ui.switch(r, false)
  label(r, "Wi-Fi", C.text)
  local cb = ui.checkbox(r, "Remember me", true)
  ui.on(sw, "change", function(v) log("switch " .. tostring(v)) end)
  ui.on(cb, "change", function(v) log("checkbox " .. tostring(v)) end)

  local r2 = row(choice, 16)
  local seg = ui.segmented(r2, { "Day", "Week", "Month" }, 2)
  ui.on(seg, "change", function(i) log("segmented " .. i) end)
  local dd = ui.dropdown(r2, { "Ha Noi", "Da Nang", "Ho Chi Minh" }, 1)
  ui.on(dd, "change", function(i) log("dropdown " .. i) end)

  local sl = ui.slider(choice, 0, 100, 40)
  ui.on(sl, "change", function(v) log("slider " .. v) end)

  ui.button(body, { text = "Read all values", icon = S.EYE_OPEN, variant = "tinted" }, function()
    ui.alert {
      title = "Current values",
      message = string.format("name=%q\nwifi=%s remember=%s\nrange=%d city=%d slider=%d",
        ui.get_text(name), tostring(ui.get_value(sw)), tostring(ui.get_value(cb)),
        ui.get_value(seg), ui.get_value(dd), ui.get_value(sl)),
      buttons = { "OK" },
    }
  end)
end

-- ===================== Trang: Feedback =====================
local function page_feedback(body)
  local alerts = card(body, "ALERTS  (ui.alert)")
  local r = row(alerts)
  ui.button(r, { text = "Confirm" }, function()
    ui.alert { title = "Save changes?", message = "Your edits will be kept on the SD card.",
               buttons = { "Cancel", "Save" }, fn = function(i) log("alert -> button " .. i) end }
  end)
  ui.button(r, { text = "Destructive", variant = "destructive" }, function()
    ui.alert { title = "Delete all notes?", message = "This cannot be undone.",
               buttons = { "Cancel", "Delete" }, destructive = true,
               fn = function(i) ui.toast(i == 2 and "Deleted" or "Kept") end }
  end)
  ui.button(r, { text = "Info", variant = "gray" }, function()
    ui.alert { title = "gach SDK v" .. gach.sdk_version, buttons = { "OK" } }
  end)

  local toasts = card(body, "TOAST  (ui.toast)")
  local r2 = row(toasts)
  ui.button(r2, { text = "Short toast", variant = "tinted" }, function() ui.toast("Saved") end)
  ui.button(r2, { text = "Long toast", variant = "tinted" },
    function() ui.toast("Uploaded 3 files to the cloud", 4000) end)

  local progress = card(body, "LOADING  (spinner + bar)")
  local r3 = row(progress, 14)
  ui.spinner(r3)
  local status = label(r3, "Working...", C.dim)
  local bar = ui.bar(progress, 0, 100, 0)
  local value = 0
  timers[#timers + 1] = timer.every(80, function()
    value = (value + 2) % 102
    ui.set_value(bar, value)
    ui.set_text(status, value >= 100 and "Done" or string.format("Working... %d%%", value))
  end)
end

-- ===================== Trang: Motion =====================
local function page_motion(body)
  local stage_card = card(body, "ANIMATE  (ui.animate: x, y, w, h, opa, ease, done)")
  local stage = ui.box(stage_card)
  ui.style(stage, { bg = 0x2C2C2E, radius = 12, width = "100%", height = 120, clip = true })
  local ball = ui.box(stage)
  ui.style(ball, { bg = C.purple, radius = "round", width = 56, height = 56 })
  ui.set_pos(ball, 12, 32)

  local ease = "out"
  local r = row(stage_card)
  local seg = ui.segmented(r, { "out", "in_out", "overshoot", "bounce", "linear" }, 1)
  ui.on(seg, "change", function(i) ease = ({ "out", "in_out", "overshoot", "bounce", "linear" })[i] end)

  local r2 = row(stage_card)
  local right = false
  ui.button(r2, { text = "Move", icon = S.RIGHT }, function()
    right = not right
    ui.animate(ball, { x = right and 640 or 12, ms = 700, ease = ease, done = function() log("move done") end })
  end)
  ui.button(r2, { text = "Fade", variant = "tinted" }, function()
    ui.animate(ball, { opa = 10, ms = 300, done = function() ui.animate(ball, { opa = 100, ms = 300 }) end })
  end)
  ui.button(r2, { text = "Grow", variant = "tinted" }, function()
    ui.animate(ball, { w = 96, h = 96, y = 12, ms = 250, ease = "overshoot", done = function()
      ui.animate(ball, { w = 56, h = 56, y = 32, ms = 250 })
    end })
  end)

  local swipe = card(body, "SWIPE  (ui.on(h, \"swipe\"))")
  local area = ui.box(swipe)
  ui.style(area, { bg = 0x2C2C2E, radius = 12, width = "100%", height = 70 })
  local hint = label(area, "Swipe in any direction", C.dim)
  ui.center(hint)
  ui.on(area, "swipe", function(dir)
    ui.set_text(hint, "Swiped " .. dir)
    log("swipe " .. dir)
  end)
end

local BUILDERS = { page_buttons, page_inputs, page_feedback, page_motion }

local function show_page(i)
  w.page = i
  stop_timers()
  ui.clear(w.body)
  BUILDERS[i](w.body)
  log("page " .. PAGES[i])
end

-- ===================== Vòng đời =====================
function app.on_create(root)
  ui.style(root, { bg = C.bg, pad = 14, gap = 12 })

  local header = row(root, 14)
  ui.style(header, { width = "100%", justify = "between" })
  label(header, "UI Kit", C.text, "large")
  local tabs = ui.segmented(header, PAGES, 1)
  ui.on(tabs, "change", show_page)

  w.body = ui.column(root)
  ui.style(w.body, { width = "100%", height = 0, grow = 1, gap = 12, scroll = true })

  w.log = label(root, "Event:  -", C.green)
  show_page(1)
end

function app.on_pause()
  stop_timers()
end

function app.on_resume()
  -- Timer bị hủy khi app vào nền: trang Feedback dựng lại để chạy tiếp thanh tiến độ.
  if w.page == 3 then show_page(3) end
end

return app
