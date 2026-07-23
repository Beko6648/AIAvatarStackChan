#include <Arduino.h>
#include <M5Unified.h>

#include "AIAvatarStackChan.h"
#include "UserApp.h"

static aiavatar::Config config;
static aiavatar::ResourceProvider resources;
static aiavatar::AIAvatar avatar;
static UserApp userApp;
static const char* kResourceProfileName = nullptr;

void setup() {
    Serial.begin(115200);
    uint32_t serialStart = millis();
    while (!Serial && millis() - serialStart < 3000) {
        delay(10);
    }
    delay(300);
    Serial.println();
    Serial.println("[Main] boot");

    auto m5cfg = M5.config();
    M5.begin(m5cfg);
    Serial.println("[Main] M5 initialized");
    Serial.printf("[Main] heap=%u psram=%u\n", ESP.getFreeHeap(), ESP.getFreePsram());

    if (resources.beginSD(GPIO_NUM_4)) {
        Serial.println("[Main] SD mounted");
        size_t profileCount = resources.scanProfiles();
        if (kResourceProfileName && kResourceProfileName[0]) {
            if (!resources.selectProfile(kResourceProfileName)) {
                Serial.printf("[Main] resource profile not found: %s\n", kResourceProfileName);
                while (true) delay(1000);
            }
        } else if (profileCount > 1) {
            Serial.println("[Main] multiple resource profiles found; select one in main.cpp");
            for (size_t i = 0; i < profileCount; ++i) {
                const aiavatar::ResourceProfile* profile = resources.profile(i);
                if (profile) {
                    Serial.printf("[Main]   %u: %s\n",
                                  static_cast<unsigned>(i), profile->name);
                }
            }
            resources.selectProfile(static_cast<size_t>(0));
        }
        resources.loadConfig(config);
    } else {
        Serial.println("[Main] SD not available; using built-in defaults");
    }

    if (config.wsHost[0] == '\0') {
        Serial.println("[Main] WS host is empty; check /config.json");
        while (true) delay(1000);
    }

    // Uncomment to show the neutral face and start PTT recording as early as possible.
    // Tradeoff: blink, mouth sprites, speaker, WebSocket, and other assets become ready gradually after boot.
    // config.fastStartup = true;

    // Remove `avatar.useStackChan()` if you want to run CoreS3 without Stack-chan hardware
    avatar.useStackChan();

    userApp.begin(avatar);

    if (!avatar.begin(config, resources)) {
        Serial.println("[Main] AIAvatar init failed");
        while (true) delay(1000);
    }
}

void loop() {
    avatar.update();
    userApp.update();
    delay(1);
}
