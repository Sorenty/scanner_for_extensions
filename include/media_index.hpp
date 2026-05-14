#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class MediaIndex {
public:
    struct DirectoryCache {
        std::filesystem::file_time_type last_write_time{};
        std::unordered_set<std::string> files;
    };

    void clear();

    void add_file(const std::filesystem::path& path);
    void remove_file(const std::filesystem::path& path);

    std::string to_json() const;

    DirectoryCache& cache_for(const std::filesystem::path& dir);
    DirectoryCache* get_cache(const std::filesystem::path& dir);
    const DirectoryCache* get_cache(const std::filesystem::path& dir) const;

    std::vector<std::filesystem::path> cached_directories() const;
    void erase_cache(const std::filesystem::path& dir);

private:
    using CategoryFiles = std::unordered_map<std::string, std::string>;
    using CategoryMap = std::unordered_map<std::string, CategoryFiles>;

    CategoryMap categories_;
    std::unordered_map<std::string, DirectoryCache> dir_cache_;
};