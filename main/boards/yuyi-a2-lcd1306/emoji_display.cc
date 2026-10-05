#include "emoji_display.h"
#include "robo_eyes.h"
#include "assets/lang_config.h"
#include "lvgl_theme.h"
#include "lvgl_font.h"

#include <string>
#include <algorithm>
#include <cstring>

#include <esp_log.h>
#include <esp_err.h>
#include <esp_lvgl_port.h>

#define TAG "EmoteDisplay"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);

// Static RoboEyes instance
static RoboEyes robo_eyes_;

// Reverse bits in a byte: RoboEyes LSB-first → LVGL I1 MSB-first
static inline uint8_t reverse_byte(uint8_t b) {
    b = ((b & 0xF0) >> 4) | ((b & 0x0F) << 4);
    b = ((b & 0xCC) >> 2) | ((b & 0x33) << 2);
    b = ((b & 0xAA) >> 1) | ((b & 0x55) << 1);
    return b;
}

EmoteDisplay::EmoteDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           int width, int height, bool mirror_x, bool mirror_y)
    : panel_io_(panel_io), panel_(panel) {
    width_ = width;
    height_ = height;

    auto text_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);
    auto icon_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_ICON_FONT);

    auto dark_theme = new LvglTheme("dark");
    dark_theme->set_text_font(text_font);
    dark_theme->set_icon_font(icon_font);
    dark_theme->set_large_icon_font(icon_font);

    auto& theme_manager = LvglThemeManager::GetInstance();
    theme_manager.RegisterTheme("dark", dark_theme);
    current_theme_ = dark_theme;

    // Initialize LVGL
    ESP_LOGI(TAG, "Initialize LVGL");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.task_stack = 6144;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    port_cfg.task_affinity = 1;
#endif
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding OLED display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * height_),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = true,
        .rotation = {
            .swap_xy = false,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .flags = {
            .buff_dma = 1,
            .buff_spiram = 0,
            .sw_rotate = 0,
            .full_refresh = 0,
            .direct_mode = 0,
        },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    // Initialize RoboEyes with larger eyes for full-screen 128x64 display
    robo_eyes_.resetDefaults();
    robo_eyes_.setWidth(44, 44);
    robo_eyes_.setHeight(40, 40);
    robo_eyes_.setBorderradius(10, 10);
    robo_eyes_.setSpacebetween(10);
    robo_eyes_.recenter();
    robo_eyes_.open();
    robo_eyes_.setAutoblinker(true, 2, 3);
    robo_eyes_.setIdleMode(true, 2, 3);
    robo_eyes_.setCuriosity(true);

    // Create UI screens (must be inside LVGL lock)
    Lock(5000);
    SetupEmoteScreen();
    SetupTextScreen();

    // Load emote screen first
    lv_screen_load(emote_screen_);

    // Create LVGL timer for animation (runs in LVGL task, no lock needed)
    emote_timer_ = lv_timer_create(EmoteTimerCallback, 33, this);  // ~30fps

    // Create emote notification auto-hide timer
    esp_timer_create_args_t notif_timer_args = {
        .callback = &EmoteDisplay::EmoteNotificationTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "emote_notif_timer",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&notif_timer_args, &emote_notification_timer_));

    Unlock();

    ESP_LOGI(TAG, "EmoteDisplay initialized with full-screen eyes");
}

EmoteDisplay::~EmoteDisplay() {
    if (emote_notification_timer_ != nullptr) {
        esp_timer_stop(emote_notification_timer_);
        esp_timer_delete(emote_notification_timer_);
        emote_notification_timer_ = nullptr;
    }
    Lock(5000);
    if (emote_timer_ != nullptr) {
        lv_timer_delete(emote_timer_);
        emote_timer_ = nullptr;
    }
    Unlock();
    if (panel_ != nullptr) {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr) {
        esp_lcd_panel_io_del(panel_io_);
    }
    lvgl_port_deinit();
}

bool EmoteDisplay::Lock(int timeout_ms) {
    return lvgl_port_lock(timeout_ms);
}

void EmoteDisplay::Unlock() {
    lvgl_port_unlock();
}

void EmoteDisplay::SetupEmoteScreen() {
    emote_screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(emote_screen_, lv_color_black(), 0);
    lv_obj_set_style_text_color(emote_screen_, lv_color_white(), 0);

    // Full-screen canvas using lv_draw_buf_create (handles palette + stride alignment)
    emote_canvas_ = lv_canvas_create(emote_screen_);
    emote_draw_buf_ = lv_draw_buf_create(128, 64, LV_COLOR_FORMAT_I1, LV_STRIDE_AUTO);
    if (emote_draw_buf_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create emote draw buffer!");
        return;
    }
    lv_canvas_set_draw_buf(emote_canvas_, emote_draw_buf_);

    // Set I1 palette: index 0 = black (background), index 1 = white (pixels)
    lv_canvas_set_palette(emote_canvas_, 0, lv_color32_make(0, 0, 0, 255));
    lv_canvas_set_palette(emote_canvas_, 1, lv_color32_make(255, 255, 255, 255));

    lv_obj_set_size(emote_canvas_, 128, 64);
    lv_obj_center(emote_canvas_);
    lv_canvas_fill_bg(emote_canvas_, lv_color_black(), LV_OPA_COVER);

    // Notification overlay (small text at bottom, hidden by default)
    auto text_font = static_cast<LvglTheme*>(current_theme_)->text_font()->font();
    emote_notification_label_ = lv_label_create(emote_screen_);
    lv_label_set_text(emote_notification_label_, "");
    lv_obj_set_style_text_font(emote_notification_label_, text_font, 0);
    lv_obj_set_style_text_color(emote_notification_label_, lv_color_white(), 0);
    lv_obj_set_style_bg_color(emote_notification_label_, lv_color_black(), LV_OPA_COVER);
    lv_obj_set_style_bg_opa(emote_notification_label_, LV_OPA_80, 0);
    lv_obj_set_style_pad_all(emote_notification_label_, 2, 0);
    lv_obj_align(emote_notification_label_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(emote_notification_label_, LV_OBJ_FLAG_HIDDEN);
}

void EmoteDisplay::SetupTextScreen() {
    text_screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(text_screen_, lv_color_black(), 0);
    lv_obj_set_style_text_color(text_screen_, lv_color_black(), 0);

    auto text_font = static_cast<LvglTheme*>(current_theme_)->text_font()->font();
    auto icon_font = static_cast<LvglTheme*>(current_theme_)->icon_font()->font();
    lv_obj_set_style_text_font(text_screen_, text_font, 0);

    // Simple status bar with all labels the base class expects
    lv_obj_t* status_bar = lv_obj_create(text_screen_);
    lv_obj_set_size(status_bar, LV_HOR_RES, 16);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_pad_all(status_bar, 0, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);
    lv_obj_set_flex_flow(status_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(status_bar, 0, 0);

    network_label_ = lv_label_create(status_bar);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);

    notification_label_ = lv_label_create(status_bar);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);

    mute_label_ = lv_label_create(status_bar);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);

    battery_label_ = lv_label_create(status_bar);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);

    // Content area
    lv_obj_t* content = lv_obj_create(text_screen_);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_size(content, LV_HOR_RES, LV_VER_RES - 16);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(content, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, 0);

    text_status_label_ = lv_label_create(content);
    lv_label_set_text(text_status_label_, "");
    lv_obj_set_style_text_align(text_status_label_, LV_TEXT_ALIGN_CENTER, 0);

    // Low battery popup
    low_battery_popup_ = lv_obj_create(text_screen_);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(low_battery_popup_, lv_color_black(), 0);
    lv_obj_set_style_radius(low_battery_popup_, 10, 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
}

void EmoteDisplay::SetEmoteMode(bool emote) {
    if (emote_mode_ == emote) return;
    emote_mode_ = emote;

    // Caller must hold the LVGL lock
    if (emote) {
        lv_screen_load(emote_screen_);
        if (emote_timer_) lv_timer_resume(emote_timer_);
        ESP_LOGI(TAG, "Switched to EMOTE mode");
    } else {
        lv_screen_load(text_screen_);
        ESP_LOGI(TAG, "Switched to TEXT mode");
    }
}

void EmoteDisplay::SetStatus(const char* status) {
    DisplayLockGuard lock(this);
    SetEmoteMode(false);
    LvglDisplay::SetStatus(status);
}

void EmoteDisplay::ShowNotification(const char* notification, int duration_ms) {
    if (emote_mode_) {
        DisplayLockGuard lock(this);
        if (emote_notification_label_ != nullptr) {
            lv_label_set_text(emote_notification_label_, notification);
            lv_obj_remove_flag(emote_notification_label_, LV_OBJ_FLAG_HIDDEN);
        }
        // Auto-hide with our own timer (base class timer modifies text_screen labels)
        if (emote_notification_timer_ != nullptr) {
            esp_timer_stop(emote_notification_timer_);
            esp_timer_start_once(emote_notification_timer_, duration_ms * 1000);
        }
    } else {
        DisplayLockGuard lock(this);
        LvglDisplay::ShowNotification(notification, duration_ms);
    }
}

void EmoteDisplay::EmoteNotificationTimerCallback(void* arg) {
    EmoteDisplay* self = static_cast<EmoteDisplay*>(arg);
    DisplayLockGuard lock(self);
    if (self->emote_notification_label_ != nullptr) {
        lv_obj_add_flag(self->emote_notification_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void EmoteDisplay::ShowNotification(const std::string& notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}

void EmoteDisplay::SetEmotion(const char* emotion) {
    DisplayLockGuard lock(this);
    SetEmoteMode(true);

    // Reset any active animations
    robo_eyes_.setMood(ROBO_MOOD_DEFAULT);
    robo_eyes_.setHFlicker(false);
    robo_eyes_.setVFlicker(false);
    robo_eyes_.setSweat(false);

    // Restore default eye size
    robo_eyes_.setWidth(44, 44);
    robo_eyes_.setHeight(40, 40);
    robo_eyes_.setBorderradius(10, 10);
    robo_eyes_.recenter();

    if (emotion != nullptr) {
        std::string emote_str(emotion);
        if (emote_str == "happy" || emote_str == "laughing" || emote_str == "smile") {
            robo_eyes_.setMood(ROBO_MOOD_HAPPY);
            robo_eyes_.anim_laugh();
        } else if (emote_str == "sad" || emote_str == "crying" || emote_str == "sorrow") {
            robo_eyes_.setMood(ROBO_MOOD_TIRED);
            robo_eyes_.setSweat(true);
        } else if (emote_str == "angry" || emote_str == "frustrated" || emote_str == "mad") {
            robo_eyes_.setMood(ROBO_MOOD_ANGRY);
        } else if (emote_str == "surprised" || emote_str == "shocked") {
            robo_eyes_.setWidth(48, 48);
            robo_eyes_.setHeight(44, 44);
            robo_eyes_.setBorderradius(12, 12);
            robo_eyes_.recenter();
        } else if (emote_str == "confused" || emote_str == "thinking") {
            robo_eyes_.anim_confused();
        } else if (emote_str == "love" || emote_str == "heart") {
            robo_eyes_.setMood(ROBO_MOOD_HAPPY);
        } else if (emote_str == "winking") {
            robo_eyes_.close(false, true);
            robo_eyes_.open(false, true);
        } else if (emote_str == "sleeping" || emote_str == "sleepy") {
            robo_eyes_.setMood(ROBO_MOOD_TIRED);
            robo_eyes_.close();
        } else if (emote_str == "neutral" || emote_str == "idle") {
            robo_eyes_.setMood(ROBO_MOOD_DEFAULT);
            robo_eyes_.setIdleMode(true, 2, 3);
        } else {
            robo_eyes_.setMood(ROBO_MOOD_DEFAULT);
            robo_eyes_.setIdleMode(true, 2, 3);
            robo_eyes_.setCuriosity(true);
        }
    }
}

void EmoteDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    SetEmoteMode(true);

    if (role != nullptr) {
        std::string role_str(role);
        if (role_str == "assistant" || role_str == "system") {
            robo_eyes_.setMood(ROBO_MOOD_HAPPY);
            robo_eyes_.setCuriosity(true);
            robo_eyes_.setIdleMode(true, 1, 2);
        } else if (role_str == "user") {
            robo_eyes_.setMood(ROBO_MOOD_DEFAULT);
            robo_eyes_.setIdleMode(true, 2, 3);
            robo_eyes_.setCuriosity(true);
        }
    }
}

void EmoteDisplay::SetTheme(Theme* theme) {
    DisplayLockGuard lock(this);
    auto lvgl_theme = static_cast<LvglTheme*>(theme);
    auto text_font = lvgl_theme->text_font()->font();
    lv_obj_set_style_text_font(emote_screen_, text_font, 0);
    lv_obj_set_style_text_font(text_screen_, text_font, 0);
}

void EmoteDisplay::UpdateStatusBar(bool update_all) {
    if (!emote_mode_) {
        LvglDisplay::UpdateStatusBar(update_all);
    }
}

// --- Emote Animation (LVGL timer, runs in LVGL task context) ---

void EmoteDisplay::EmoteTimerCallback(lv_timer_t* timer) {
    EmoteDisplay* self = static_cast<EmoteDisplay*>(lv_timer_get_user_data(timer));
    self->RenderEmoteFrame();
}

void EmoteDisplay::RenderEmoteFrame() {
    if (!emote_mode_ || emote_draw_buf_ == nullptr) return;

    unsigned long now_ms = (unsigned long)(esp_timer_get_time() / 1000);

    // Render RoboEyes into its internal buffer
    robo_eyes_.drawFrame(now_ms);

    // Get draw buffer layout info
    uint32_t stride = emote_draw_buf_->header.stride;
    uint32_t palette_size = LV_COLOR_INDEXED_PALETTE_SIZE(LV_COLOR_FORMAT_I1) * sizeof(lv_color32_t);
    uint8_t* pixel_data = emote_draw_buf_->data + palette_size;

    // RoboEyes: 128/8 = 16 bytes per row, LSB-first
    // LVGL I1: stride bytes per row, MSB-first, palette at start of data
    const int src_row_bytes = 128 / 8;  // 16

    for (int y = 0; y < 64; y++) {
        const uint8_t* src_row = &robo_eyes_.buffer.buf[y * src_row_bytes];
        uint8_t* dst_row = &pixel_data[y * stride];

        // Copy, reverse bits (LSB → MSB), and INVERT (swap eye/background colors)
        for (int x = 0; x < src_row_bytes; x++) {
            dst_row[x] = ~reverse_byte(src_row[x]);
        }
        // Pad remaining stride bytes with 0
        if (stride > (uint32_t)src_row_bytes) {
            memset(&dst_row[src_row_bytes], 0, stride - src_row_bytes);
        }
    }

    // Mark canvas for redraw (we're in LVGL task, no lock needed)
    lv_obj_invalidate(emote_canvas_);
}
