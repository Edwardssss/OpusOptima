#pragma once

#include "RepoRoot.hpp"
#include <filesystem>
#include <gtest/gtest.h>
#include <random>
#include <stdexcept>

namespace opusoptima::test {
class ScopedTestDirectory {
    public:
    ScopedTestDirectory() {
        const auto base = opusoptima::test::RepoRoot(__FILE__) / "build/test-output/integration";
        std::filesystem::create_directories(base);
        std::random_device random;
        for (int attempt = 0; attempt < 64; ++attempt) {
            path_ = base / (std::to_string(random()) + "-" + std::to_string(random()));
            if (std::filesystem::create_directory(path_)) return;
        }
        throw std::runtime_error("cannot allocate exclusive test output directory");
    }
    ~ScopedTestDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        if (error) ADD_FAILURE() << "cannot clean test output directory: " << error.message();
    }
    ScopedTestDirectory(const ScopedTestDirectory&) = delete;
    ScopedTestDirectory& operator=(const ScopedTestDirectory&) = delete;
    const std::filesystem::path& Path() const {
        return path_;
    }

    private:
    std::filesystem::path path_;
};
} // namespace opusoptima::test
