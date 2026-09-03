#include "ProductRegistry.h"

namespace px3::update
{

const char* const ProductRegistry::kSynthProductId = "px3-synth";

ProductRegistry& ProductRegistry::getInstance()
{
    static ProductRegistry instance;
    return instance;
}

void ProductRegistry::registerProduct(Registration registration)
{
    if (registration.productId.isEmpty()) { return; }

    const juce::ScopedLock guard(lock);

    for (auto& existing : registrations)
    {
        if (existing.productId == registration.productId)
        {
            existing = std::move(registration);
            return;
        }
    }

    registrations.push_back(std::move(registration));
}

void ProductRegistry::clear()
{
    const juce::ScopedLock guard(lock);
    registrations.clear();
}

std::vector<juce::String> ProductRegistry::productIds() const
{
    const juce::ScopedLock guard(lock);

    std::vector<juce::String> ids;
    ids.reserve(registrations.size());
    for (const auto& entry : registrations) { ids.push_back(entry.productId); }
    return ids;
}

bool ProductRegistry::isRegistered(const juce::String& productId) const
{
    const juce::ScopedLock guard(lock);

    for (const auto& entry : registrations)
    {
        if (entry.productId == productId) { return true; }
    }
    return false;
}

ProductInfo ProductRegistry::lookup(const juce::String& productId) const
{
    const juce::ScopedLock guard(lock);

    for (const auto& entry : registrations)
    {
        if (entry.productId != productId) { continue; }

        ProductInfo info;
        info.productId = entry.productId;
        info.displayName = entry.displayName;
        info.installedVersion = SemanticVersion::parse(
            entry.versionProvider != nullptr ? entry.versionProvider() : juce::String());
        return info;
    }

    return {};
}

std::vector<ProductInfo> ProductRegistry::installedProducts() const
{
    std::vector<ProductInfo> products;
    for (const auto& id : productIds())
    {
        auto info = lookup(id);
        if (info.isValid()) { products.push_back(std::move(info)); }
    }
    return products;
}

} // namespace px3::update
