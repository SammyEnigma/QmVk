// SPDX-License-Identifier: MIT
/*
   QmVk - simple Vulkan library created for QMPlay2
   Copyright (C) 2020-2025 Błażej Szczygieł
*/

#pragma once

#include "QmVkExport.hpp"

#include <vulkan/vulkan.hpp>

#include <memory>

namespace QmVk {

using namespace std;

class Device;

class QMVK_EXPORT ShaderModule
{
public:
    static shared_ptr<ShaderModule> create(
        const shared_ptr<Device> &device,
        vk::ShaderStageFlagBits stage,
        const vector<uint32_t> &data
    );

public:
    ShaderModule(
        const shared_ptr<Device> &device,
        vk::ShaderStageFlagBits stage
    );
    ~ShaderModule();

private:
    void init(const vector<uint32_t> &data);

public:
    inline vk::ShaderStageFlagBits stage() const;

    vk::PipelineShaderStageCreateInfo getPipelineShaderStageCreateInfo(
        const vk::SpecializationInfo &specializationInfo
    ) const;

private:
    const shared_ptr<Device> m_device;
    const vk::ShaderStageFlagBits m_stage;

    vk::UniqueShaderModule m_shaderModule;
};

/* Inline implementation */

vk::ShaderStageFlagBits ShaderModule::stage() const
{
    return m_stage;
}

}
