#include "media_index.hpp"

#include <algorithm>
#include <cctype>
#include <optional>

namespace fs = std::filesystem;

namespace {
std::string normalize_key(const fs::path& path) {
    return path.lexically_normal().generic_string();
}

std::string normalize_extension(std::string ext) {
    std::transform(
        ext.begin(),
        ext.end(),
        ext.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
    );
    return ext;
}

std::optional<std::string> category_for_extension(const std::string& ext) {
    const std::string e = normalize_extension(ext);

    if (e == ".mp3" || e == ".wav" || e == ".flac" || e == ".ogg" || e == ".aac" || e == ".m4a") {
        return std::string("audio");
    }
    if (e == ".mp4" || e == ".avi" || e == ".mkv" || e == ".mpg" || e == ".mpeg" || e == ".mov" || e == ".webm") {
        return std::string("video");
    }
    if (e == ".jpg" || e == ".jpeg" || e == ".png" || e == ".gif" || e == ".bmp" || e == ".webp" || e == ".tiff") {
        return std::string("images");
    }

    return std::nullopt;
}

std::string escape_json_string(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);

    for (char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default: {
                const unsigned char c = static_cast<unsigned char>(ch);
                if (c < 0x20) {
                    const char* hex = "0123456789ABCDEF";
                    out += "\\u00";
                    out += hex[(c >> 4) & 0x0F];
                    out += hex[c & 0x0F];
                } else {
                    out += ch;
                }
            }
        }
    }

    return out;
}

void append_array(std::string& out, const std::vector<std::string>& values) {
    out += "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += "\"";
        out += escape_json_string(values[i]);
        out += "\"";
    }
    out += "]";
}
} // namespace

void MediaIndex::clear() {
    categories_.clear();
    dir_cache_.clear();
}

void MediaIndex::add_file(const fs::path& path) {
    const std::optional<std::string> category = category_for_extension(path.extension().string());
    if (!category) {
        return;
    }

    categories_[*category][normalize_key(path)] = path.filename().string();
}

void MediaIndex::remove_file(const fs::path& path) {
    const std::optional<std::string> category = category_for_extension(path.extension().string());
    if (!category) {
        return;
    }

    const std::string key = normalize_key(path);

    auto cat_it = categories_.find(*category);
    if (cat_it == categories_.end()) {
        return;
    }

    cat_it->second.erase(key);
    if (cat_it->second.empty()) {
        categories_.erase(cat_it);
    }
}

std::string MediaIndex::to_json() const {
    auto collect = [&](const std::string& name) -> std::vector<std::string> {
        std::vector<std::string> result;
        auto it = categories_.find(name);
        if (it == categories_.end()) {
            return result;
        }

        result.reserve(it->second.size());
        for (const auto& [_, filename] : it->second) {
            result.push_back(filename);
        }

        std::sort(result.begin(), result.end());
        return result;
    };

    const std::vector<std::string> audio = collect("audio");
    const std::vector<std::string> video = collect("video");
    const std::vector<std::string> images = collect("images");

    std::string json;
    json += "{\n";
    json += "  \"audio\": ";
    append_array(json, audio);
    json += ",\n";
    json += "  \"video\": ";
    append_array(json, video);
    json += ",\n";
    json += "  \"images\": ";
    append_array(json, images);
    json += "\n";
    json += "}";

    return json;
}

MediaIndex::DirectoryCache& MediaIndex::cache_for(const fs::path& dir) {
    return dir_cache_[normalize_key(dir)];
}

MediaIndex::DirectoryCache* MediaIndex::get_cache(const fs::path& dir) {
    auto it = dir_cache_.find(normalize_key(dir));
    if (it == dir_cache_.end()) {
        return nullptr;
    }
    return &it->second;
}

const MediaIndex::DirectoryCache* MediaIndex::get_cache(const fs::path& dir) const {
    auto it = dir_cache_.find(normalize_key(dir));
    if (it == dir_cache_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<fs::path> MediaIndex::cached_directories() const {
    std::vector<fs::path> result;
    result.reserve(dir_cache_.size());

    for (const auto& [key, _] : dir_cache_) {
        result.emplace_back(key);
    }

    std::sort(result.begin(), result.end(), [](const fs::path& a, const fs::path& b) {
        return a.generic_string() < b.generic_string();
    });

    return result;
}

void MediaIndex::erase_cache(const fs::path& dir) {
    dir_cache_.erase(normalize_key(dir));
}