#include "graphics.hpp"

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace graphics {

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " (VkResult " +
                                 std::to_string(result) + ")");
}

Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage) {
    const VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    const VmaAllocationCreateInfo memory = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    Buffer buffer;
    check(vmaCreateBuffer(internal::context.allocator, &info, &memory,
                          &buffer.handle, &buffer.allocation, nullptr), "Create application buffer");
    return buffer;
}

void writeBuffer(const Buffer& buffer, const void* data, VkDeviceSize size) {
    auto allocator = internal::context.allocator;
    void* mapped = nullptr;
    check(vmaMapMemory(allocator, buffer.allocation, &mapped), "Map application buffer");
    std::memcpy(mapped, data, static_cast<size_t>(size));
    const VkResult result = vmaFlushAllocation(allocator, buffer.allocation, 0, size);
    vmaUnmapMemory(allocator, buffer.allocation);
    check(result, "Flush application buffer");
}

void destroyBuffer(Buffer& buffer) {
    if (buffer.handle != VK_NULL_HANDLE)
        vmaDestroyBuffer(internal::context.allocator, buffer.handle, buffer.allocation);
    buffer = {};
}

VkShaderModule loadShader(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error(std::string("Cannot open shader: ") + path +
                                 "; launch from the project directory using ./run.sh");
    const auto size = file.tellg();
    if (size < 20 || size % 4 != 0)
        throw std::runtime_error(std::string("Invalid SPIR-V size: ") + path);
    std::vector<uint32_t> code(static_cast<size_t>(size) / sizeof(uint32_t));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(code.data()), size) || code[0] != 0x07230203)
        throw std::runtime_error(std::string("Invalid SPIR-V file: ") + path);
    const VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = static_cast<size_t>(size),
        .pCode = code.data(),
    };
    VkShaderModule shader = VK_NULL_HANDLE;
    check(vkCreateShaderModule(internal::context.device, &info, nullptr, &shader),
          "Create shader module");
    return shader;
}

}
