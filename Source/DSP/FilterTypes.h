#pragma once

#include <array>

inline constexpr int kFilterInstanceCount = 2;

struct FilterSettings
{
    float cutoffHz { 10000.0f };
    float resonanceQ { 0.8f };
    int modeIndex { 0 };
};
