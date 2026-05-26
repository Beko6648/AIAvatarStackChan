#pragma once

#include "AIAvatarStackChan.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <M5UnitENV.h>

class UserApp {
public:
    // Called once from setup() before avatar.begin().
    bool begin(aiavatar::AIAvatar& avatar);

    // Called repeatedly from loop() after avatar.update().
    void update();

private:
    static constexpr int kPortBSdaPin = 9;
    static constexpr int kPortBSclPin = 8;

    static UserApp* s_instance;
    static void onStartStatic(const char* requestText);
    static void onFinalStatic(const char* responseText, const char* voiceText);
    static void onToolCallStatic(const char* toolName);
    static void onAcceptedStatic();
    static void onOverlayStatic(LGFX_Sprite* canvas);

    void onStart(const char* requestText);
    void onFinal(const char* responseText, const char* voiceText);
    void onToolCall(const char* toolName);
    void onAccepted();
    void onOverlay(LGFX_Sprite* canvas);

    void updateEnvironment();
    void drawEnvironment(LGFX_Sprite* canvas) const;

    aiavatar::AIAvatar* avatar_ = nullptr;
    SHT3X sht30_;

    bool envReady_ = false;
    int8_t temperatureC_ = 0;
    uint8_t humidity_ = 0;
    uint32_t lastEnvUpdateMs_ = 0;

};
