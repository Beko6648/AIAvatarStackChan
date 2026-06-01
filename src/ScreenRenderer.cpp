#include "ScreenRenderer.h"
#include "ResourceProvider.h"

#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace aiavatar {

ScreenRenderer::ScreenRenderer()
    : canvas_(nullptr),
      currentBase_(nullptr),
      currentOverlay_(nullptr),
      overlayCb_(nullptr),
      resources_(nullptr),
      imageFitMode_(ImageFitMode::Contain),
      dirty_(true),
      rotation_(1),
      width_(320),
      height_(240) {}

bool ScreenRenderer::begin(uint8_t rotation, uint8_t brightness) {
    M5.Display.setRotation(rotation);
    M5.Display.setBrightness(brightness);
    width_ = M5.Display.width();
    height_ = M5.Display.height();
    rotation_ = rotation;
    M5.Display.fillScreen(TFT_BLACK);

    canvas_ = new LGFX_Sprite(&M5.Display);
    if (!canvas_) return false;
    canvas_->setColorDepth(16);
    canvas_->setPsram(true);
    if (!canvas_->createSprite(width_, height_)) {
        Serial.println("[Display] canvas allocation failed");
        delete canvas_;
        canvas_ = nullptr;
        return false;
    }

    dirty_ = true;
    Serial.printf("[Display] initialized %dx%d\n", width_, height_);
    return true;
}

bool ScreenRenderer::setRotation(uint8_t rotation) {
    rotation &= 3;
    if (rotation_ == rotation) return true;

    int oldW = width_;
    int oldH = height_;
    M5.Display.setRotation(rotation);
    int newW = M5.Display.width();
    int newH = M5.Display.height();
    if (newW != oldW || newH != oldH) {
        M5.Display.setRotation(rotation_);
        Serial.printf("[Display] runtime rotation rejected: %dx%d -> %dx%d\n",
                      oldW, oldH, newW, newH);
        return false;
    }

    rotation_ = rotation;
    M5.Display.fillScreen(TFT_BLACK);
    dirty_ = true;
    Serial.printf("[Display] rotation=%u\n", rotation_);
    return true;
}

LGFX_Sprite* ScreenRenderer::loadSprite(const char* path, int w, int h, uint16_t bgColor) {
    if (!path || !path[0]) return nullptr;

    if (!resources_) {
        Serial.printf("[Display] no resource provider: %s\n", path);
        return nullptr;
    }

    uint8_t* png = nullptr;
    size_t len = 0;
    if (!resources_->readBytes(path, &png, &len)) {
        Serial.printf("[Display] open failed: %s\n", path);
        return nullptr;
    }

    auto* sprite = new LGFX_Sprite(&M5.Display);
    if (!sprite) {
        free(png);
        return nullptr;
    }
    sprite->setColorDepth(16);
    sprite->setPsram(true);
    if (!sprite->createSprite(w, h)) {
        Serial.printf("[Display] sprite allocation failed: %s\n", path);
        delete sprite;
        free(png);
        return nullptr;
    }

    int imgW = w;
    int imgH = h;
    if (len >= 24) {
        imgW = (png[16] << 24) | (png[17] << 16) | (png[18] << 8) | png[19];
        imgH = (png[20] << 24) | (png[21] << 16) | (png[22] << 8) | png[23];
    }
    sprite->fillSprite(bgColor);
    if (imageFitMode_ == ImageFitMode::Cover) {
        float scale = 1.0f;
        int offsetX = 0;
        int offsetY = 0;
        if (imgW > 0 && imgH > 0 && w > 0 && h > 0) {
            scale = std::max(static_cast<float>(w) / imgW,
                             static_cast<float>(h) / imgH);
            int scaledW = static_cast<int>(ceilf(imgW * scale));
            int scaledH = static_cast<int>(ceilf(imgH * scale));
            offsetX = (scaledW - w) / 2;
            offsetY = (scaledH - h) / 2;
        }
        sprite->drawPng(png, len, 0, 0, w, h, offsetX, offsetY, scale, scale);
    } else {
        int offsetX = (w - imgW) / 2;
        int offsetY = (h - imgH) / 2;
        if (offsetX < 0) offsetX = 0;
        if (offsetY < 0) offsetY = 0;
        sprite->drawPng(png, len, offsetX, offsetY);
    }
    free(png);

    Serial.printf("[Display] loaded %s\n", path);
    return sprite;
}

void ScreenRenderer::setBase(LGFX_Sprite* sprite) {
    if (currentBase_ == sprite) return;
    currentBase_ = sprite;
    dirty_ = true;
}

void ScreenRenderer::setOverlay(LGFX_Sprite* sprite) {
    if (currentOverlay_ == sprite) return;
    currentOverlay_ = sprite;
    dirty_ = true;
}

void ScreenRenderer::update() {
    if (!dirty_ || !canvas_) return;
    dirty_ = false;

    if (currentBase_) {
        currentBase_->pushSprite(canvas_, 0, 0);
    } else {
        canvas_->fillSprite(TFT_BLACK);
    }
    if (currentOverlay_) {
        currentOverlay_->pushSprite(canvas_, 0, 0, kTransparentColor);
    }
    if (overlayCb_) overlayCb_(canvas_);

    canvas_->pushSprite(0, 0);
}

}  // namespace aiavatar
