#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <mutex>

namespace px3::synth
{
enum class ModulationScope { global, voice };
enum class ModulationPolarity { native, unipolar, bipolar };
enum class ModulationCurve { linear, square, squareRoot };

struct ModulationSourceDescriptor
{
    ModulationScope scope { ModulationScope::global };
    bool bipolar { false };
};

struct ModulationDestinationDescriptor
{
    ModulationScope scope { ModulationScope::global };
    int controllingSource { -1 };
    bool enabled { true };
};

struct ModulationRoute
{
    int slot { -1 };
    int source { -1 };
    int destination { -1 };
    float depth { 0.0f };
    ModulationPolarity polarity { ModulationPolarity::native };
    ModulationCurve curve { ModulationCurve::linear };
};

class CompiledModulationGraph final
{
public:
    static constexpr int sourceCapacity = 32;
    static constexpr int destinationCapacity = 512;
    static constexpr int routeCapacity = 192;

    bool compile(std::span<const ModulationSourceDescriptor> sourceDescriptors,
                 std::span<const ModulationDestinationDescriptor> destinationDescriptors,
                 std::span<const ModulationRoute> routeDescriptors,
                 std::string& error)
    {
        if (sourceDescriptors.size() > sourceCapacity || destinationDescriptors.size() > destinationCapacity
            || routeDescriptors.size() > routeCapacity)
        {
            error = "Modulation graph capacity exceeded.";
            return false;
        }
        CompiledModulationGraph candidate;
        candidate.sourceCount = static_cast<int>(sourceDescriptors.size());
        candidate.destinationCount = static_cast<int>(destinationDescriptors.size());
        std::array<bool, routeCapacity> usedSlots {};
        std::array<std::array<bool, sourceCapacity>, sourceCapacity> edges {};
        std::array<int, sourceCapacity> incoming {};
        for (std::size_t index = 0; index < sourceDescriptors.size(); ++index)
        {
            candidate.sources[index] = sourceDescriptors[index];
        }
        for (const auto& route : routeDescriptors)
        {
            if (route.slot < 0 || route.slot >= routeCapacity || usedSlots[static_cast<std::size_t>(route.slot)]
                || route.source < 0 || route.source >= candidate.sourceCount
                || route.destination < 0 || route.destination >= candidate.destinationCount
                || ! std::isfinite(route.depth) || route.depth < -1.0f || route.depth > 1.0f
                || route.polarity < ModulationPolarity::native || route.polarity > ModulationPolarity::bipolar
                || route.curve < ModulationCurve::linear || route.curve > ModulationCurve::squareRoot)
            {
                error = "Invalid modulation route or duplicate slot.";
                return false;
            }
            const auto& source = sourceDescriptors[static_cast<std::size_t>(route.source)];
            const auto& destination = destinationDescriptors[static_cast<std::size_t>(route.destination)];
            if (! destination.enabled || (source.scope == ModulationScope::voice
                                          && destination.scope == ModulationScope::global))
            {
                error = "Modulation route has an unavailable destination or incompatible scope.";
                return false;
            }
            const auto dependency = destination.controllingSource;
            if (dependency < -1 || dependency >= candidate.sourceCount)
            {
                error = "Unknown modulation source dependency.";
                return false;
            }
            if (dependency >= 0)
            {
                auto& edge = edges[static_cast<std::size_t>(route.source)][static_cast<std::size_t>(dependency)];
                if (! edge)
                {
                    edge = true;
                    ++incoming[static_cast<std::size_t>(dependency)];
                }
            }
            usedSlots[static_cast<std::size_t>(route.slot)] = true;
            candidate.routes[static_cast<std::size_t>(candidate.routeCount++)] = route;
        }
        std::array<bool, sourceCapacity> visited {};
        for (int position = 0; position < candidate.sourceCount; ++position)
        {
            auto next = -1;
            for (int source = 0; source < candidate.sourceCount; ++source)
            {
                if (! visited[static_cast<std::size_t>(source)] && incoming[static_cast<std::size_t>(source)] == 0)
                {
                    next = source;
                    break;
                }
            }
            if (next < 0)
            {
                error = "Modulation graph contains a source cycle.";
                return false;
            }
            visited[static_cast<std::size_t>(next)] = true;
            candidate.sourceOrder[static_cast<std::size_t>(position)] = next;
            for (int dependent = 0; dependent < candidate.sourceCount; ++dependent)
            {
                if (edges[static_cast<std::size_t>(next)][static_cast<std::size_t>(dependent)])
                {
                    --incoming[static_cast<std::size_t>(dependent)];
                }
            }
        }
        *this = candidate;
        error.clear();
        return true;
    }

    float deltaFor(int destination, float base, std::span<const float> signals,
                   std::span<const float> automatedDepths = {},
                   std::span<const bool> enabledSources = {}) const noexcept
    {
        auto delta = 0.0f;
        for (int index = 0; index < routeCount; ++index)
        {
            const auto& route = routes[static_cast<std::size_t>(index)];
            if (route.destination != destination || static_cast<std::size_t>(route.source) >= signals.size()) { continue; }
            if (static_cast<std::size_t>(route.source) < enabledSources.size()
                && ! enabledSources[static_cast<std::size_t>(route.source)]) { continue; }
            const auto& source = sources[static_cast<std::size_t>(route.source)];
            auto signal = signals[static_cast<std::size_t>(route.source)];
            if (! std::isfinite(signal)) { continue; }
            signal = source.bipolar ? std::clamp(signal, -1.0f, 1.0f) : std::clamp(signal, 0.0f, 1.0f);
            auto bipolar = source.bipolar;
            if (route.polarity == ModulationPolarity::bipolar && ! bipolar)
            {
                signal = signal * 2.0f - 1.0f;
                bipolar = true;
            }
            else if (route.polarity == ModulationPolarity::unipolar && bipolar)
            {
                signal = (signal + 1.0f) * 0.5f;
                bipolar = false;
            }
            const auto magnitude = std::abs(signal);
            if (route.curve == ModulationCurve::square) { signal = std::copysign(magnitude * magnitude, signal); }
            if (route.curve == ModulationCurve::squareRoot) { signal = std::copysign(std::sqrt(magnitude), signal); }
            const auto depth = static_cast<std::size_t>(route.slot) < automatedDepths.size()
                                   ? automatedDepths[static_cast<std::size_t>(route.slot)] : route.depth;
            if (! std::isfinite(depth)) { continue; }
            const auto swing = bipolar ? 0.5f : (depth >= 0.0f ? 1.0f - base : base);
            delta += std::clamp(depth, -1.0f, 1.0f) * swing * signal;
        }
        return delta;
    }

    std::span<const int> evaluationOrder() const noexcept
    {
        return { sourceOrder.data(), static_cast<std::size_t>(sourceCount) };
    }
    int getRouteCount() const noexcept { return routeCount; }
    bool hasDestination(int destination, int minimumSlot = 0) const noexcept
    {
        for (int index = 0; index < routeCount; ++index)
        {
            const auto& route = routes[static_cast<std::size_t>(index)];
            if (route.destination == destination && route.slot >= minimumSlot) { return true; }
        }
        return false;
    }
    std::array<float, routeCapacity> depths() const noexcept
    {
        std::array<float, routeCapacity> result {};
        for (int index = 0; index < routeCount; ++index)
        {
            const auto& route = routes[static_cast<std::size_t>(index)];
            result[static_cast<std::size_t>(route.slot)] = route.depth;
        }
        return result;
    }

private:
    std::array<ModulationSourceDescriptor, sourceCapacity> sources {};
    std::array<ModulationRoute, routeCapacity> routes {};
    std::array<int, sourceCapacity> sourceOrder {};
    int sourceCount { 0 };
    int destinationCount { 0 };
    int routeCount { 0 };
};

class ModulationGraphPublication final
{
public:
    class ReadHandle final
    {
    public:
        ReadHandle(const CompiledModulationGraph* graphIn, std::atomic<int>* pinsIn) noexcept
            : graph(graphIn), pins(pinsIn) {}
        ~ReadHandle() { if (pins != nullptr) { pins->fetch_sub(1, std::memory_order_release); } }
        ReadHandle(const ReadHandle&) = delete;
        ReadHandle& operator=(const ReadHandle&) = delete;
        const CompiledModulationGraph* operator->() const noexcept { return graph; }
        explicit operator bool() const noexcept { return graph != nullptr; }
    private:
        const CompiledModulationGraph* graph;
        std::atomic<int>* pins;
    };

    ReadHandle read() const noexcept
    {
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const auto index = active.load(std::memory_order_acquire);
            auto& slot = slots[static_cast<std::size_t>(index)];
            auto count = slot.readers.load(std::memory_order_relaxed);
            if (count >= 0 && slot.readers.compare_exchange_strong(count, count + 1,
                                                                  std::memory_order_acquire,
                                                                  std::memory_order_relaxed))
            {
                return { &slot.plan, &slot.readers };
            }
        }
        return { nullptr, nullptr };
    }

    bool publish(const CompiledModulationGraph& graph)
    {
        const std::lock_guard<std::mutex> lock(writerMutex);
        const auto current = active.load(std::memory_order_acquire);
        for (std::size_t index = 0; index < slots.size(); ++index)
        {
            if (static_cast<int>(index) == current) { continue; }
            auto& slot = slots[index];
            auto expected = 0;
            if (! slot.readers.compare_exchange_strong(expected, -1, std::memory_order_acquire)) { continue; }
            slot.plan = graph;
            slot.readers.store(0, std::memory_order_release);
            active.store(static_cast<int>(index), std::memory_order_release);
            return true;
        }
        return false;
    }

private:
    struct Slot
    {
        CompiledModulationGraph plan;
        std::atomic<int> readers { 0 };
    };
    mutable std::array<Slot, 3> slots;
    std::atomic<int> active { 0 };
    std::mutex writerMutex;
};
}