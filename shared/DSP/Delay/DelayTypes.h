#pragma once

struct DelaySettings
{
    float amount { 0.0f };
    float timeControl { 0.35f };
    float feedbackControl { 0.38f };
    int syncDivisionIndex { 0 };
    int algorithmIndex { 0 };
    int granularModeIndex { 0 };
    bool enabled { true };
    double bpm { 120.0 };
};
