/**
 * @file    game_world.cpp
 * @brief   Logic thế giới sprite của gach.game (không LVGL / Lua).
 */
#include "game_world.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace runtime {

namespace {

constexpr uint32_t SLOT_BITS = 11U; /**< Ô + 1 tối đa 1024 cần 11 bit. */
constexpr uint32_t SLOT_MASK = (1U << SLOT_BITS) - 1U;
constexpr uint32_t GENERATION_MASK = 0xFFFFU;
constexpr uint32_t CONTACT_SHIFT = 16U;
constexpr float MS_PER_S = 1000.0F;

/** @brief Danh sách ô theo nhóm (dựng lại mỗi tick khi có theo dõi va chạm). */
uint16_t s_group_slots[GameWorld::MAX_GROUPS][GameWorld::MAX_SPRITES];
size_t s_group_count[GameWorld::MAX_GROUPS];

bool overlaps(const GameRect& a, const GameRect& b)
{
    return (a.x1 <= b.x2) && (b.x1 <= a.x2) && (a.y1 <= b.y2) && (b.y1 <= a.y2);
}

bool same_rect(const GameRect& a, const GameRect& b)
{
    return (a.x1 == b.x1) && (a.y1 == b.y1) && (a.x2 == b.x2) && (a.y2 == b.y2);
}

} // namespace

GameRect GameWorld::rect_of(const Sprite& sprite)
{
    const auto x = static_cast<int32_t>(std::floor(sprite.x));
    const auto y = static_cast<int32_t>(std::floor(sprite.y));
    return GameRect{x, y, x + static_cast<int32_t>(sprite.w) - 1, y + static_cast<int32_t>(sprite.h) - 1};
}

void GameWorld::clear()
{
    for (Slot& slot : m_slots) {
        slot.used = false;
        slot.has_drawn = false;
    }
    m_count = 0U;
    std::memset(m_watch, 0, sizeof(m_watch));
    m_event_count = 0U;
    m_contact_count = 0U;
    m_removed_count = 0U;
}

int32_t GameWorld::make_id(size_t slot) const
{
    return static_cast<int32_t>((static_cast<uint32_t>(m_slots[slot].generation) << SLOT_BITS) |
                                static_cast<uint32_t>(slot + 1U));
}

int32_t GameWorld::add(const Sprite& sprite)
{
    for (size_t index = 0U; index < MAX_SPRITES; ++index) {
        Slot& slot = m_slots[index];
        if (slot.used) {
            continue;
        }
        slot.sprite = sprite;
        Sprite& s = slot.sprite;
        s.anim_period_ms = 0U;
        s.anim_acc_ms = 0U;
        s.move_left_ms = 0U;
        s.life_ms = 0U;
        s.layer = static_cast<uint8_t>(s.layer % MAX_LAYERS);
        s.group = static_cast<uint8_t>(s.group % MAX_GROUPS);
        s.changed = true;
        slot.generation = static_cast<uint16_t>((slot.generation + 1U) & GENERATION_MASK);
        slot.used = true;
        slot.has_drawn = false;
        ++m_count;
        return make_id(index);
    }
    return INVALID;
}

Sprite* GameWorld::get(int32_t id)
{
    return const_cast<Sprite*>(static_cast<const GameWorld*>(this)->get(id));
}

const Sprite* GameWorld::get(int32_t id) const
{
    if (id <= 0) {
        return nullptr;
    }
    const uint32_t raw = static_cast<uint32_t>(id);
    const uint32_t slot = (raw & SLOT_MASK);
    if ((slot == 0U) || (slot > MAX_SPRITES)) {
        return nullptr;
    }
    const Slot& entry = m_slots[slot - 1U];
    return (entry.used && (entry.generation == (raw >> SLOT_BITS))) ? &entry.sprite : nullptr;
}

void GameWorld::remove(int32_t id)
{
    if (get(id) == nullptr) {
        return;
    }
    const size_t slot = (static_cast<uint32_t>(id) & SLOT_MASK) - 1U;
    Slot& entry = m_slots[slot];
    if (entry.has_drawn && (m_removed_count < MAX_SPRITES)) {
        m_removed[m_removed_count++] = entry.drawn;
    }
    entry.used = false;
    entry.has_drawn = false;
    --m_count;
    // Bỏ các cặp đang chạm có ô này: sprite mới vào cùng ô phải được báo chạm lại từ đầu.
    size_t kept = 0U;
    for (size_t index = 0U; index < m_contact_count; ++index) {
        const uint32_t key = m_contacts[index];
        if (((key >> CONTACT_SHIFT) != slot) && ((key & ((1U << CONTACT_SHIFT) - 1U)) != slot)) {
            m_contacts[kept++] = key;
        }
    }
    m_contact_count = kept;
}

void GameWorld::watch_overlap(uint8_t group_a, uint8_t group_b, bool is_on)
{
    if ((group_a >= MAX_GROUPS) || (group_b >= MAX_GROUPS)) {
        return;
    }
    const auto bit = static_cast<uint16_t>(1U << group_b);
    m_watch[group_a] = is_on ? static_cast<uint16_t>(m_watch[group_a] | bit)
                             : static_cast<uint16_t>(m_watch[group_a] & ~bit);
}

void GameWorld::play(int32_t id, uint16_t from, uint16_t to, uint16_t period_ms, bool loop)
{
    Sprite* const p_sprite = get(id);
    if (p_sprite == nullptr) {
        return;
    }
    p_sprite->anim_from = from;
    p_sprite->anim_to = (to < from) ? from : to;
    p_sprite->anim_period_ms = period_ms;
    p_sprite->anim_loop = loop;
    p_sprite->anim_acc_ms = 0U;
    p_sprite->frame = from;
    p_sprite->changed = true;
}

void GameWorld::move_to(int32_t id, float x, float y, uint32_t ms)
{
    Sprite* const p_sprite = get(id);
    if (p_sprite == nullptr) {
        return;
    }
    p_sprite->tx = x;
    p_sprite->ty = y;
    p_sprite->move_left_ms = (ms == 0U) ? 1U : ms;
}

void GameWorld::set_life(int32_t id, uint32_t ms)
{
    Sprite* const p_sprite = get(id);
    if (p_sprite != nullptr) {
        p_sprite->life_ms = (ms == 0U) ? 1U : ms;
    }
}

int32_t GameWorld::pick(int32_t x, int32_t y, int16_t group) const
{
    int32_t best = INVALID;
    int32_t best_layer = -1;
    float best_y = 0.0F;
    for (size_t index = 0U; index < MAX_SPRITES; ++index) {
        const Slot& slot = m_slots[index];
        const Sprite& s = slot.sprite;
        if (!slot.used || !s.visible || ((group >= 0) && (s.group != group))) {
            continue;
        }
        const GameRect r = rect_of(s);
        if ((x < r.x1) || (x > r.x2) || (y < r.y1) || (y > r.y2)) {
            continue;
        }
        if ((static_cast<int32_t>(s.layer) > best_layer) ||
            ((static_cast<int32_t>(s.layer) == best_layer) && (s.y >= best_y))) {
            best = make_id(index);
            best_layer = s.layer;
            best_y = s.y;
        }
    }
    return best;
}

void GameWorld::push_event(GameEvent::Kind kind, int32_t a, int32_t b)
{
    if (m_event_count < MAX_EVENTS) {
        m_events[m_event_count++] = GameEvent{kind, a, b};
    }
}

size_t GameWorld::take_events(GameEvent* p_out, size_t max)
{
    const size_t count = (m_event_count < max) ? m_event_count : max;
    std::memcpy(p_out, m_events, count * sizeof(GameEvent));
    std::memmove(m_events, &m_events[count], (m_event_count - count) * sizeof(GameEvent));
    m_event_count -= count;
    return count;
}

int32_t GameWorld::id_at(size_t slot) const
{
    return ((slot < MAX_SPRITES) && m_slots[slot].used) ? make_id(slot) : INVALID;
}

bool GameWorld::was_touching(uint32_t key) const
{
    return std::binary_search(m_contacts, m_contacts + m_contact_count, key);
}

void GameWorld::collide()
{
    uint16_t watched_groups = 0U;
    for (size_t group = 0U; group < MAX_GROUPS; ++group) {
        if (m_watch[group] != 0U) {
            watched_groups = static_cast<uint16_t>(watched_groups | (1U << group) | m_watch[group]);
        }
        s_group_count[group] = 0U;
    }
    if (watched_groups == 0U) {
        m_contact_count = 0U;
        return;
    }
    for (size_t index = 0U; index < MAX_SPRITES; ++index) {
        const Slot& slot = m_slots[index];
        if (slot.used && slot.sprite.visible && ((watched_groups & (1U << slot.sprite.group)) != 0U)) {
            s_group_slots[slot.sprite.group][s_group_count[slot.sprite.group]++] = static_cast<uint16_t>(index);
        }
    }
    size_t next_count = 0U;
    size_t reported = 0U;
    for (size_t ga = 0U; ga < MAX_GROUPS; ++ga) {
        for (size_t gb = 0U; (m_watch[ga] != 0U) && (gb < MAX_GROUPS); ++gb) {
            if ((m_watch[ga] & (1U << gb)) == 0U) {
                continue;
            }
            for (size_t ia = 0U; ia < s_group_count[ga]; ++ia) {
                const uint16_t sa = s_group_slots[ga][ia];
                const GameRect ra = rect_of(m_slots[sa].sprite);
                for (size_t ib = 0U; ib < s_group_count[gb]; ++ib) {
                    const uint16_t sb = s_group_slots[gb][ib];
                    if ((sa == sb) || !overlaps(ra, rect_of(m_slots[sb].sprite))) {
                        continue;
                    }
                    const uint32_t key = (static_cast<uint32_t>(sa) << CONTACT_SHIFT) | sb;
                    bool keep = was_touching(key);
                    if (!keep && (reported < MAX_OVERLAP_EVENTS)) {
                        push_event(GameEvent::OVERLAP, make_id(sa), make_id(sb));
                        ++reported;
                        keep = true;
                    }
                    // Chưa báo được (hết hạn mức tick này): không nhớ, tick sau báo nếu vẫn chạm.
                    if (keep && (next_count < MAX_CONTACTS)) {
                        m_next_contacts[next_count++] = key;
                    }
                }
            }
        }
    }
    std::sort(m_next_contacts, m_next_contacts + next_count);
    std::memcpy(m_contacts, m_next_contacts, next_count * sizeof(uint32_t));
    m_contact_count = next_count;
}

void GameWorld::step(uint32_t dt_ms, DirtyFn dirty, void* p_ctx)
{
    for (size_t index = 0U; index < m_removed_count; ++index) {
        dirty(m_removed[index], p_ctx);
    }
    m_removed_count = 0U;
    const float dt_s = static_cast<float>(dt_ms) / MS_PER_S;
    for (size_t index = 0U; index < MAX_SPRITES; ++index) {
        Slot& slot = m_slots[index];
        if (!slot.used) {
            continue;
        }
        Sprite& s = slot.sprite;
        const int32_t id = make_id(index);
        if (s.life_ms > 0U) {
            if (dt_ms >= s.life_ms) {
                push_event(GameEvent::EXPIRED, id, 0);
                remove(id);
                continue;
            }
            s.life_ms -= dt_ms;
        }
        if (s.move_left_ms > 0U) {
            if (dt_ms >= s.move_left_ms) {
                s.x = s.tx;
                s.y = s.ty;
                s.move_left_ms = 0U;
                push_event(GameEvent::MOVE_DONE, id, 0);
            } else {
                const float frac = static_cast<float>(dt_ms) / static_cast<float>(s.move_left_ms);
                s.x += (s.tx - s.x) * frac;
                s.y += (s.ty - s.y) * frac;
                s.move_left_ms -= dt_ms;
            }
        } else {
            s.x += s.vx * dt_s;
            s.y += s.vy * dt_s;
        }
        if (s.anim_period_ms > 0U) {
            s.anim_acc_ms += dt_ms;
            while ((s.anim_period_ms > 0U) && (s.anim_acc_ms >= s.anim_period_ms)) {
                s.anim_acc_ms -= s.anim_period_ms;
                if (s.frame < s.anim_to) {
                    ++s.frame;
                } else if (s.anim_loop) {
                    s.frame = s.anim_from;
                } else {
                    s.anim_period_ms = 0U;
                    push_event(GameEvent::ANIM_DONE, id, 0);
                }
                s.changed = true;
            }
        }
        const GameRect now = rect_of(s);
        if (!slot.has_drawn) {
            dirty(now, p_ctx);
        } else if (s.changed || !same_rect(now, slot.drawn)) {
            dirty(slot.drawn, p_ctx);
            dirty(now, p_ctx);
        } else {
            // Không đổi: không vẽ lại.
        }
        s.changed = false;
        slot.drawn = now;
        slot.has_drawn = true;
    }
    collide();
}

} // namespace runtime
