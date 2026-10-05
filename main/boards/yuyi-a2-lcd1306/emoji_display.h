#ifndef EMOJI_DISPLAY_H
#define EMOJI_DISPLAY_H

#include "display.h"
#include "lvgl_display.h"

#include <lvgl.h>
#include <esp_lcd_types.h>
#include <esp_lcd_panel_io.h>

class EmoteDisplay : public LvglDisplay {
public:
    EmoteDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                 int width, int height, bool mirror_x, bool mirror_y);
    ~EmoteDisplay();

    void SetEmoteMode(bool emote);

    // Override Display methods
    void SetStatus(const char* status) override;
    void ShowNotification(const char* notification, int duration_ms = 3000) override;
    void ShowNotification(const std::string& notification, int duration_ms = 3000) override;
    void SetEmotion(const char* emotion) override;
    void SetChatMessage(const char* role, const char* content) override;
    void SetTheme(Theme* theme) override;
    void UpdateStatusBar(bool update_all = false) override;

protected:
    bool Lock(int timeout_ms = 0) override;
    void Unlock() override;

private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    bool emote_mode_ = true;  // default to emote mode

    // Emote screen (full-screen canvas)
    lv_obj_t* emote_screen_ = nullptr;
    lv_obj_t* emote_canvas_ = nullptr;
    lv_draw_buf_t* emote_draw_buf_ = nullptr;
    // Notification overlay on emote screen
    lv_obj_t* emote_notification_label_ = nullptr;

    // Text screen (for WiFi config / verification code)
    lv_obj_t* text_screen_ = nullptr;
    lv_obj_t* text_status_label_ = nullptr;

    // LVGL timer for animation (runs in LVGL task context, no locking needed)
    lv_timer_t* emote_timer_ = nullptr;
    static void EmoteTimerCallback(lv_timer_t* timer);

    // Emote notification auto-hide timer
    esp_timer_handle_t emote_notification_timer_ = nullptr;
    static void EmoteNotificationTimerCallback(void* arg);

    void SetupEmoteScreen();
    void SetupTextScreen();
    void RenderEmoteFrame();
};

#endif // EMOJI_DISPLAY_H
