#pragma once

struct ReverbSettings
{
    float amount { 0.0f };
    bool enabled { true };
    int algorithmIndex { 0 };

    float size { 0.52f };
    float decay { 0.48f };
    float damping { 0.46f };
    float preDelay { 0.08f };
    float modDepth { 0.24f };
    float modRate { 0.18f };
    float width { 0.86f };
    float cloudFeedback { 0.62f };
    float cloudDiffusion { 0.54f };
    // CLOUD only: how much of the tail is shifted up an octave and fed back in,
    // so it climbs as it decays. 0 = off.
    float shimmer { 0.0f };
};
