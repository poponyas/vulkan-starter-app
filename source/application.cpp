#include "application.hpp"

#include <imgui.h>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace application {
namespace {

struct Vertex {
    float position[3];
};

struct alignas(16) Mat4 {
    float data[16] = {};
};
struct alignas(16) GlobalUniforms {
    Mat4 mvp;
};
static_assert(sizeof(Vertex) == 3 * sizeof(float));
static_assert(sizeof(GlobalUniforms) == 64);

VkBuffer vertex_buffer = VK_NULL_HANDLE;
VmaAllocation vertex_buffer_allocation = VK_NULL_HANDLE;
VkBuffer index_buffer = VK_NULL_HANDLE;
VmaAllocation index_buffer_allocation = VK_NULL_HANDLE;
VkBuffer uniform_buffer = VK_NULL_HANDLE;
VmaAllocation uniform_buffer_allocation = VK_NULL_HANDLE;
VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
VkShaderModule vertex_shader = VK_NULL_HANDLE;
VkShaderModule fragment_shader = VK_NULL_HANDLE;
VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
VkPipeline pipeline = VK_NULL_HANDLE;

struct Controls {
    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation[3] = {25.0f, -35.0f, 0.0f};
    float scale[3] = {1.0f, 1.0f, 1.0f};
    float distance = 3.5f;
    float fov = 60.0f;
    float ortho_size = 1.5f;
    bool perspective = true;
};
Controls controls;

constexpr float pi = 3.14159265358979323846f;
float radians(float degrees) { return degrees * pi / 180.0f; }

Mat4 identity() {
    Mat4 m;
    m.data[0] = m.data[5] = m.data[10] = m.data[15] = 1.0f;
    return m;
}
Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 result;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                result.data[column * 4 + row] +=
                    a.data[k * 4 + row] * b.data[column * 4 + k];
    return result;
}
Mat4 translation(float x, float y, float z) {
    Mat4 m = identity();
    m.data[12] = x; m.data[13] = y; m.data[14] = z;
    return m;
}
Mat4 scaling(float x, float y, float z) {
    Mat4 m = identity();
    m.data[0] = x; m.data[5] = y; m.data[10] = z;
    return m;
}
Mat4 rotationX(float angle) {
    Mat4 m = identity();
    const float c = std::cos(angle), s = std::sin(angle);
    m.data[5] = c; m.data[6] = s; m.data[9] = -s; m.data[10] = c;
    return m;
}
Mat4 rotationY(float angle) {
    Mat4 m = identity();
    const float c = std::cos(angle), s = std::sin(angle);
    m.data[0] = c; m.data[2] = -s; m.data[8] = s; m.data[10] = c;
    return m;
}
Mat4 rotationZ(float angle) {
    Mat4 m = identity();
    const float c = std::cos(angle), s = std::sin(angle);
    m.data[0] = c; m.data[1] = s; m.data[4] = -s; m.data[5] = c;
    return m;
}
Mat4 perspective(float fov, float aspect, float near_plane, float far_plane) {
    Mat4 m;
    const float cotangent = 1.0f / std::tan(fov / 2.0f);
    m.data[0] = cotangent / aspect;
    m.data[5] = -cotangent; 
    m.data[10] = far_plane / (near_plane - far_plane);
    m.data[11] = -1.0f;    
    m.data[14] = near_plane * far_plane / (near_plane - far_plane);
    return m;
}
Mat4 orthographic(float half_height, float aspect, float near_plane, float far_plane) {
    Mat4 m = identity();
    m.data[0] = 1.0f / (half_height * aspect);
    m.data[5] = -1.0f / half_height;
    m.data[10] = 1.0f / (near_plane - far_plane);
    m.data[14] = near_plane / (near_plane - far_plane);
    return m;
}


const Vertex vertices[] = {
    { {-0.5f, -0.5f, -0.5f} },
    { { 0.5f, -0.5f, -0.5f} },
    { { 0.5f,  0.5f, -0.5f} },
    { {-0.5f,  0.5f, -0.5f} },

    { {-0.5f, -0.5f,  0.5f} },
    { { 0.5f, -0.5f,  0.5f} },
    { { 0.5f,  0.5f,  0.5f} },
    { {-0.5f,  0.5f,  0.5f} },
};

const uint16_t indices[] = {
    4, 5, 6,
    4, 6, 7,

    0, 3, 2,
    0, 2, 1,

    0, 4, 7,
    0, 7, 3,

    1, 2, 6,
    1, 6, 5,

    0, 1, 5,
    0, 5, 4,

    3, 7, 6,
    3, 6, 2,
};


void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " (VkResult " +
                                 std::to_string(result) + ")");
}
void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                  VkBuffer& buffer, VmaAllocation& allocation) {
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
    check(vmaCreateBuffer(graphics::internal::context.allocator, &info, &memory,
                          &buffer, &allocation, nullptr), "Create application buffer");
}
void uploadBuffer(VmaAllocation allocation, const void* data, VkDeviceSize size) {
    auto allocator = graphics::internal::context.allocator;
    void* mapped = nullptr;
    check(vmaMapMemory(allocator, allocation, &mapped), "Map application buffer");
    std::memcpy(mapped, data, static_cast<size_t>(size));
    const VkResult result = vmaFlushAllocation(allocator, allocation, 0, size);
    vmaUnmapMemory(allocator, allocation);
    check(result, "Flush application buffer");
}


void loadShader(const char* path, VkShaderModule& shader) {
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
    check(vkCreateShaderModule(graphics::internal::context.device, &info, nullptr, &shader),
          "Create shader module");
}


void createDescriptors() {
    const auto device = graphics::internal::context.device;
    const VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    };
    const VkDescriptorSetLayoutCreateInfo layout = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    check(vkCreateDescriptorSetLayout(device, &layout, nullptr, &descriptor_set_layout),
          "Create descriptor set layout");
    const VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    const VkDescriptorPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };
    check(vkCreateDescriptorPool(device, &pool, nullptr, &descriptor_pool),
          "Create descriptor pool");
    const VkDescriptorSetAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &descriptor_set_layout,
    };
    check(vkAllocateDescriptorSets(device, &allocation, &descriptor_set),
          "Allocate descriptor set");
    const VkDescriptorBufferInfo buffer = {
        .buffer = uniform_buffer,
        .offset = 0,
        .range = sizeof(GlobalUniforms),
    };
    const VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptor_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &buffer,
    };
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}


void createPipeline() {
    auto& context = graphics::internal::context;
    const VkPipelineLayoutCreateInfo layout = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &descriptor_set_layout,
    };
    check(vkCreatePipelineLayout(context.device, &layout, nullptr, &pipeline_layout),
          "Create pipeline layout");
    const VkPipelineShaderStageCreateInfo stages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex_shader, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment_shader, .pName = "main"},
    };
    const VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const VkVertexInputAttributeDescription attribute = {
        .location = 0,
        .binding = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = offsetof(Vertex, position),
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 1,
        .pVertexAttributeDescriptions = &attribute,
    };
    const VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    const VkPipelineRasterizationStateCreateInfo rasterization = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
    };
    const VkPipelineColorBlendAttachmentState attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo blending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &attachment,
    };
    const VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = states,
    };
    const VkGraphicsPipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &assembly,
        .pViewportState = &viewport,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depth,
        .pColorBlendState = &blending,
        .pDynamicState = &dynamic,
        .layout = pipeline_layout,
        .renderPass = context.render_pass,
        .subpass = 0,
    };
    check(vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
          "Create graphics pipeline");
}

} // namespace


bool initialize() {
    try {
        createBuffer(sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     vertex_buffer, vertex_buffer_allocation);
        uploadBuffer(vertex_buffer_allocation, vertices, sizeof(vertices));
        createBuffer(sizeof(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     index_buffer, index_buffer_allocation);
        uploadBuffer(index_buffer_allocation, indices, sizeof(indices));
        createBuffer(sizeof(GlobalUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     uniform_buffer, uniform_buffer_allocation);
        createDescriptors();
        loadShader("shaders/cube.vert.spv", vertex_shader);
        loadShader("shaders/cube.frag.spv", fragment_shader);
        createPipeline();
        return true;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        shutdown();
        return false;
    }
}

void shutdown() {
    auto& context = graphics::internal::context;
    
    vkQueueWaitIdle(context.graphics_queue);
    vkDestroyPipeline(context.device, pipeline, nullptr);
    vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
    vkDestroyShaderModule(context.device, vertex_shader, nullptr);
    vkDestroyShaderModule(context.device, fragment_shader, nullptr);
    vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
    vkDestroyDescriptorSetLayout(context.device, descriptor_set_layout, nullptr);
    pipeline = VK_NULL_HANDLE;
    pipeline_layout = VK_NULL_HANDLE;
    vertex_shader = fragment_shader = VK_NULL_HANDLE;
    descriptor_pool = VK_NULL_HANDLE;
    descriptor_set_layout = VK_NULL_HANDLE;
    descriptor_set = VK_NULL_HANDLE;
    if (uniform_buffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(context.allocator, uniform_buffer, uniform_buffer_allocation);
    if (index_buffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(context.allocator, index_buffer, index_buffer_allocation);
    if (vertex_buffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(context.allocator, vertex_buffer, vertex_buffer_allocation);
    uniform_buffer = index_buffer = vertex_buffer = VK_NULL_HANDLE;
    uniform_buffer_allocation = index_buffer_allocation = vertex_buffer_allocation = VK_NULL_HANDLE;
}


void update([[maybe_unused]] double time) {
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(310, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Lab 1 - Variant 12");
    ImGui::TextUnformatted("Regular hexahedron (cube)");
    ImGui::TextUnformatted("8 vertices, 12 triangles");
    ImGui::Separator();
    ImGui::PushItemWidth(175.0f);
    ImGui::Checkbox("Perspective", &controls.perspective);
    if (controls.perspective)
        ImGui::SliderFloat("FOV", &controls.fov, 25.0f, 90.0f, "%.1f deg");
    else
        ImGui::SliderFloat("Half-height", &controls.ortho_size, 0.6f, 4.0f);
    ImGui::SliderFloat("Camera distance", &controls.distance, 1.5f, 8.0f);
    ImGui::Separator();
    ImGui::SliderFloat3("Position", controls.position, -1.5f, 1.5f);
    ImGui::SliderFloat3("Rotation", controls.rotation, -180.0f, 180.0f, "%.0f deg");
    ImGui::SliderFloat3("Scale", controls.scale, 0.1f, 2.0f);
    if (ImGui::Button("Reset")) controls = Controls{};
    ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
    ImGui::PopItemWidth();
    ImGui::End();

    const auto extent = graphics::internal::context.swapchain_extent;
    if (extent.width == 0 || extent.height == 0) return;
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const Mat4 scale = scaling(controls.scale[0], controls.scale[1], controls.scale[2]);
    const Mat4 rotate = multiply(rotationZ(radians(controls.rotation[2])),
                       multiply(rotationY(radians(controls.rotation[1])),
                                rotationX(radians(controls.rotation[0]))));
    const Mat4 model = multiply(translation(controls.position[0], controls.position[1],
                                           controls.position[2]), multiply(rotate, scale));
    const Mat4 view = translation(0.0f, 0.0f, -controls.distance);
    const Mat4 projection = controls.perspective
        ? perspective(radians(controls.fov), aspect, 0.1f, 100.0f)
        : orthographic(controls.ortho_size, aspect, 0.1f, 100.0f);
    const GlobalUniforms uniforms = {multiply(projection, multiply(view, model))};
    uploadBuffer(uniform_buffer_allocation, &uniforms, sizeof(uniforms));
}


void render(const graphics::internal::FrameData& fd) {
    auto& context = graphics::internal::context;
    check(vkResetCommandBuffer(fd.command_buffer, 0), "Reset application command buffer");
    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    check(vkBeginCommandBuffer(fd.command_buffer, &begin), "Begin application command buffer");
    const VkClearValue clear[] = {
        {.color = {{0.08f, 0.08f, 0.10f, 1.0f}}},
        {.depthStencil = {1.0f, 0}},
    };
    const VkRenderPassBeginInfo pass = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = context.render_pass,
        .framebuffer = fd.framebuffer,
        .renderArea = {.extent = context.swapchain_extent},
        .clearValueCount = 2,
        .pClearValues = clear,
    };
    vkCmdBeginRenderPass(fd.command_buffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport = {
        .width = static_cast<float>(context.swapchain_extent.width),
        .height = static_cast<float>(context.swapchain_extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    const VkRect2D scissor = {.extent = context.swapchain_extent};
    vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);
    vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer, &offset);
    vkCmdBindIndexBuffer(fd.command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
    vkCmdDrawIndexed(fd.command_buffer, static_cast<uint32_t>(std::size(indices)), 1, 0, 0, 0);
    vkCmdEndRenderPass(fd.command_buffer);
    check(vkEndCommandBuffer(fd.command_buffer), "End application command buffer");
}

} // namespace application
