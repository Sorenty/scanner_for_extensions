#pragma once

#include "media_index.hpp"

#include <filesystem>

class Scanner {
public:
    explicit Scanner(std::filesystem::path root);

    void full_scan(MediaIndex& index);
    void incremental_scan(MediaIndex& index);

private:
    std::filesystem::path root_;
};