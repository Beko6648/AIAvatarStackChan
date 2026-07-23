#include "ResourceProvider.h"

#include <Arduino.h>
#include <SD.h>
#include <cstring>
#include <cstdlib>
#include <pgmspace.h>

namespace aiavatar {

ResourceProvider::ResourceProvider()
    : sdAvailable_(false),
      builtinAssets_(nullptr),
      builtinAssetCount_(0),
      profileCount_(0),
      activeProfileIndex_(-1) {}

bool ResourceProvider::beginSD(uint8_t csPin, SPIClass& spi, uint32_t frequency) {
    sdAvailable_ = SD.begin(csPin, spi, frequency);
    return sdAvailable_;
}

void ResourceProvider::setBuiltinAssets(const BuiltinAsset* assets, size_t count) {
    builtinAssets_ = assets;
    builtinAssetCount_ = assets ? count : 0;
}

size_t ResourceProvider::scanProfiles() {
    profileCount_ = 0;
    activeProfileIndex_ = -1;

    if (!sdAvailable_) return 0;

    if (SD.exists("/config.json") && sdDirectoryExists("/avatar")) {
        addProfile("default", "/config.json", "/avatar");
    }

    File root = SD.open("/");
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return profileCount_;
    }

    while (profileCount_ < kMaxResourceProfiles) {
        File entry = root.openNextFile();
        if (!entry) break;
        if (entry.isDirectory()) {
            entry.close();
            continue;
        }

        const char* fullName = entry.name();
        const char* fileName = strrchr(fullName, '/');
        fileName = fileName ? fileName + 1 : fullName;
        if (!fileName || !fileName[0] || strcmp(fileName, "config.json") == 0) {
            entry.close();
            continue;
        }

        size_t len = strlen(fileName);
        static constexpr const char* kJsonSuffix = ".json";
        static constexpr size_t kJsonSuffixLen = 5;
        if (len <= kJsonSuffixLen ||
            strcmp(fileName + len - kJsonSuffixLen, kJsonSuffix) != 0 ||
            len - kJsonSuffixLen >= kResourceProfileNameMaxLen) {
            entry.close();
            continue;
        }

        char name[kResourceProfileNameMaxLen];
        memcpy(name, fileName, len - kJsonSuffixLen);
        name[len - kJsonSuffixLen] = '\0';

        char configPath[kResourceProfilePathMaxLen];
        char avatarDir[kResourceProfilePathMaxLen];
        snprintf(configPath, sizeof(configPath), "/%s.json", name);
        snprintf(avatarDir, sizeof(avatarDir), "/%s", name);
        if (sdDirectoryExists(avatarDir)) {
            addProfile(name, configPath, avatarDir);
        }
        entry.close();
    }
    root.close();

    if (profileCount_ == 1) activeProfileIndex_ = 0;

    Serial.printf("[Resources] profiles=%u", static_cast<unsigned>(profileCount_));
    if (activeProfileIndex_ >= 0) {
        Serial.printf(" active=%s", profiles_[activeProfileIndex_].name);
    }
    Serial.println();
    for (size_t i = 0; i < profileCount_; ++i) {
        Serial.printf("[Resources] profile[%u]=%s config=%s avatar=%s\n",
                      static_cast<unsigned>(i),
                      profiles_[i].name,
                      profiles_[i].configPath,
                      profiles_[i].avatarDir);
    }

    return profileCount_;
}

const ResourceProfile* ResourceProvider::profile(size_t index) const {
    if (index >= profileCount_) return nullptr;
    return &profiles_[index];
}

bool ResourceProvider::selectProfile(const char* name) {
    if (!name || !name[0]) return false;
    for (size_t i = 0; i < profileCount_; ++i) {
        if (strcmp(profiles_[i].name, name) == 0) {
            activeProfileIndex_ = static_cast<int>(i);
            Serial.printf("[Resources] active profile=%s\n", profiles_[i].name);
            return true;
        }
    }
    Serial.printf("[Resources] profile not found: %s\n", name);
    return false;
}

bool ResourceProvider::selectProfile(size_t index) {
    if (index >= profileCount_) return false;
    activeProfileIndex_ = static_cast<int>(index);
    Serial.printf("[Resources] active profile=%s\n", profiles_[index].name);
    return true;
}

const char* ResourceProvider::activeProfileName() const {
    if (activeProfileIndex_ < 0 ||
        static_cast<size_t>(activeProfileIndex_) >= profileCount_) {
        return nullptr;
    }
    return profiles_[activeProfileIndex_].name;
}

bool ResourceProvider::exists(const char* path) const {
    if (!path || !path[0]) return false;
    if (profileCount_ > 1 && activeProfileIndex_ < 0 &&
        (strcmp(path, "/config.json") == 0 ||
         strncmp(path, "/avatar/", 8) == 0)) {
        return false;
    }
    char resolved[kResourceProfilePathMaxLen];
    const char* sdPath = resolvePath(path, resolved, sizeof(resolved));
    if (sdAvailable_ && SD.exists(sdPath)) return true;
    return findBuiltinAsset(path) != nullptr;
}

bool ResourceProvider::readBytes(const char* path, uint8_t** out, size_t* len) const {
    if (!out || !len) return false;
    *out = nullptr;
    *len = 0;
    if (!path || !path[0]) return false;
    if (profileCount_ > 1 && activeProfileIndex_ < 0 &&
        strncmp(path, "/avatar/", 8) == 0) {
        Serial.printf("[Resources] profile selection required before reading %s\n", path);
        return false;
    }

    char resolved[kResourceProfilePathMaxLen];
    const char* sdPath = resolvePath(path, resolved, sizeof(resolved));
    if (sdAvailable_ && readSDBytes(sdPath, out, len)) {
        return true;
    }

    const BuiltinAsset* asset = findBuiltinAsset(path);
    if (asset && readBuiltinBytes(*asset, out, len)) {
        return true;
    }

    return false;
}

bool ResourceProvider::loadConfig(Config& config, const char* path) const {
    if (!path || !path[0]) return false;
    if (profileCount_ > 1 && activeProfileIndex_ < 0 &&
        strcmp(path, "/config.json") == 0) {
        Serial.println("[Resources] profile selection required before loading /config.json");
        return false;
    }

    char resolved[kResourceProfilePathMaxLen];
    const char* sdPath = resolvePath(path, resolved, sizeof(resolved));
    if (sdAvailable_ && SD.exists(sdPath)) {
        File file = SD.open(sdPath, FILE_READ);
        if (!file) {
            Serial.printf("[Resources] failed to open %s\n", sdPath);
            return false;
        }
        bool ok = config.loadFromJson(file);
        file.close();
        return ok;
    }

    const BuiltinAsset* asset = findBuiltinAsset(path);
    if (asset) {
        uint8_t* data = nullptr;
        size_t len = 0;
        if (!readBuiltinBytes(*asset, &data, &len)) return false;
        bool ok = config.loadFromJsonBytes(data, len);
        free(data);
        return ok;
    }

    Serial.printf("[Resources] %s not found, using current config\n", sdPath);
    return false;
}

const BuiltinAsset* ResourceProvider::findBuiltinAsset(const char* path) const {
    if (!path || !builtinAssets_) return nullptr;
    for (size_t i = 0; i < builtinAssetCount_; ++i) {
        const BuiltinAsset& asset = builtinAssets_[i];
        if (asset.path && strcmp(asset.path, path) == 0 && asset.data && asset.len > 0) {
            return &asset;
        }
    }
    return nullptr;
}

bool ResourceProvider::addProfile(const char* name, const char* configPath,
                                  const char* avatarDir) {
    if (!name || !name[0] || !configPath || !avatarDir ||
        profileCount_ >= kMaxResourceProfiles) {
        return false;
    }
    for (size_t i = 0; i < profileCount_; ++i) {
        if (strcmp(profiles_[i].name, name) == 0) return false;
    }

    ResourceProfile& profile = profiles_[profileCount_];
    snprintf(profile.name, sizeof(profile.name), "%s", name);
    snprintf(profile.configPath, sizeof(profile.configPath), "%s", configPath);
    snprintf(profile.avatarDir, sizeof(profile.avatarDir), "%s", avatarDir);
    ++profileCount_;
    return true;
}

bool ResourceProvider::sdDirectoryExists(const char* path) const {
    if (!sdAvailable_ || !path || !path[0]) return false;
    File dir = SD.open(path);
    if (!dir) return false;
    bool ok = dir.isDirectory();
    dir.close();
    return ok;
}

const char* ResourceProvider::resolvePath(const char* path, char* buffer,
                                          size_t bufferLen) const {
    if (!path) return "";
    if (activeProfileIndex_ < 0 ||
        static_cast<size_t>(activeProfileIndex_) >= profileCount_) {
        return path;
    }

    const ResourceProfile& profile = profiles_[activeProfileIndex_];
    if (strcmp(path, "/config.json") == 0) {
        snprintf(buffer, bufferLen, "%s", profile.configPath);
        return buffer;
    }

    static constexpr const char* kAvatarPrefix = "/avatar/";
    static constexpr size_t kAvatarPrefixLen = 8;
    if (strncmp(path, kAvatarPrefix, kAvatarPrefixLen) == 0) {
        snprintf(buffer, bufferLen, "%s/%s", profile.avatarDir,
                 path + kAvatarPrefixLen);
        return buffer;
    }

    return path;
}

bool ResourceProvider::readSDBytes(const char* path, uint8_t** out, size_t* len) const {
    File file = SD.open(path, FILE_READ);
    if (!file) return false;

    size_t fileLen = file.size();
    uint8_t* data = static_cast<uint8_t*>(ps_malloc(fileLen));
    if (!data) data = static_cast<uint8_t*>(malloc(fileLen));
    if (!data) {
        Serial.printf("[Resources] buffer allocation failed: %s (%u bytes)\n",
                      path, static_cast<unsigned>(fileLen));
        file.close();
        return false;
    }

    size_t readLen = file.read(data, fileLen);
    file.close();
    if (readLen != fileLen) {
        Serial.printf("[Resources] read failed: %s\n", path);
        free(data);
        return false;
    }

    *out = data;
    *len = fileLen;
    return true;
}

bool ResourceProvider::readBuiltinBytes(const BuiltinAsset& asset, uint8_t** out, size_t* len) const {
    uint8_t* data = static_cast<uint8_t*>(ps_malloc(asset.len));
    if (!data) data = static_cast<uint8_t*>(malloc(asset.len));
    if (!data) {
        Serial.printf("[Resources] builtin buffer allocation failed: %s (%u bytes)\n",
                      asset.path ? asset.path : "", static_cast<unsigned>(asset.len));
        return false;
    }

    memcpy_P(data, asset.data, asset.len);
    *out = data;
    *len = asset.len;
    return true;
}

}  // namespace aiavatar
