#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace config {
    class s_config_profile {
      public:
        explicit s_config_profile(const char* phase)
            : phase_(phase), start_(std::chrono::steady_clock::now()) {}
        ~s_config_profile() {
            if (std::getenv("ANTARES_CONFIG_PROFILE") == nullptr && std::getenv("ORION2RE_CONFIG_PROFILE") == nullptr) return;
            const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start_;
            std::fprintf(stderr, "config-profile %s %.3f ms\n", phase_, elapsed.count());
        }

      private:
        const char* phase_;
        std::chrono::steady_clock::time_point start_;
    };
} // namespace config