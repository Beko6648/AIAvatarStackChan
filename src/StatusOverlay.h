#pragma once

#include <M5Unified.h>
#include <cstdint>

namespace aiavatar {

struct UiRect {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;

    bool contains(int16_t px, int16_t py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

struct StatusOverlayState {
    bool micMuted;
    bool volumeVisible;
    uint8_t volumeLevel;
    uint8_t volumeLevelCount;
    bool wifiConnected;
    bool websocketConnected;
    int8_t batteryLevel;
    bool batteryCharging;
    uint8_t hour;
    uint8_t minute;
};

struct StatusOverlayLayout {
    int16_t clockX;
    int16_t clockY;
    textdatum_t clockDatum;
    uint8_t clockTextSize;
    UiRect micBounds;
    UiRect networkBounds;
    UiRect batteryBounds;
    UiRect volumeTapBounds;
    int16_t volumeIndicatorX;
};

class StatusOverlay {
public:
    StatusOverlay();

    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }
    bool update(const StatusOverlayState& state);
    void draw(LGFX_Sprite* canvas) const;
    void setLayout(const StatusOverlayLayout& layout) { layout_ = layout; }
    void setClockPosition(int16_t x, int16_t y, textdatum_t datum = top_left);
    void setClockTextSize(uint8_t size) { layout_.clockTextSize = size; }
    void setMicBounds(UiRect bounds) { layout_.micBounds = bounds; }
    void setNetworkBounds(UiRect bounds) { layout_.networkBounds = bounds; }
    void setBatteryBounds(UiRect bounds) { layout_.batteryBounds = bounds; }
    void setVolumeTapBounds(UiRect bounds) { layout_.volumeTapBounds = bounds; }
    void setVolumeIndicatorX(int16_t x) { layout_.volumeIndicatorX = x; }
    const StatusOverlayLayout& layout() const { return layout_; }
    UiRect micBounds() const { return layout_.micBounds; }
    UiRect networkBounds() const { return layout_.networkBounds; }
    UiRect batteryBounds() const { return layout_.batteryBounds; }
    UiRect volumeTapBounds() const { return layout_.volumeTapBounds; }

private:
    bool enabled_;
    bool hasState_;
    StatusOverlayState state_;
    StatusOverlayLayout layout_;

    static bool equals(const StatusOverlayState& a, const StatusOverlayState& b);
    void drawClock(LGFX_Sprite* canvas, uint8_t hour, uint8_t minute) const;
    void drawBatteryIcon(LGFX_Sprite* canvas, int8_t level, bool charging) const;
    void drawWiFiIcon(LGFX_Sprite* canvas, bool wifiConnected, bool wsConnected) const;
    void drawMicIcon(LGFX_Sprite* canvas, bool muted) const;
    void drawVolumeIndicator(LGFX_Sprite* canvas, uint8_t level, uint8_t levelCount) const;
};

}  // namespace aiavatar
