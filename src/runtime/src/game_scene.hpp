/**
 * @file    game_scene.hpp
 * @brief   [Private] Scene của gach.game: một đối tượng LVGL tự vẽ mọi sprite của GameWorld, timer 16 ms chạy
 *          thế giới, chỉ vẽ lại vùng thay đổi; gọi logic (Lua) ở nhịp riêng.
 *
 * Một app một scene. Chỉ dùng trên task UI.
 */
#ifndef RUNTIME_GAME_SCENE_HPP
#define RUNTIME_GAME_SCENE_HPP

#include <cstddef>
#include <cstdint>

#include "common/non_copyable.hpp"
#include "common/status.hpp"
#include "game_world.hpp"
#include "lvgl.h"

namespace runtime {

/** @brief Scene. */
class GameScene : private common::NonCopyable {
public:
    static constexpr size_t MAX_SHEETS = 64U;
    static constexpr uint32_t LOGIC_HZ_MIN = 10U;
    static constexpr uint32_t LOGIC_HZ_MAX = 60U;
    static constexpr uint32_t LOGIC_HZ_DEFAULT = 30U;

    enum TouchPhase : uint8_t { TOUCH_DOWN, TOUCH_MOVE, TOUCH_UP };

    using LogicFn = void (*)(void* p_ctx, uint32_t dt_ms);
    using TouchFn = void (*)(void* p_ctx, TouchPhase phase, int32_t x, int32_t y);

    /** @brief Số đo (gach.game.stats). */
    struct Stats {
        uint32_t sprites;
        uint32_t fps;       /**< Khung vẽ lại mỗi giây (chỉ đếm khi có thay đổi). */
        uint32_t tick_us;   /**< Lần chạy thế giới gần nhất. */
        uint32_t logic_us;  /**< Lần gọi logic gần nhất. */
        uint32_t draw_us;   /**< Lần vẽ scene gần nhất. */
    };

    GameScene() = default;

    /** @brief Tạo vùng chơi w x h px trong parent. @return ERR_STATE nếu đã có scene. */
    common::Status create(lv_obj_t* p_parent, int32_t w, int32_t h, uint32_t logic_hz);

    /** @brief Hủy timer + đối tượng (gọi được nhiều lần). */
    void destroy();

    bool is_active() const { return m_p_obj != nullptr; }
    lv_obj_t* object() const { return m_p_obj; }
    GameWorld& world() { return m_world; }

    /** @brief Thêm sprite sheet (ảnh chia lưới khung). @return id >= 0, -1 nếu đầy / tham số sai. */
    int32_t add_sheet(const lv_image_dsc_t* p_image, uint16_t frame_w, uint16_t frame_h);

    /** @brief Kích thước khung của sheet (false nếu id sai). */
    bool sheet_frame(int32_t sheet, uint16_t& w, uint16_t& h) const;

    void set_background(uint32_t color, const lv_image_dsc_t* p_image);
    void set_paused(bool paused);    /**< Do app (game.pause). */
    void set_suspended(bool suspended); /**< Do hệ thống (app vào nền / lỗi / kill). */
    void set_callbacks(LogicFn logic, TouchFn touch_fn, void* p_ctx);
    void stats(Stats& out) const;

private:
    struct Sheet {
        const lv_image_dsc_t* p_image;
        uint16_t frame_w;
        uint16_t frame_h;
        uint16_t columns;
    };

    static void on_timer(lv_timer_t* p_timer);
    static void on_event(lv_event_t* p_event);
    static void on_dirty(const GameRect& rect, void* p_ctx);
    static void on_display_event(lv_event_t* p_event);
    void flush_dirty();
    void build_order();
    void detach_display();

    static constexpr int32_t TILE = 32;          /**< Ô gom vùng bẩn (px). */
    static constexpr size_t TILE_ROWS = 64U;     /**< Scene tối đa 2048 px mỗi chiều. */
    static constexpr size_t MAX_INVALIDATIONS = 24U; /**< Dưới bộ đệm 32 vùng của LVGL; nhiều hơn thì gộp 1 khung. */
    void tick();
    void draw(lv_layer_t* p_layer);
    void touch(lv_event_code_t code);

    GameWorld m_world;
    Sheet m_sheets[MAX_SHEETS] = {};
    size_t m_sheet_count = 0U;
    lv_obj_t* m_p_obj = nullptr;
    lv_timer_t* m_p_timer = nullptr;
    const lv_image_dsc_t* m_p_background = nullptr;
    uint32_t m_bg_color = 0U;
    uint32_t m_logic_period_us = 0U;
    uint32_t m_logic_acc_us = 0U;
    uint32_t m_last_us = 0U;
    LogicFn m_logic = nullptr;
    TouchFn m_touch = nullptr;
    void* m_p_ctx = nullptr;
    bool m_paused = false;
    bool m_suspended = false;
    Stats m_stats = {};
    uint32_t m_frames = 0U;
    uint32_t m_fps_since_us = 0U;
    lv_point_t m_last_touch = {0, 0};
    uint64_t m_dirty_rows[TILE_ROWS] = {}; /**< Bit c của hàng r: ô (c, r) cần vẽ lại. */
    bool m_has_dirty = false;
    lv_display_t* m_p_display = nullptr;
};

} // namespace runtime

#endif /* RUNTIME_GAME_SCENE_HPP */
