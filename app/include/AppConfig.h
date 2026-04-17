#pragma once

#include <string_view>

namespace moon::app
{
struct AppConfig
{
    static constexpr std::string_view appName = "Moon Audio Editor";
    static constexpr std::string_view companyName = "Moon Dev";
    static constexpr std::string_view backendUrl = "http://127.0.0.1:8000";
};
}
