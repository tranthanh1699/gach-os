/**
 * @file    game_world.hpp
 * @brief   [Private] Thế giới 2D của gach.game: bảng sprite cố định, chuyển động, animation khung, thời hạn sống,
 *          va chạm theo nhóm, chọn sprite theo điểm, gom vùng cần vẽ lại.
 *
 * Không phụ thuộc LVGL / Lua (test được trên máy tính). Đơn vị: pixel của scene, ms.
 * Id sprite = (thế hệ << 10) | (ô + 1): id của sprite đã xóa không bao giờ trỏ nhầm sang sprite mới cùng ô.
 */
#ifndef RUNTIME_GAME_WORLD_HPP
#define RUNTIME_GAME_WORLD_HPP

#include <cstddef>
#include <cstdint>

namespace runtime {

/** @brief Hình chữ nhật (pixel, gồm cả hai mép: x2 = x + w - 1). */
struct GameRect {
    int32_t x1;
    int32_t y1;
    int32_t x2;
    int32_t y2;
};

/** @brief Một sprite. Phần "trạng thái nội bộ" do GameWorld quản lý. */
struct Sprite {
    // ---- Thuộc tính (đặt qua add / binding) ----
    float x;            /**< Góc trên trái. */
    float y;
    float vx;           /**< px/s. */
    float vy;
    uint16_t w;         /**< Kích thước khung (px). */
    uint16_t h;
    int16_t sheet;      /**< Sprite sheet (-1 = không ảnh: hình chữ nhật màu). */
    uint16_t frame;
    uint32_t color;     /**< Màu khi không có ảnh (0xRRGGBB). */
    uint8_t layer;      /**< 0..7, lớp cao vẽ sau. */
    uint8_t group;      /**< 0..15 (va chạm / pick). */
    uint8_t opa;        /**< 0..255. */
    bool visible;
    bool flip;
    // ---- Trạng thái nội bộ ----
    uint16_t anim_from;
    uint16_t anim_to;
    uint16_t anim_period_ms; /**< 0 = không chạy animation. */
    bool anim_loop;
    uint32_t anim_acc_ms;
    float tx;
    float ty;
    uint32_t move_left_ms;   /**< 0 = không có move_to. */
    uint32_t life_ms;        /**< 0 = sống mãi. */
    bool changed;            /**< Thuộc tính đổi ngoài step() (binding đặt): cần vẽ lại. */
};

/** @brief Sự kiện sinh ra trong step(), lấy bằng take_events(). */
struct GameEvent {
    enum Kind : uint8_t { OVERLAP, ANIM_DONE, MOVE_DONE, EXPIRED };
    Kind kind;
    int32_t a;
    int32_t b; /**< Chỉ OVERLAP: sprite nhóm sau. */
};

/** @brief Thế giới sprite. Không thread-safe (chỉ task UI dùng). */
class GameWorld {
public:
    static constexpr size_t MAX_SPRITES = 1024U;
    static constexpr size_t MAX_GROUPS = 16U;
    static constexpr size_t MAX_LAYERS = 8U;
    static constexpr size_t MAX_OVERLAP_EVENTS = 64U; /**< Mỗi tick; dư thì báo ở tick sau (nếu vẫn chạm). */
    static constexpr size_t MAX_EVENTS = 256U;
    static constexpr size_t MAX_CONTACTS = 1024U;     /**< Cặp đang chạm được nhớ (để chỉ báo khi bắt đầu chạm). */
    static constexpr int32_t INVALID = -1;

    using DirtyFn = void (*)(const GameRect& rect, void* p_ctx);

    /** @brief Xóa mọi sprite, cặp theo dõi, sự kiện. */
    void clear();

    /** @brief Thêm sprite (các trường nội bộ được đặt lại). @return id, INVALID nếu đầy. */
    int32_t add(const Sprite& sprite);

    /** @brief Sprite theo id; NULL nếu không tồn tại (đã xóa / id sai). Sửa thuộc tính xong đặt changed = true. */
    Sprite* get(int32_t id);
    const Sprite* get(int32_t id) const;

    /** @brief Xóa sprite (vùng cũ được vẽ lại ở step kế tiếp). */
    void remove(int32_t id);

    /** @brief Bật / tắt theo dõi chạm giữa hai nhóm. */
    void watch_overlap(uint8_t group_a, uint8_t group_b, bool is_on);

    /** @brief Chạy animation khung from..to, period_ms mỗi khung. */
    void play(int32_t id, uint16_t from, uint16_t to, uint16_t period_ms, bool loop);

    /** @brief Đi thẳng tới (x, y) trong ms (vận tốc bị bỏ qua tới khi tới đích). */
    void move_to(int32_t id, float x, float y, uint32_t ms);

    /** @brief Tự xóa sau ms (sự kiện EXPIRED). */
    void set_life(int32_t id, uint32_t ms);

    /** @brief Sprite hiển thị trên cùng tại điểm (lớp cao, rồi y lớn). group < 0 = mọi nhóm. @return id / INVALID. */
    int32_t pick(int32_t x, int32_t y, int16_t group) const;

    /** @brief Tiến dt ms: chuyển động, animation, thời hạn, va chạm; báo vùng cần vẽ lại (cũ và mới). */
    void step(uint32_t dt_ms, DirtyFn dirty, void* p_ctx);

    /** @brief Lấy (và xóa) sự kiện đã sinh. @return số sự kiện chép. */
    size_t take_events(GameEvent* p_out, size_t max);

    size_t count() const { return m_count; }

    /** @brief Duyệt sprite đang tồn tại (cho bộ vẽ): id thứ index trong [0, MAX_SPRITES), INVALID nếu ô trống. */
    int32_t id_at(size_t slot) const;

    /** @brief Hình chữ nhật hiện tại của sprite. */
    static GameRect rect_of(const Sprite& sprite);

private:
    struct Slot {
        Sprite sprite;
        GameRect drawn; /**< Vùng đã báo vẽ lần trước. */
        uint16_t generation;
        bool used;
        bool has_drawn;
    };

    int32_t make_id(size_t slot) const;
    void push_event(GameEvent::Kind kind, int32_t a, int32_t b);
    void collide();
    bool was_touching(uint32_t key) const;

    Slot m_slots[MAX_SPRITES] = {};
    size_t m_count = 0U;
    uint16_t m_watch[MAX_GROUPS] = {}; /**< m_watch[a] bit b = theo dõi (a, b). */
    GameEvent m_events[MAX_EVENTS] = {};
    size_t m_event_count = 0U;
    uint32_t m_contacts[MAX_CONTACTS] = {};
    size_t m_contact_count = 0U;
    uint32_t m_next_contacts[MAX_CONTACTS] = {};
    GameRect m_removed[MAX_SPRITES] = {}; /**< Vùng của sprite bị xóa, vẽ lại ở step kế. */
    size_t m_removed_count = 0U;
};

} // namespace runtime

#endif /* RUNTIME_GAME_WORLD_HPP */
