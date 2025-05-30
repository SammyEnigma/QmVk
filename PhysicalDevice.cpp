// SPDX-License-Identifier: MIT
/*
   QmVk - simple Vulkan library created for QMPlay2
   Copyright (C) 2020-2025 Błażej Szczygieł
*/

#include "PhysicalDevice.hpp"
#include "AbstractInstance.hpp"
#include "MemoryPropertyFlags.hpp"
#include "Device.hpp"

#include <cmath>

namespace QmVk {

PhysicalDevice::PhysicalDevice(
        const shared_ptr<AbstractInstance> &instance,
        vk::PhysicalDevice physicalDevice)
    : vk::PhysicalDevice(physicalDevice)
    , m_instance(instance)
    , m_dld(m_instance->dld())
{}
PhysicalDevice::~PhysicalDevice()
{}

void PhysicalDevice::init()
{
    const auto deviceExtensionProperties = [this] {
        vector<vk::ExtensionProperties> deviceExtensionProperties;
        uint32_t propertyCount = 0;
        auto result = enumerateDeviceExtensionProperties(nullptr, &propertyCount, static_cast<vk::ExtensionProperties *>(nullptr), dld());
        if (result == vk::Result::eSuccess && propertyCount > 0)
        {
            deviceExtensionProperties.resize(propertyCount);
            result = enumerateDeviceExtensionProperties(nullptr, &propertyCount, deviceExtensionProperties.data(), dld());
            if (result != vk::Result::eSuccess && result != vk::Result::eIncomplete)
                propertyCount = 0;
            if (propertyCount != deviceExtensionProperties.size())
                deviceExtensionProperties.resize(propertyCount);
        }
        return deviceExtensionProperties;
    }();

    for (auto &&extensionProperty : deviceExtensionProperties)
        m_extensionProperties.insert(extensionProperty.extensionName);

    const bool useGetProperties2KHR = m_instance->checkExtension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    if (!m_instance->isVk10() || useGetProperties2KHR)
    {
        if (useGetProperties2KHR)
        {
            tie(m_properties, m_pciBusInfo) = getProperties2KHR<
                decltype(m_properties),
                decltype(m_pciBusInfo)
            >(dld()).get<
                decltype(m_properties),
                decltype(m_pciBusInfo)
            >();
        }
        else
        {
            tie(m_properties, m_pciBusInfo) = getProperties2<
                decltype(m_properties),
                decltype(m_pciBusInfo)
            >(dld()).get<
                decltype(m_properties),
                decltype(m_pciBusInfo)
            >();
        }

        m_hasMemoryBudget = checkExtension(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
        m_hasPciBusInfo = checkExtension(VK_EXT_PCI_BUS_INFO_EXTENSION_NAME);
    }
    else
    {
        m_properties = getProperties(dld());
    }

    vk::DeviceSize deviceLocalAndHostVisibleSize = 0;
    vk::DeviceSize deviceLocalSize = 0;
    for (auto &&heapInfo : getMemoryHeapsInfo())
    {
        if (!heapInfo.deviceLocal)
            continue;

        if (heapInfo.hostVisible)
        {
            if (deviceLocalAndHostVisibleSize == 0)
                deviceLocalAndHostVisibleSize = heapInfo.size;
        }
        else
        {
            if (deviceLocalSize == 0)
                deviceLocalSize = heapInfo.size;
        }
    }
    m_hasFullHostVisibleDeviceLocal = (deviceLocalAndHostVisibleSize >= deviceLocalSize);

    const uint32_t localWorkgroupSizeSqr = pow(2.0, floor(log2(sqrt(limits().maxComputeWorkGroupInvocations))));
    m_localWorkgroupSize = vk::Extent2D(
        min(localWorkgroupSizeSqr, limits().maxComputeWorkGroupSize[0]),
        min(localWorkgroupSizeSqr, limits().maxComputeWorkGroupSize[1])
    );

    const auto queueFamilyProperties = getQueueFamilyProperties(dld());
    for (uint32_t queueFamilyIndex = 0; queueFamilyIndex < queueFamilyProperties.size(); ++queueFamilyIndex)
    {
        auto &&props = queueFamilyProperties[queueFamilyIndex];

        if (props.queueCount < 1)
            continue;

#ifndef QMVK_NO_GRAPHICS
        const bool hasGraphics = static_cast<bool>(props.queueFlags & vk::QueueFlagBits::eGraphics);
#else
        const bool hasGraphics = false;
#endif
#if VK_HEADER_VERSION > 237
        const bool hasDecode = static_cast<bool>(props.queueFlags & vk::QueueFlagBits::eVideoDecodeKHR);
#else
        const bool hasDecode = false;
#endif
#if VK_HEADER_VERSION > 274
        const bool hasEncode = static_cast<bool>(props.queueFlags & vk::QueueFlagBits::eVideoEncodeKHR);
#else
        const bool hasEncode = false;
#endif
        const bool hasCompute = static_cast<bool>(props.queueFlags & vk::QueueFlagBits::eCompute);
        const bool hasTransfer = static_cast<bool>(props.queueFlags & vk::QueueFlagBits::eTransfer);
        if (!hasGraphics && !hasCompute && !hasTransfer && !hasDecode && !hasEncode)
            continue;

        m_queues[queueFamilyIndex] = {
            props.queueFlags,
            queueFamilyIndex,
            props.queueCount
        };
    }
}

vector<const char *> PhysicalDevice::filterAvailableExtensions(
    const vector<const char *> &wantedExtensions) const
{
    vector<const char *> availableWantedExtensions;
    availableWantedExtensions.reserve(wantedExtensions.size());
    for (auto &&wantedExtension : wantedExtensions)
    {
        if (checkExtension(wantedExtension))
        {
            availableWantedExtensions.push_back(wantedExtension);
            if (availableWantedExtensions.size() == wantedExtensions.size())
                break;
        }
    }
    return availableWantedExtensions;
}

bool PhysicalDevice::checkExtensions(
    const vector<const char *> &wantedExtensions) const
{
    size_t foundExtensions = 0;
    for (auto &&wantedExtension : wantedExtensions)
    {
        if (checkExtension(wantedExtension))
        {
            ++foundExtensions;
            if (foundExtensions == wantedExtensions.size())
                return true;
        }
    }
    return false;
}

shared_ptr<Device> PhysicalDevice::createDevice(
    const vk::PhysicalDeviceFeatures2 &features,
    const vector<const char *> &extensions,
    const vector<pair<uint32_t, uint32_t>> &queuesFamily)
{
    auto device = make_shared<Device>(
        shared_from_this()
    );
    device->init(features, extensions, queuesFamily);
    return device;
}

#ifdef QMVK_APPLY_MEMORY_PROPERTIES_QUIRKS
void PhysicalDevice::applyMemoryPropertiesQuirks(vk::PhysicalDeviceMemoryProperties &props) const
{
    const auto &physDevProps = properties();
    if (physDevProps.deviceType != vk::PhysicalDeviceType::eIntegratedGpu || physDevProps.vendorID != 0x1002 /* AMD */)
    {
        // Not an AMD integrated GPU
        return;
    }

    // Integrated AMD GPUs don't expose DeviceLocal flag when CPU has cached access.
    // Adding the DeviceLocal flag can prevent some copies and speed-up things,
    // because it's one and the same memory.

    constexpr auto hostFlags =
        vk::MemoryPropertyFlagBits::eHostVisible  |
        vk::MemoryPropertyFlagBits::eHostCoherent |
        vk::MemoryPropertyFlagBits::eHostCached
    ;

    const uint32_t nHeaps = props.memoryHeapCount;
    const uint32_t nTypes = props.memoryTypeCount;

    if (nHeaps <= 1)
        return;

    unordered_set<uint32_t> heapIndexes(nHeaps - 1);

    for (uint32_t i = 0; i < nTypes; ++i)
    {
        auto &memoryType = props.memoryTypes[i];

        if ((memoryType.propertyFlags & hostFlags) != hostFlags)
        {
            // Not a host coherent cached memory
            continue;
        }

        if (memoryType.propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal)
        {
            // We already have it, nothing to do
            return;
        }

        if (memoryType.heapIndex < nHeaps)
        {
            heapIndexes.insert(memoryType.heapIndex);
        }
    }

    for (auto &&heapIndex : heapIndexes)
    {
        props.memoryHeaps[heapIndex].flags |= vk::MemoryHeapFlagBits::eDeviceLocal;
        for (uint32_t i = 0; i < nTypes; ++i)
        {
            if (props.memoryTypes[i].heapIndex == heapIndex)
                props.memoryTypes[i].propertyFlags |= vk::MemoryPropertyFlagBits::eDeviceLocal;
        }
    }
}
#endif

vector<PhysicalDevice::MemoryHeap> PhysicalDevice::getMemoryHeapsInfo() const
{
    vk::PhysicalDeviceMemoryProperties2 props;
    vk::PhysicalDeviceMemoryBudgetPropertiesEXT budget;

    const bool useGetMemoryProperties2KHR = m_instance->checkExtension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    if (!m_instance->isVk10() || useGetMemoryProperties2KHR)
    {
        if (useGetMemoryProperties2KHR)
        {
            tie(props, budget) = getMemoryProperties2KHR<
                decltype(props),
                decltype(budget)
            >(dld()).get<
                decltype(props),
                decltype(budget)
            >();
        }
        else
        {
            tie(props, budget) = getMemoryProperties2<
                decltype(props),
                decltype(budget)
            >(dld()).get<
                decltype(props),
                decltype(budget)
            >();
        }
    }
    else
    {
        props = getMemoryProperties(dld());
    }
#ifdef QMVK_APPLY_MEMORY_PROPERTIES_QUIRKS
    applyMemoryPropertiesQuirks(props.memoryProperties);
#endif

    vector<MemoryHeap> memoryHeaps(props.memoryProperties.memoryHeapCount);
    for (uint32_t i = 0; i < props.memoryProperties.memoryHeapCount; ++i)
    {
        memoryHeaps[i].idx = i;
        memoryHeaps[i].size = props.memoryProperties.memoryHeaps[i].size;
        if (m_hasMemoryBudget)
        {
            memoryHeaps[i].budget = budget.heapBudget[i];
            memoryHeaps[i].usage = budget.heapUsage[i];
        }
        else
        {
            memoryHeaps[i].budget = memoryHeaps[i].size;
            memoryHeaps[i].usage = 0;
        }
        memoryHeaps[i].deviceLocal = static_cast<bool>(props.memoryProperties.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal);
        memoryHeaps[i].multiInstance = static_cast<bool>(props.memoryProperties.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eMultiInstance);
    }
    for (uint32_t i = 0; i < props.memoryProperties.memoryTypeCount; ++i)
    {
        if (props.memoryProperties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible)
            memoryHeaps[props.memoryProperties.memoryTypes[i].heapIndex].hostVisible = true;
    }

    return memoryHeaps;
}

PhysicalDevice::MemoryType PhysicalDevice::findMemoryType(
    const MemoryPropertyFlags &memoryPropertyFlags,
    uint32_t memoryTypeBits,
    uint32_t heap) const
{
    using MemoryTypeResult = pair<MemoryType, bool>;
    MemoryTypeResult result;

    auto memoryProperties = getMemoryProperties(dld());
#ifdef QMVK_APPLY_MEMORY_PROPERTIES_QUIRKS
    applyMemoryPropertiesQuirks(memoryProperties);
#endif
    bool optionalFallbackFound = false;
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        if (heap != ~0u && memoryProperties.memoryTypes[i].heapIndex != heap)
            continue;

        if (!(memoryTypeBits & (1u << i)))
            continue;

        const auto currMemoryPropertyFlags = memoryProperties.memoryTypes[i].propertyFlags;
        const auto required = memoryPropertyFlags.required;
        if ((currMemoryPropertyFlags & required) == required)
        {
            const MemoryTypeResult currResult = {{i, currMemoryPropertyFlags}, true};
            bool doBreak = false;

            const auto optional = memoryPropertyFlags.optional;
            const auto optionalFallback = memoryPropertyFlags.optionalFallback;
            const auto notWanted = memoryPropertyFlags.notWanted;

            auto getFlagsWithoutNotWanted = [&] {
                return (currMemoryPropertyFlags & ~notWanted);
            };

            if (optional || optionalFallback)
            {
                auto testFlags = [&](vk::MemoryPropertyFlags flags) {
                    return ((getFlagsWithoutNotWanted() & flags) == flags);
                };

                if (optional && testFlags(optional))
                {
                    result = currResult;
                    break;
                }
                if (optionalFallback && !optionalFallbackFound && testFlags(optionalFallback))
                {
                    result = currResult;
                    optionalFallbackFound = true;
                }
            }
            else if (notWanted)
            {
                if (getFlagsWithoutNotWanted() == currMemoryPropertyFlags)
                {
                    result = currResult;
                    break;
                }
            }
            else
            {
                doBreak = true;
            }

            if (!result.second)
                result = currResult;

            if (doBreak)
                break;
        }
    }

    if (!result.second)
        throw vk::InitializationFailedError("Cannot find specified memory type");

    return result.first;
}
PhysicalDevice::MemoryType PhysicalDevice::findMemoryType(
    uint32_t memoryTypeBits) const
{
    return findMemoryType(MemoryPropertyFlags(), memoryTypeBits);
}

vector<pair<uint32_t, uint32_t>> PhysicalDevice::getQueuesFamily(
    vk::QueueFlags queueFlags,
    bool tryExcludeGraphics,
    bool firstOnly,
    bool exceptionOnFail) const
{
    vector<pair<uint32_t, uint32_t>> ret;
    for (uint32_t t = 0; t < 2; ++t)
    {
        for (auto &&familypropsPair : m_queues)
        {
            auto &&props = familypropsPair.second;

            if (tryExcludeGraphics && (props.flags & vk::QueueFlagBits::eGraphics))
                continue;

            if ((props.flags & queueFlags) == queueFlags)
            {
                ret.emplace_back(props.familyIndex, props.count);
                if (firstOnly)
                    break;
            }
        }

        if (tryExcludeGraphics && ret.empty())
        {
            tryExcludeGraphics = false;
            continue;
        }

        break;
    }
    if (exceptionOnFail && ret.empty())
        throw vk::InitializationFailedError("Cannot find specified queue family");
    return ret;
}

string PhysicalDevice::linuxPCIPath() const
{
    if (!m_hasPciBusInfo)
        return string();

    char out[13];
    snprintf(
        out,
        sizeof(out),
        "%.4x:%.2x:%.2x.%1x",
        m_pciBusInfo.pciDomain,
        m_pciBusInfo.pciBus,
        m_pciBusInfo.pciDevice,
        m_pciBusInfo.pciFunction
    );
    return out;
}

const vk::FormatProperties &PhysicalDevice::getFormatPropertiesCached(vk::Format fmt)
{
    lock_guard<mutex> locker(m_formatPropertiesMutex);
    auto it = m_formatProperties.find(fmt);
    if (it == m_formatProperties.end())
    {
        m_formatProperties[fmt] = getFormatProperties(fmt, dld());
        it = m_formatProperties.find(fmt);
    }
    return it->second;
}

}
