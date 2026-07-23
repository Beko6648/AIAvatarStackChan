#pragma once

#include "Config.h"

#include <SPI.h>
#include <cstddef>
#include <cstdint>

namespace aiavatar {

static constexpr size_t kMaxResourceProfiles = 8;
static constexpr size_t kResourceProfileNameMaxLen = 32;
static constexpr size_t kResourceProfilePathMaxLen = 96;

struct BuiltinAsset {
    const char* path;
    const uint8_t* data;
    size_t len;
};

struct ResourceProfile {
    char name[kResourceProfileNameMaxLen];
    char configPath[kResourceProfilePathMaxLen];
    char avatarDir[kResourceProfilePathMaxLen];
};

class ResourceProvider {
public:
    ResourceProvider();

    bool beginSD(uint8_t csPin, SPIClass& spi = SPI, uint32_t frequency = 25000000);
    void useSD(bool available = true) { sdAvailable_ = available; }
    bool sdAvailable() const { return sdAvailable_; }

    void setBuiltinAssets(const BuiltinAsset* assets, size_t count);

    size_t scanProfiles();
    size_t profileCount() const { return profileCount_; }
    const ResourceProfile* profile(size_t index) const;
    bool selectProfile(const char* name);
    bool selectProfile(size_t index);
    const char* activeProfileName() const;

    bool exists(const char* path) const;
    bool readBytes(const char* path, uint8_t** out, size_t* len) const;
    bool loadConfig(Config& config, const char* path = "/config.json") const;

private:
    bool sdAvailable_;
    const BuiltinAsset* builtinAssets_;
    size_t builtinAssetCount_;
    ResourceProfile profiles_[kMaxResourceProfiles];
    size_t profileCount_;
    int activeProfileIndex_;

    const BuiltinAsset* findBuiltinAsset(const char* path) const;
    bool addProfile(const char* name, const char* configPath, const char* avatarDir);
    bool sdDirectoryExists(const char* path) const;
    const char* resolvePath(const char* path, char* buffer, size_t bufferLen) const;
    bool readSDBytes(const char* path, uint8_t** out, size_t* len) const;
    bool readBuiltinBytes(const BuiltinAsset& asset, uint8_t** out, size_t* len) const;
};

}  // namespace aiavatar
