#include "application.hpp"
#include "graphics.hpp"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <exception>
#include <cstddef>
#include <iostream>
#include <iterator>

namespace application {
namespace {

struct Vertex {
    float position[3];
};

struct alignas(16) GlobalUniforms {
    glm::mat4 mvp;
};
static_assert(sizeof(Vertex) == 3 * sizeof(float));
static_assert(sizeof(GlobalUniforms) == 64);

graphics::Buffer vertex_buffer;
graphics::Buffer index_buffer;
graphics::Buffer uniform_buffer;
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
    graphics::check(vkCreateDescriptorSetLayout(device, &layout, nullptr, &descriptor_set_layout),
          "Create descriptor set layout");
    const VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    const VkDescriptorPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };
    graphics::check(vkCreateDescriptorPool(device, &pool, nullptr, &descriptor_pool),
          "Create descriptor pool");
    const VkDescriptorSetAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &descriptor_set_layout,
    };
    graphics::check(vkAllocateDescriptorSets(device, &allocation, &descriptor_set),
          "Allocate descriptor set");
    const VkDescriptorBufferInfo buffer = {
        .buffer = uniform_buffer.handle,
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
    graphics::check(vkCreatePipelineLayout(context.device, &layout, nullptr, &pipeline_layout),
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
    graphics::check(vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
          "Create graphics pipeline");
}

}

bool initialize() {
    try {
        vertex_buffer = graphics::createBuffer(sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        graphics::writeBuffer(vertex_buffer, vertices, sizeof(vertices));
        index_buffer = graphics::createBuffer(sizeof(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        graphics::writeBuffer(index_buffer, indices, sizeof(indices));
        uniform_buffer = graphics::createBuffer(sizeof(GlobalUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        createDescriptors();
        vertex_shader = graphics::loadShader("shaders/cube.vert.spv");
        fragment_shader = graphics::loadShader("shaders/cube.frag.spv");
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
    graphics::destroyBuffer(uniform_buffer);
    graphics::destroyBuffer(index_buffer);
    graphics::destroyBuffer(vertex_buffer);
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
    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::make_vec3(controls.position));
    model = glm::rotate(model, glm::radians(controls.rotation[2]), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(controls.rotation[1]), glm::vec3(0, 1, 0));
    model = glm::rotate(model, glm::radians(controls.rotation[0]), glm::vec3(1, 0, 0));
    model = glm::scale(model, glm::make_vec3(controls.scale));
    const glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -controls.distance));
    const float half_height = controls.ortho_size;
    glm::mat4 projection = controls.perspective
        ? glm::perspectiveRH_ZO(glm::radians(controls.fov), aspect, 0.1f, 100.0f)
        : glm::orthoRH_ZO(-half_height * aspect, half_height * aspect,
                         -half_height, half_height, 0.1f, 100.0f);
    projection[1][1] *= -1.0f;
    const GlobalUniforms uniforms = {projection * view * model};
    graphics::writeBuffer(uniform_buffer, &uniforms, sizeof(uniforms));
}

void render(const graphics::internal::FrameData& fd) {
    auto& context = graphics::internal::context;
    graphics::check(vkResetCommandBuffer(fd.command_buffer, 0), "Reset application command buffer");
    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    graphics::check(vkBeginCommandBuffer(fd.command_buffer, &begin), "Begin application command buffer");
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
    vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer.handle, &offset);
    vkCmdBindIndexBuffer(fd.command_buffer, index_buffer.handle, 0, VK_INDEX_TYPE_UINT16);
    vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
    vkCmdDrawIndexed(fd.command_buffer, static_cast<uint32_t>(std::size(indices)), 1, 0, 0, 0);
    vkCmdEndRenderPass(fd.command_buffer);
    graphics::check(vkEndCommandBuffer(fd.command_buffer), "End application command buffer");
}

}
