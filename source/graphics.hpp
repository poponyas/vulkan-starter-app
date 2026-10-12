#pragma once

#include "graphics_internal.hpp"

namespace graphics {

struct Buffer {
    VkBuffer handle = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
};

void check(VkResult result, const char* operation);
Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage);
void writeBuffer(const Buffer& buffer, const void* data, VkDeviceSize size);
void destroyBuffer(Buffer& buffer);
VkShaderModule loadShader(const char* path);

}
