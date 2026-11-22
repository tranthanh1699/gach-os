/**
 * @file    launcher_app.cpp
 * @brief   Hiện thực home screen dạng lưới.
 */
#include "launcher_app.hpp"

#include "framework/app_manager.hpp"
#include "framework/theme.hpp"

namespace apps {

namespace {

constexpr fw::AppInfo INFO{"launcher", "Home", LV_SYMBOL_HOME, 0x2F81F7U, false};
constexpr int32_t TILE_WIDTH = 72;
constexpr int32_t ICON_SIZE = 52;
constexpr int32_t ICON_RADIUS = 14;
constexpr int32_t GRID_GAP = 6;
constexpr int32_t TILE_ROW_GAP = 4;

} // namespace

LauncherApp::LauncherApp() : fw::App(INFO) {}

void LauncherApp::on_create(lv_obj_t* p_root)
{
    lv_obj_set_style_pad_all(p_root, GRID_GAP, 0);
    lv_obj_set_style_pad_row(p_root, GRID_GAP, 0);
    lv_obj_set_style_pad_column(p_root, GRID_GAP, 0);
    lv_obj_set_flex_flow(p_root, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(p_root, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(p_root, LV_DIR_VER);

    fw::AppManager& apps = context().apps;
    for (size_t index = 0U; index < apps.count(); ++index) {
        fw::App* const p_app = apps.at(index);
        if ((p_app != nullptr) && p_app->info().show_in_launcher) {
            create_tile(p_root, *p_app);
        }
    }
}

void LauncherApp::create_tile(lv_obj_t* p_parent, fw::App& target)
{
    // Tile = container clickable; con của nó không clickable để click luôn rơi vào tile.
    lv_obj_t* const p_tile = lv_obj_create(p_parent);
    lv_obj_remove_style_all(p_tile);
    lv_obj_set_size(p_tile, TILE_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(p_tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p_tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(p_tile, TILE_ROW_GAP, 0);
    lv_obj_set_style_opa(p_tile, LV_OPA_60, LV_STATE_PRESSED);
    lv_obj_add_flag(p_tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(p_tile, &target);
    lv_obj_add_event_cb(p_tile, &LauncherApp::on_tile_clicked, LV_EVENT_CLICKED, this);

    lv_obj_t* const p_icon = lv_obj_create(p_tile);
    lv_obj_remove_style_all(p_icon);
    lv_obj_set_size(p_icon, ICON_SIZE, ICON_SIZE);
    lv_obj_set_style_radius(p_icon, ICON_RADIUS, 0);
    lv_obj_set_style_bg_opa(p_icon, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(p_icon, lv_color_hex(target.info().color_hex), 0);
    lv_obj_remove_flag(p_icon, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* const p_symbol = lv_label_create(p_icon);
    lv_label_set_text(p_symbol, target.info().p_icon);
    lv_obj_set_style_text_font(p_symbol, fw::theme::font_medium(), 0);
    lv_obj_set_style_text_color(p_symbol, lv_color_white(), 0);
    lv_obj_center(p_symbol);

    lv_obj_t* const p_name = lv_label_create(p_tile);
    lv_label_set_text(p_name, target.info().p_name);
    lv_label_set_long_mode(p_name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(p_name, TILE_WIDTH);
    lv_obj_set_style_text_align(p_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(p_name, lv_color_hex(fw::theme::COLOR_TEXT), 0);
}

void LauncherApp::on_tile_clicked(lv_event_t* p_event)
{
    auto* const p_self = static_cast<LauncherApp*>(lv_event_get_user_data(p_event));
    auto* const p_target = static_cast<fw::App*>(lv_obj_get_user_data(lv_event_get_current_target_obj(p_event)));
    if (p_target != nullptr) {
        (void)p_self->context().apps.launch(p_target->info().p_id);
    }
}

} // namespace apps
