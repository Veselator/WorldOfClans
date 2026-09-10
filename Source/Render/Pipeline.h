// Pipeline.h - small fluent builder for graphics pipelines.
//
// Keeps Renderer.cpp readable: each pipeline is described by the handful of states that
// actually differ between them, and the builder supplies sane defaults for the rest.
#pragma once

#include "VulkanCommon.h"

namespace woc
{
    VkShaderModule LoadShaderModule(const std::string& spirvPath);

    class PipelineBuilder
    {
    public:
        PipelineBuilder& Shaders(VkShaderModule vertex, VkShaderModule fragment);
        PipelineBuilder& VertexBinding(u32 binding, u32 stride, VkVertexInputRate rate);
        PipelineBuilder& VertexAttribute(u32 location, u32 binding, VkFormat format, u32 offset);
        PipelineBuilder& Topology(VkPrimitiveTopology topology);
        PipelineBuilder& CullMode(VkCullModeFlags mode, VkFrontFace frontFace);
        PipelineBuilder& DepthTest(bool test, bool write);
        PipelineBuilder& AlphaBlend(bool enabled);
        PipelineBuilder& Layout(VkPipelineLayout layout);

        VkPipeline Build(VkRenderPass renderPass, u32 subpass = 0);

    private:
        VkShaderModule m_vertex = VK_NULL_HANDLE;
        VkShaderModule m_fragment = VK_NULL_HANDLE;
        std::vector<VkVertexInputBindingDescription> m_bindings;
        std::vector<VkVertexInputAttributeDescription> m_attributes;
        VkPrimitiveTopology m_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkCullModeFlags m_cullMode = VK_CULL_MODE_NONE;
        VkFrontFace m_frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        bool m_depthTest = false;
        bool m_depthWrite = false;
        bool m_blend = true;
        VkPipelineLayout m_layout = VK_NULL_HANDLE;
    };
}
