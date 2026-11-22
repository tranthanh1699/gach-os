-- __NAME__: app gach-os (gach SDK v2). Tài liệu API: docs/sdk.md
local ui = gach.ui

local app = {}
local count = 0
local label

function app.on_create(root)
  label = ui.label(root, "Hello from __NAME__!")
  ui.set_font(label, "medium")
  ui.button(root, "Tap me", function()
    count = count + 1
    ui.set_text(label, "Tapped " .. count .. " time(s)")
  end)
end

-- Tùy chọn: app.on_resume, app.on_pause, app.on_destroy, app.on_back

return app
