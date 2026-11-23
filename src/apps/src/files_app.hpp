/**
 * @file    files_app.hpp
 * @brief   [Private] File Manager: duyệt thẻ SD (sidebar, breadcrumb, List/Grid, sắp xếp), quản lý file (thư mục mới,
 *          đổi tên, copy / cut -> paste, thùng rác), mở file (cài .gapp, xem chữ, thông tin) và danh sách gần đây.
 *
 * Dữ liệu hệ thống của app nằm trong thư mục ẩn ".gach" ở gốc thẻ: trash/ (file đã xóa), trashinfo/ (đường dẫn gốc
 * để khôi phục), recent (danh sách mở gần đây).
 */
#ifndef APPS_FILES_APP_HPP
#define APPS_FILES_APP_HPP

#include <cstddef>
#include <cstdint>

#include "framework/app.hpp"
#include "framework/file_service.hpp"

namespace apps {

/** @brief App "Files". */
class FilesApp final : public fw::App {
public:
    FilesApp();
    void on_create(lv_obj_t* p_root) override;
    void on_resume() override;
    void on_pause() override;
    bool on_back() override;

    static constexpr size_t MAX_ENTRIES = 512U; /**< Mục tối đa một thư mục (dư: báo bị cắt). */
    static constexpr size_t PAGE_SIZE = 48U;    /**< Mục dựng cùng lúc (giữ heap LVGL nhỏ). */
    static constexpr size_t HISTORY_MAX = 16U;
    static constexpr size_t QUEUE_MAX = 64U;    /**< Clipboard / hàng đợi chép-xóa. */
    static constexpr size_t RECENT_MAX = 20U;
    static constexpr size_t TEXT_MAX = 16U * 1024U; /**< Trình xem chữ: phần đầu file. */

    enum class Location : uint8_t { RECENT, HOME, APPS, INSTALL, TRASH, COUNT };

private:
    enum class SortKey : uint8_t { NAME, SIZE, MODIFIED };
    enum class QueueKind : uint8_t { NONE, PASTE_COPY, DELETE, INSTALL };
    enum class PromptKind : uint8_t { NONE, NEW_FOLDER, RENAME };
    enum class ClipOp : uint8_t { NONE, COPY, CUT };

    using Path = char[fw::FILE_PATH_BUF_LEN];

    // ---- Dựng giao diện ----
    void build_sidebar(lv_obj_t* p_parent);
    void build_main(lv_obj_t* p_parent);
    void build_toolbar(lv_obj_t* p_parent);
    void build_header(lv_obj_t* p_parent);
    void build_footer(lv_obj_t* p_parent);

    // ---- Điều hướng & dữ liệu ----
    void open_location(Location location);
    void navigate(const char* p_path, bool push_history);
    void history_step(bool is_forward);
    void reload();
    void load_entries();
    void load_recent_entries();
    void sort_entries();
    bool entry_path(size_t entry, char* p_out, size_t out_len) const;
    const char* display_name(size_t entry) const;
    bool is_protected(const char* p_path) const;

    // ---- Hiển thị ----
    void render();
    void render_row(lv_obj_t* p_list, size_t entry, size_t slot);
    void render_tile(lv_obj_t* p_list, size_t entry, size_t slot);
    void render_breadcrumb();
    void render_header();
    void render_footer();
    void render_storage();
    void update_selection_style();

    // ---- Chọn & thao tác ----
    void toggle_select(size_t entry);
    void clear_selection();
    void open_entry(size_t entry);
    void open_text(const char* p_path);
    void show_info(const char* p_path, const fw::FileEntry& info);
    void ask_install(const char* p_path);
    void copy_selection(ClipOp op);
    void paste();
    void trash_selection();
    void restore_selection();
    void erase_selection_forever();
    void empty_trash();
    bool move_to_trash(const char* p_path);
    bool unique_path(const char* p_dir, const char* p_name, char* p_out, size_t out_len) const;
    void start_queue(QueueKind kind);
    void finish_queue(bool is_ok);
    void add_recent(const char* p_path);
    void load_recent();
    void save_recent();
    void set_status(const char* p_text);

    // ---- Lớp phủ: menu, hộp nhập, trình xem, hộp thoại ----
    void open_menu();
    void close_menu();
    void open_prompt(PromptKind kind, const char* p_title, const char* p_initial);
    void close_prompt();
    void confirm_prompt();
    void close_viewer();
    void close_dialog();
    lv_obj_t* overlay(lv_opa_t backdrop_opa);

    // ---- Callback LVGL ----
    static void on_sidebar(lv_event_t* p_event);
    static void on_item_clicked(lv_event_t* p_event);
    static void on_item_long_pressed(lv_event_t* p_event);
    static void on_crumb(lv_event_t* p_event);
    static void on_history(lv_event_t* p_event);
    static void on_header(lv_event_t* p_event);
    static void on_view_toggle(lv_event_t* p_event);
    static void on_menu(lv_event_t* p_event);
    static void on_menu_item(lv_event_t* p_event);
    static void on_action(lv_event_t* p_event);
    static void on_page(lv_event_t* p_event);
    static void on_paste(lv_event_t* p_event);
    static void on_prompt_key(lv_event_t* p_event);
    static void on_prompt_button(lv_event_t* p_event);
    static void on_viewer_close(lv_event_t* p_event);
    static void on_dialog_cancel(lv_event_t* p_event);
    static void on_install_confirm(lv_event_t* p_event);
    static void on_forever_confirm(lv_event_t* p_event);
    static void on_empty_confirm(lv_event_t* p_event);
    static void on_job_timer(lv_timer_t* p_timer);

    lv_obj_t* m_p_root;
    lv_obj_t* m_p_side_items[static_cast<size_t>(Location::COUNT)];
    lv_obj_t* m_p_storage_bar;
    lv_obj_t* m_p_storage_label;
    lv_obj_t* m_p_back_btn;
    lv_obj_t* m_p_fwd_btn;
    lv_obj_t* m_p_crumbs;
    lv_obj_t* m_p_paste_btn;
    lv_obj_t* m_p_view_btn;
    lv_obj_t* m_p_header;
    lv_obj_t* m_p_header_cells[3];
    lv_obj_t* m_p_list;
    lv_obj_t* m_p_footer_label;
    lv_obj_t* m_p_actions;
    lv_obj_t* m_p_pager;
    lv_obj_t* m_p_page_label;
    lv_obj_t* m_p_menu;
    lv_obj_t* m_p_prompt;
    lv_obj_t* m_p_prompt_field;
    lv_obj_t* m_p_viewer;
    lv_obj_t* m_p_dialog;
    lv_timer_t* m_p_job_timer;
    lv_obj_t* m_p_items[PAGE_SIZE]; /**< Dòng / ô đang hiển thị (để đổi kiểu khi chọn). */

    Location m_location;
    Path m_path;
    fw::FileEntry m_entries[MAX_ENTRIES];
    uint16_t m_order[MAX_ENTRIES];
    bool m_selected[MAX_ENTRIES];
    size_t m_count;
    size_t m_selected_count;
    size_t m_page;
    bool m_truncated;
    bool m_is_grid;
    bool m_show_hidden;
    SortKey m_sort;
    bool m_sort_desc;
    lv_obj_t* m_p_long_pressed; /**< Bỏ CLICKED theo sau một lần nhấn giữ. */

    Path m_back[HISTORY_MAX];
    size_t m_back_count;
    Path m_forward[HISTORY_MAX];
    size_t m_forward_count;

    Path m_clip[QUEUE_MAX];
    size_t m_clip_count;
    ClipOp m_clip_op;

    Path m_queue[QUEUE_MAX];
    size_t m_queue_count;
    size_t m_queue_next;
    QueueKind m_queue_kind;
    Path m_queue_dir;     /**< Thư mục đích khi dán. */
    size_t m_queue_done;
    size_t m_queue_failed;

    Path m_recent[RECENT_MAX];
    size_t m_recent_count;

    PromptKind m_prompt_kind;
    Path m_target; /**< Mục đang được đổi tên / cài đặt. */
    char m_text[TEXT_MAX + 1U];
    char m_status[96];
};

} // namespace apps

#endif /* APPS_FILES_APP_HPP */
