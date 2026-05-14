#include "scanner.hpp"

#include <algorithm>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {
std::string normalize_key(const fs::path& path) {
    return path.lexically_normal().generic_string();
}

bool should_ignore_filename(const std::string& filename) {
    if (filename == ".DS_Store") {
        return true;
    }
    if (filename.size() >= 2 && filename[0] == '.' && filename[1] == '_') {
        return true;
    }
    return false;
}

bool is_same_or_descendant(const std::string& child, const std::string& base) {
    if (child == base) {
        return true;
    }

    if (base == "/") {
        return child.size() > 1 && child[0] == '/';
    }

    const std::string prefix = base + '/';
    return child.rfind(prefix, 0) == 0;
}

struct SubtreeSnapshot {
    std::unordered_set<std::string> dirs;
    std::unordered_map<std::string, std::unordered_set<std::string>> files_by_dir;
    std::unordered_map<std::string, fs::file_time_type> mtimes;
};

void collect_subtree_snapshot(const fs::path& root, SubtreeSnapshot& snapshot) {
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        return;
    }

    const std::string root_key = normalize_key(root);
    snapshot.dirs.insert(root_key);
    snapshot.mtimes[root_key] = fs::last_write_time(root, ec);

    fs::recursive_directory_iterator it(
        root,
        fs::directory_options::skip_permission_denied,
        ec
    );
    fs::recursive_directory_iterator end;

    while (it != end) {
        if (ec) {
            ec.clear();
            it.increment(ec);
            continue;
        }

        const fs::path current = it->path();
        const std::string current_key = normalize_key(current);

        std::error_code status_ec;
        if (it->is_directory(status_ec) && !status_ec) {
            snapshot.dirs.insert(current_key);
            snapshot.mtimes[current_key] = fs::last_write_time(current, ec);
        } else if (it->is_regular_file(status_ec) && !status_ec) {
            const std::string filename = current.filename().string();
            if (!should_ignore_filename(filename)) {
                const std::string parent_key = normalize_key(current.parent_path());
                snapshot.files_by_dir[parent_key].insert(current_key);
            }
        }

        it.increment(ec);
    }
}

void remove_subtree(const fs::path& root, MediaIndex& index) {
    const std::string base = normalize_key(root);
    const auto cached_dirs = index.cached_directories();

    std::vector<fs::path> to_remove;
    for (const fs::path& dir : cached_dirs) {
        const std::string key = normalize_key(dir);
        if (is_same_or_descendant(key, base)) {
            to_remove.push_back(dir);
        }
    }

    for (const fs::path& dir : to_remove) {
        const MediaIndex::DirectoryCache* cache = index.get_cache(dir);
        if (cache != nullptr) {
            for (const std::string& file_path : cache->files) {
                index.remove_file(fs::path(file_path));
            }
        }
        index.erase_cache(dir);
    }
}

void rescan_subtree(const fs::path& root, MediaIndex& index) {
    SubtreeSnapshot snapshot;
    collect_subtree_snapshot(root, snapshot);

    const std::string base = normalize_key(root);
    const auto cached_dirs = index.cached_directories();

    for (const fs::path& dir : cached_dirs) {
        const std::string key = normalize_key(dir);
        if (!is_same_or_descendant(key, base)) {
            continue;
        }

        if (snapshot.dirs.find(key) == snapshot.dirs.end()) {
            const MediaIndex::DirectoryCache* cache = index.get_cache(dir);
            if (cache != nullptr) {
                for (const std::string& file_path : cache->files) {
                    index.remove_file(fs::path(file_path));
                }
            }
            index.erase_cache(dir);
        }
    }

    for (const std::string& dir_key : snapshot.dirs) {
        fs::path dir_path(dir_key);
        MediaIndex::DirectoryCache& cache = index.cache_for(dir_path);

        const auto files_it = snapshot.files_by_dir.find(dir_key);
        const std::unordered_set<std::string> empty_files;
        const std::unordered_set<std::string>& new_files =
            (files_it != snapshot.files_by_dir.end()) ? files_it->second : empty_files;

        std::unordered_set<std::string> old_files = cache.files;

        for (const std::string& old_file : old_files) {
            if (new_files.find(old_file) == new_files.end()) {
                index.remove_file(fs::path(old_file));
            }
        }

        for (const std::string& new_file : new_files) {
            if (old_files.find(new_file) == old_files.end()) {
                index.add_file(fs::path(new_file));
            }
        }

        cache.files = new_files;

        const auto mtime_it = snapshot.mtimes.find(dir_key);
        if (mtime_it != snapshot.mtimes.end()) {
            cache.last_write_time = mtime_it->second;
        }
    }
}
} // namespace

Scanner::Scanner(fs::path root)
    : root_(std::move(root)) {}

void Scanner::full_scan(MediaIndex& index) {
    index.clear();

    std::error_code ec;
    if (!fs::exists(root_, ec) || !fs::is_directory(root_, ec)) {
        return;
    }

    MediaIndex::DirectoryCache& root_cache = index.cache_for(root_);
    root_cache.files.clear();
    root_cache.last_write_time = fs::last_write_time(root_, ec);

    fs::recursive_directory_iterator it(
        root_,
        fs::directory_options::skip_permission_denied,
        ec
    );
    fs::recursive_directory_iterator end;

    while (it != end) {
        if (ec) {
            ec.clear();
            it.increment(ec);
            continue;
        }

        const fs::path current = it->path();

        std::error_code status_ec;
        if (it->is_directory(status_ec) && !status_ec) {
            MediaIndex::DirectoryCache& cache = index.cache_for(current);
            cache.files.clear();
            cache.last_write_time = fs::last_write_time(current, ec);
        } else if (it->is_regular_file(status_ec) && !status_ec) {
            const std::string filename = current.filename().string();
            if (!should_ignore_filename(filename)) {
                index.add_file(current);
                index.cache_for(current.parent_path()).files.insert(normalize_key(current));
            }
        }

        it.increment(ec);
    }
}

void Scanner::incremental_scan(MediaIndex& index) {
    std::error_code ec;
    if (!fs::exists(root_, ec) || !fs::is_directory(root_, ec)) {
        index.clear();
        return;
    }

    if (index.cached_directories().empty()) {
        full_scan(index);
        return;
    }

    const auto snapshot_dirs = index.cached_directories();

    for (const fs::path& dir : snapshot_dirs) {
        if (!fs::exists(dir, ec)) {
            remove_subtree(dir, index);
            continue;
        }

        MediaIndex::DirectoryCache* cache = index.get_cache(dir);
        if (cache == nullptr) {
            continue;
        }

        const fs::file_time_type current_mtime = fs::last_write_time(dir, ec);
        if (ec) {
            ec.clear();
            continue;
        }

        if (current_mtime != cache->last_write_time) {
            rescan_subtree(dir, index);
        }
    }
}