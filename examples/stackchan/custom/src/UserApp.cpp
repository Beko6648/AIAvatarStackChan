#include "UserApp.h"

UserApp* UserApp::s_instance = nullptr;

bool UserApp::begin(aiavatar::AIAvatar& avatar) {
    avatar_ = &avatar;
    s_instance = this;

    // Register AIAvatar hooks here. Replace these handlers with your own app logic.
    avatar_->onStart(UserApp::onStartStatic);
    avatar_->onFinal(UserApp::onFinalStatic);
    avatar_->onToolCall(UserApp::onToolCallStatic);
    avatar_->onAccepted(UserApp::onAcceptedStatic);
    avatar_->onOverlay(UserApp::onOverlayStatic);

    // This sample reads Unit ENV III from Port B and draws temperature/humidity.
    bool shtReady = sht30_.begin(&Wire, SHT3X_I2C_ADDR, kPortBSdaPin, kPortBSclPin, 400000U);
    envReady_ = shtReady;
    Serial.printf("[UserApp] ENV %s sht30=%d sda=%d scl=%d\n",
                  envReady_ ? "ready" : "unavailable",
                  shtReady ? 1 : 0, kPortBSdaPin, kPortBSclPin);

    return true;
}

void UserApp::update() {
    updateEnvironment();
}

// Message callbacks. This sample only logs the events.
void UserApp::onStartStatic(const char* requestText) {
    if (s_instance) s_instance->onStart(requestText);
}

void UserApp::onFinalStatic(const char* responseText, const char* voiceText) {
    if (s_instance) s_instance->onFinal(responseText, voiceText);
}

void UserApp::onToolCallStatic(const char* toolName) {
    if (s_instance) s_instance->onToolCall(toolName);
}

void UserApp::onAcceptedStatic() {
    if (s_instance) s_instance->onAccepted();
}

void UserApp::onStart(const char* requestText) {
    Serial.printf("[Main] User: %s\n", requestText ? requestText : "");
}

void UserApp::onFinal(const char* responseText, const char* voiceText) {
    (void)responseText;
    Serial.printf("[Main] StackChan: %s\n", voiceText ? voiceText : "");
}

void UserApp::onToolCall(const char* toolName) {
    Serial.printf("[Main] tool_call: %s\n", toolName ? toolName : "");
}

void UserApp::onAccepted() {
    Serial.println("[Main] accepted");
}

// Display overlay callbacks.
void UserApp::onOverlayStatic(LGFX_Sprite* canvas) {
    if (s_instance) s_instance->onOverlay(canvas);
}

void UserApp::onOverlay(LGFX_Sprite* canvas) {
    // Draw custom UI after built-in effects/status overlays and before system UI.
    if (avatar_ && avatar_->systemUI().uiVisible()) {
        drawEnvironment(canvas);
    }
}

// Unit ENV III handling.
void UserApp::updateEnvironment() {
    uint32_t now = millis();
    if (now - lastEnvUpdateMs_ < 1000) return;
    lastEnvUpdateMs_ = now;

    if (!envReady_) return;
    if (!sht30_.update()) return;

    int8_t newTemp = static_cast<int8_t>(sht30_.cTemp);
    uint8_t newHum = static_cast<uint8_t>(sht30_.humidity);
    if (newTemp != temperatureC_ || newHum != humidity_) {
        temperatureC_ = newTemp;
        humidity_ = newHum;
        if (avatar_) avatar_->display().setDirty();
    }
}

void UserApp::drawEnvironment(LGFX_Sprite* canvas) const {
    if (!canvas || !envReady_) return;

    // Keep the sample overlay compact so it does not cover StackChan's face.
    const int bY = 36;
    const int bX = 18;

    const int tcx = bX + 5;
    canvas->fillRoundRect(tcx - 2, bY, 4, 12, 1, TFT_WHITE);
    canvas->fillCircle(tcx, bY + 12, 3, TFT_WHITE);
    canvas->fillRect(tcx - 1, bY + 2, 2, 9, TFT_RED);
    canvas->fillCircle(tcx, bY + 12, 2, TFT_RED);

    char buf[6];
    snprintf(buf, sizeof(buf), "%d", static_cast<int>(temperatureC_));
    canvas->setFont(nullptr);
    canvas->setTextSize(2);
    canvas->setTextDatum(top_left);
    canvas->setTextColor(TFT_BLACK);
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            if (dx == 0 && dy == 0) continue;
            canvas->drawString(buf, bX + 12 + dx, bY + dy);
        }
    }
    canvas->setTextColor(TFT_WHITE);
    canvas->drawString(buf, bX + 12, bY);

    const int hcx = bX + 56;
    canvas->fillTriangle(hcx, bY, hcx - 5, bY + 10, hcx + 5, bY + 10, TFT_WHITE);
    canvas->fillCircle(hcx, bY + 11, 4, TFT_WHITE);
    canvas->fillTriangle(hcx, bY + 1, hcx - 4, bY + 10, hcx + 4, bY + 10, TFT_CYAN);
    canvas->fillCircle(hcx, bY + 11, 3, TFT_CYAN);

    snprintf(buf, sizeof(buf), "%d", static_cast<int>(humidity_));
    canvas->setTextColor(TFT_BLACK);
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            if (dx == 0 && dy == 0) continue;
            canvas->drawString(buf, bX + 63 + dx, bY + dy);
        }
    }
    canvas->setTextColor(TFT_WHITE);
    canvas->drawString(buf, bX + 63, bY);
}
