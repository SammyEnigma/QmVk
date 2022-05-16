// SPDX-License-Identifier: MIT
/*
   QmVk - simple Vulkan library created for QMPlay2
   Copyright (C) 2020-2022 Błażej Szczygieł
*/

#pragma once

#include <vulkan/vulkan.hpp>

namespace QmVk {

using namespace std;

class DescriptorInfo
{
public:
    enum class Type
    {
        DescriptorBufferInfo,
        DescriptorImageInfo,
        BufferView,
    };

    inline DescriptorInfo(const vk::DescriptorBufferInfo &descrInfo)
        : type(Type::DescriptorBufferInfo)
        , descrBuffInfo(descrInfo)
    {}
    inline DescriptorInfo(const vk::DescriptorImageInfo &descrInfo)
        : type(Type::DescriptorImageInfo)
        , descrImgInfo(descrInfo)
    {}
    inline DescriptorInfo(vk::BufferView bufferView)
        : type(Type::BufferView)
        , bufferView(bufferView)
    {}

    Type type;

    vk::DescriptorBufferInfo descrBuffInfo;
    vk::DescriptorImageInfo descrImgInfo;
    vk::BufferView bufferView;
};

}
