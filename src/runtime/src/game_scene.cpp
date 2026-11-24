/**
 * @file    game_scene.cpp
 * @brief   Vẽ và chạy scene gach.game trên LVGL.
 *
 * Vẽ: trong LV_EVENT_DRAW_MAIN, sprite cắt theo vùng đang vẽ, sắp theo (lớp, y), mỗi sprite một lv_draw_image
 * của cả sprite sheet với vùng clip thu về đúng khung (không cần chép khung ra bộ đệm riêng).
 */
#include "game_scene.hpp"

#include <algorithm>

#include "common/trace.hpp"
#include "osal/osal.hpp"

namespace runtime {

namespace {

constexpr uint32_t TICK_MS = 16U;
constexpr uint32_t US_PER_MS = 1000U;
constexpr uint32_t US_PER_S = 1000000U;
constexpr uint32_t MAX_DT_US = 100U * US_PER_MS; /**< Bị treo lâu (VD app vào nền): không nhảy cóc quá 100 ms. */
constexpr uint8_t OPA_FULL = 255U;
constexpr int32_t SORT_LAYER_SHIFT = 20;

struct DrawItem {
    int32_t key; /**< (lớp << 20) + y: sắp tăng dần = thứ tự vẽ. */
    int32_t id;
};

/** @brief Thứ tự vẽ, dựng một lần mỗi tick (LVGL vẽ một khung thành nhiều dải: không sắp lại mỗi dải). */
DrawItem s_items[GameWorld::MAX_SPRITES];
size_t s_item_count = 0U;

/** @brief Giao hai vùng (gồm mép). @return false nếu không giao. */
bool intersect(lv_area_t& out, const lv_area_t& a, const lv_area_t& b)
{
    out.x1 = (a.x1 > b.x1) ? a.x1 : b.x1;
    out.y1 = (a.y1 > b.y1) ? a.y1 : b.y1;
    out.x2 = (a.x2 < b.x2) ? a.x2 : b.x2;
    out.y2 = (a.y2 < b.y2) ? a.y2 : b.y2;
    return (out.x1 <= out.x2) && (out.y1 <= out.y2);
}

} // namespace

common::Status GameScene::create(lv_obj_t* p_parent, int32_t w, int32_t h, uint32_t logic_hz)
{
    if (m_p_obj != nullptr) {
        return common::Status::ERR_STATE;
    }
    m_world.clear();
    m_sheet_count = 0U;
    m_p_background = nullptr;
    m_bg_color = 0U;
    m_logic = nullptr;
    m_touch = nullptr;
    m_p_ctx = nullptr;
    m_paused = false;
    m_suspended = false;
    m_stats = Stats{};
    m_frames = 0U;
    const uint32_t hz = (logic_hz < LOGIC_HZ_MIN) ? LOGIC_HZ_MIN : ((logic_hz > LOGIC_HZ_MAX) ? LOGIC_HZ_MAX : logic_hz);
    m_logic_period_us = US_PER_S / hz;
    m_logic_acc_us = 0U;

    m_p_obj = lv_obj_create(p_parent);
    lv_obj_remove_style_all(m_p_obj);
    lv_obj_set_size(m_p_obj, w, h);
    lv_obj_set_scrollable(m_p_obj, false);
    lv_obj_set_clickable(m_p_obj, true);
    lv_obj_add_event_cb(m_p_obj, &GameScene::on_event, LV_EVENT_ALL, this);
    m_p_timer = lv_timer_create(&GameScene::on_timer, TICK_MS, this);
    std::fill(m_dirty_rows, m_dirty_rows + TILE_ROWS, 0ULL);
    m_has_dirty = false;
    m_p_display = lv_obj_get_display(m_p_obj);
    lv_display_add_event_cb(m_p_display, &GameScene::on_display_event, LV_EVENT_RENDER_READY, this);
    m_last_us = osal::time_us();
    m_fps_since_us = m_last_us;
    return common::Status::OK;
}

void GameScene::destroy()
{
    detach_display();
    if (m_p_timer != nullptr) {
        lv_timer_delete(m_p_timer);
        m_p_timer = nullptr;
    }
    if (m_p_obj != nullptr) {
        lv_obj_t* const p_obj = m_p_obj;
        m_p_obj = nullptr; // Trước khi xóa: sự kiện DELETE không gọi lại vào scene.
        lv_obj_remove_event_cb_with_user_data(p_obj, &GameScene::on_event, this);
        lv_obj_delete(p_obj);
    }
    m_world.clear();
    m_logic = nullptr;
    m_touch = nullptr;
}

int32_t GameScene::add_sheet(const lv_image_dsc_t* p_image, uint16_t frame_w, uint16_t frame_h)
{
    if ((p_image == nullptr) || (m_sheet_count == MAX_SHEETS) || (frame_w == 0U) || (frame_h == 0U) ||
        (frame_w > p_image->header.w) || (frame_h > p_image->header.h)) {
        return -1;
    }
    m_sheets[m_sheet_count] =
        Sheet{p_image, frame_w, frame_h, static_cast<uint16_t>(p_image->header.w / frame_w)};
    return static_cast<int32_t>(m_sheet_count++);
}

bool GameScene::sheet_frame(int32_t sheet, uint16_t& w, uint16_t& h) const
{
    if ((sheet < 0) || (static_cast<size_t>(sheet) >= m_sheet_count)) {
        return false;
    }
    w = m_sheets[sheet].frame_w;
    h = m_sheets[sheet].frame_h;
    return true;
}

void GameScene::set_background(uint32_t color, const lv_image_dsc_t* p_image)
{
    m_bg_color = color;
    m_p_background = p_image;
    if (m_p_obj != nullptr) {
        lv_obj_invalidate(m_p_obj);
    }
}

void GameScene::set_paused(bool paused)
{
    if (m_paused && !paused) {
        m_last_us = osal::time_us(); // Không tính thời gian đã dừng.
    }
    m_paused = paused;
}

void GameScene::set_suspended(bool suspended)
{
    if (m_suspended && !suspended) {
        m_last_us = osal::time_us();
    }
    m_suspended = suspended;
}

void GameScene::set_callbacks(LogicFn logic, TouchFn touch_fn, void* p_ctx)
{
    m_logic = logic;
    m_touch = touch_fn;
    m_p_ctx = p_ctx;
}

void GameScene::stats(Stats& out) const
{
    out = m_stats;
    out.sprites = static_cast<uint32_t>(m_world.count());
}

void GameScene::on_timer(lv_timer_t* p_timer)
{
    static_cast<GameScene*>(lv_timer_get_user_data(p_timer))->tick();
}

void GameScene::on_dirty(const GameRect& rect, void* p_ctx)
{
    // Chỉ đánh dấu ô; flush_dirty() gộp thành ít vùng (gọi lv_obj_invalidate_area cho từng sprite thì LVGL tràn
    // bộ đệm 32 vùng và phải quét danh sách mỗi lần).
    auto* const p_self = static_cast<GameScene*>(p_ctx);
    const int32_t w = lv_obj_get_width(p_self->m_p_obj);
    const int32_t h = lv_obj_get_height(p_self->m_p_obj);
    const int32_t x1 = (rect.x1 < 0) ? 0 : rect.x1;
    const int32_t y1 = (rect.y1 < 0) ? 0 : rect.y1;
    const int32_t x2 = (rect.x2 >= w) ? (w - 1) : rect.x2;
    const int32_t y2 = (rect.y2 >= h) ? (h - 1) : rect.y2;
    if ((x1 > x2) || (y1 > y2)) {
        return;
    }
    const int32_t c1 = x1 / TILE;
    const int32_t c2 = x2 / TILE;
    const uint64_t bits = ((c2 - c1 + 1) >= 64) ? ~0ULL : (((1ULL << (c2 - c1 + 1)) - 1ULL) << c1);
    for (int32_t row = y1 / TILE; (row <= (y2 / TILE)) && (static_cast<size_t>(row) < TILE_ROWS); ++row) {
        p_self->m_dirty_rows[row] |= bits;
    }
    p_self->m_has_dirty = true;
}

void GameScene::flush_dirty()
{
    if (!m_has_dirty) {
        return;
    }
    m_has_dirty = false;
    lv_area_t coords;
    lv_obj_get_coords(m_p_obj, &coords);
    lv_area_t rects[MAX_INVALIDATIONS];
    size_t count = 0U;
    bool overflow = false;
    for (size_t row = 0U; (row < TILE_ROWS) && !overflow; ++row) {
        while (m_dirty_rows[row] != 0ULL) {
            // Dải ô liên tiếp trong hàng, kéo xuống các hàng có cùng dải.
            const uint64_t mask = m_dirty_rows[row];
            const int first = __builtin_ctzll(mask);
            int last = first;
            while ((last < 63) && ((mask >> (last + 1)) & 1ULL)) {
                ++last;
            }
            const uint64_t run = ((last - first + 1) >= 64) ? ~0ULL : (((1ULL << (last - first + 1)) - 1ULL) << first);
            size_t end = row;
            while (((end + 1U) < TILE_ROWS) && ((m_dirty_rows[end + 1U] & run) == run)) {
                ++end;
            }
            for (size_t r = row; r <= end; ++r) {
                m_dirty_rows[r] &= ~run;
            }
            if (count == MAX_INVALIDATIONS) {
                overflow = true;
                break;
            }
            rects[count++] = lv_area_t{coords.x1 + (first * TILE), coords.y1 + static_cast<int32_t>(row) * TILE,
                                       coords.x1 + ((last + 1) * TILE) - 1,
                                       coords.y1 + static_cast<int32_t>(end + 1U) * TILE - 1};
        }
    }
    if (overflow) {
        std::fill(m_dirty_rows, m_dirty_rows + TILE_ROWS, 0ULL);
        lv_obj_invalidate(m_p_obj); // Quá nhiều mảnh: một vùng cả scene rẻ hơn.
        return;
    }
    for (size_t index = 0U; index < count; ++index) {
        lv_area_t clipped;
        if (intersect(clipped, rects[index], coords)) {
            lv_obj_invalidate_area(m_p_obj, &clipped);
        }
    }
}

void GameScene::on_display_event(lv_event_t* p_event)
{
    ++static_cast<GameScene*>(lv_event_get_user_data(p_event))->m_frames; // Một khung đã vẽ xong.
}

void GameScene::detach_display()
{
    if (m_p_display != nullptr) {
        (void)lv_display_remove_event_cb_with_user_data(m_p_display, &GameScene::on_display_event, this);
        m_p_display = nullptr;
    }
}

void GameScene::build_order()
{
    s_item_count = 0U;
    for (size_t slot = 0U; slot < GameWorld::MAX_SPRITES; ++slot) {
        const int32_t id = m_world.id_at(slot);
        const Sprite* const p_s = m_world.get(id);
        if ((p_s == nullptr) || !p_s->visible || (p_s->opa == 0U)) {
            continue;
        }
        s_items[s_item_count++] =
            DrawItem{(static_cast<int32_t>(p_s->layer) << SORT_LAYER_SHIFT) + GameWorld::rect_of(*p_s).y1, id};
    }
    std::stable_sort(s_items, s_items + s_item_count,
                     [](const DrawItem& a, const DrawItem& b) { return a.key < b.key; });
}

void GameScene::tick()
{
    if ((m_p_obj == nullptr) || m_paused || m_suspended) {
        return;
    }
    const uint32_t now = osal::time_us();
    uint32_t dt_us = now - m_last_us;
    m_last_us = now;
    dt_us = (dt_us > MAX_DT_US) ? MAX_DT_US : dt_us;
    {
        TRACE_SCOPE("game", "tick");
        const uint32_t start = osal::time_us();
        m_world.step(dt_us / US_PER_MS, &GameScene::on_dirty, this);
        flush_dirty();
        m_stats.tick_us = osal::time_us() - start;
    }
    // Phần lẻ ms giữ lại cho tick sau qua m_last_us: dt thực tế không trôi.
    m_last_us -= dt_us % US_PER_MS;
    m_logic_acc_us += dt_us - (dt_us % US_PER_MS);
    if ((m_logic != nullptr) && (m_logic_acc_us >= m_logic_period_us)) {
        const uint32_t logic_dt_ms = m_logic_acc_us / US_PER_MS;
        m_logic_acc_us = 0U;
        TRACE_SCOPE("game", "lua_update");
        const uint32_t start = osal::time_us();
        m_logic(m_p_ctx, logic_dt_ms); // Có thể hủy scene (app thoát / lỗi).
        m_stats.logic_us = osal::time_us() - start;
    }
    if (m_p_obj != nullptr) {
        build_order(); // Sau logic: gồm cả sprite Lua vừa thêm / đổi.
    }
    if ((now - m_fps_since_us) >= US_PER_S) {
        m_stats.fps = m_frames;
        m_frames = 0U;
        m_fps_since_us = now;
    }
}

void GameScene::touch(lv_event_code_t code)
{
    if ((m_touch == nullptr) || m_paused || m_suspended) {
        return;
    }
    lv_indev_t* const p_indev = lv_indev_active();
    if (p_indev == nullptr) {
        return;
    }
    lv_point_t point;
    lv_indev_get_point(p_indev, &point);
    lv_area_t coords;
    lv_obj_get_coords(m_p_obj, &coords);
    point.x -= coords.x1;
    point.y -= coords.y1;
    TouchPhase phase = TOUCH_MOVE;
    if (code == LV_EVENT_PRESSED) {
        phase = TOUCH_DOWN;
    } else if (code == LV_EVENT_RELEASED) {
        phase = TOUCH_UP;
    } else if ((point.x == m_last_touch.x) && (point.y == m_last_touch.y)) {
        return; // Đè yên: không báo move.
    } else {
        // Di chuyển.
    }
    m_last_touch = point;
    m_touch(m_p_ctx, phase, point.x, point.y);
}

void GameScene::on_event(lv_event_t* p_event)
{
    auto* const p_self = static_cast<GameScene*>(lv_event_get_user_data(p_event));
    if (p_self->m_p_obj == nullptr) {
        return;
    }
    const lv_event_code_t code = lv_event_get_code(p_event);
    if (code == LV_EVENT_DELETE) { // Bị xóa từ ngoài (ui.clear / màn hình lỗi / đóng app).
        if (p_self->m_p_timer != nullptr) {
            lv_timer_delete(p_self->m_p_timer);
            p_self->m_p_timer = nullptr;
        }
        p_self->m_p_obj = nullptr;
        p_self->detach_display();
        p_self->m_world.clear();
    } else if (code == LV_EVENT_DRAW_MAIN) {
        p_self->draw(lv_event_get_layer(p_event));
    } else if ((code == LV_EVENT_PRESSED) || (code == LV_EVENT_PRESSING) || (code == LV_EVENT_RELEASED)) {
        p_self->touch(code);
    } else {
        // Sự kiện khác: bỏ qua.
    }
}

void GameScene::draw(lv_layer_t* p_layer)
{
    TRACE_SCOPE("game", "draw");
    const uint32_t start = osal::time_us();
    lv_area_t coords;
    lv_obj_get_coords(m_p_obj, &coords);
    const lv_area_t clip = p_layer->_clip_area;

    if (m_p_background != nullptr) {
        lv_draw_image_dsc_t dsc;
        lv_draw_image_dsc_init(&dsc);
        dsc.src = m_p_background;
        dsc.tile = 1;
        lv_draw_image(p_layer, &dsc, &coords);
    } else {
        lv_draw_rect_dsc_t dsc;
        lv_draw_rect_dsc_init(&dsc);
        dsc.bg_color = lv_color_hex(m_bg_color);
        lv_draw_rect(p_layer, &dsc, &coords);
    }

    for (size_t index = 0U; index < s_item_count; ++index) {
        const Sprite* const p_s = m_world.get(s_items[index].id);
        if ((p_s == nullptr) || !p_s->visible || (p_s->opa == 0U)) {
            continue; // Bị xóa / ẩn sau khi dựng thứ tự.
        }
        const Sprite& s = *p_s;
        const GameRect r = GameWorld::rect_of(s);
        const lv_area_t area{coords.x1 + r.x1, coords.y1 + r.y1, coords.x1 + r.x2, coords.y1 + r.y2};
        lv_area_t overlap;
        if (!intersect(overlap, area, clip)) {
            continue;
        }
        const bool has_sheet = (s.sheet >= 0) && (static_cast<size_t>(s.sheet) < m_sheet_count);
        if (!has_sheet) {
            lv_draw_rect_dsc_t dsc;
            lv_draw_rect_dsc_init(&dsc);
            dsc.bg_color = lv_color_hex(s.color);
            dsc.bg_opa = s.opa;
            lv_draw_rect(p_layer, &dsc, &area);
            continue;
        }
        const Sheet& sheet = m_sheets[s.sheet];
        const uint32_t col = s.frame % sheet.columns;
        const uint32_t row = s.frame / sheet.columns;
        // Cả sheet đặt lệch sao cho khung cần vẽ trùng vị trí sprite; clip thu về khung đó.
        const int32_t sheet_x = area.x1 - static_cast<int32_t>(col * sheet.frame_w);
        const int32_t sheet_y = area.y1 - static_cast<int32_t>(row * sheet.frame_h);
        const lv_area_t sheet_area{sheet_x, sheet_y, sheet_x + static_cast<int32_t>(sheet.p_image->header.w) - 1,
                                   sheet_y + static_cast<int32_t>(sheet.p_image->header.h) - 1};
        const lv_area_t frame{area.x1, area.y1, area.x1 + sheet.frame_w - 1, area.y1 + sheet.frame_h - 1};
        lv_area_t sprite_clip;
        if (!intersect(sprite_clip, frame, clip)) {
            continue;
        }
        lv_draw_image_dsc_t dsc;
        lv_draw_image_dsc_init(&dsc);
        dsc.src = sheet.p_image;
        dsc.opa = (s.opa == 0U) ? OPA_FULL : s.opa;
        p_layer->_clip_area = sprite_clip;
        lv_draw_image(p_layer, &dsc, &sheet_area);
        p_layer->_clip_area = clip;
    }
    m_stats.draw_us = osal::time_us() - start;
}

} // namespace runtime
