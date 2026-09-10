#include "Pipeline.h"
#include "VulkanDevice.h"

#include <fstream>

namespace woc
{
    VkShaderModule LoadShaderModule(const std::string& spirvPath)
    {
        std::ifstream file(spirvPath, std::ios::binary | std::ios::ate);
        if (!file) throw std::runtime_error("Shader not found: " + spirvPath);

        const std::streamsize size = file.tellg();
        file.seekg(0);
        std::vector<u32> code(static_cast<size_t>(size) / sizeof(u32));
        file.read(reinterpret_cast<char*>(code.data()), size);

        VkShaderModuleCreateInfo info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        info.codeSize = static_cast<size_t>(size);
        info.pCode = code.data();

        VkShaderModule module = VK_NULL_HANDLE;
        VK_CHECK(vkCreateShaderModule(VulkanDevice::Get().Handle(), &info, nullptr, &module));
        return module;
    }

    PipelineBuilder& PipelineBuilder::Shaders(VkShaderModule vertex, VkShaderModule fragment)
    {
        m_vertex = vertex;
        m_fragment = fragment;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::VertexBinding(u32 binding, u32 stride, VkVertexInputRate rate)
    {
        m_bindings.push_back({ binding, stride, rate });
        return *this;
    }

    PipelineBuilder& PipelineBuilder::VertexAttribute(u32 location, u32 binding, VkFormat format, u32 offset)
    {
        m_attributes.push_back({ location, binding, format, offset });
        return *this;
    }

    PipelineBuilder& PipelineBuilder::Topology(VkPrimitiveTopology topology)
    {
        m_topology = topology;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::CullMode(VkCullModeFlags mode, VkFrontFace frontFace)
    {
        m_cullMode = mode;
        m_frontFace = frontFace;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::DepthTest(bool test, bool write)
    {
        m_depthTest = test;
        m_depthWrite = write;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::AlphaBlend(bool enabled)
    {
        m_blend = enabled;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::Layout(VkPipelineLayout layout)
    {
        m_layout = layout;
        return *this;
    }

    VkPipeline PipelineBuilder::Build(VkRenderPass renderPass, u32 subpass)
    {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = m_vertex;
        stages[0].pName = "main";
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = m_fragment;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vertexInput.vertexBindingDescriptionCount = static_cast<u32>(m_bindings.size());
        vertexInput.pVertexBindingDescriptions = m_bindings.empty() ? nullptr : m_bindings.data();
        vertexInput.vertexAttributeDescriptionCount = static_cast<u32>(m_attributes.size());
        vertexInput.pVertexAttributeDescriptions = m_attributes.empty() ? nullptr : m_attributes.data();

        VkPipelineInputAssemblyStateCreateInfo assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        assembly.topology = m_topology;

        VkPipelineViewportStateCreateInfo viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo raster{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = m_cullMode;
        raster.frontFace = m_frontFace;
        raster.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        depth.depthTestEnable = m_depthTest ? VK_TRUE : VK_FALSE;
        depth.depthWriteEnable = m_depthWrite ? VK_TRUE : VK_FALSE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachment.blendEnable = m_blend ? VK_TRUE : VK_FALSE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo blend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blend.attachmentCount = 1;
        blend.pAttachments = &blendAttachment;

        const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamicStates;

        VkGraphicsPipelineCreateInfo info{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = m_layout;
        info.renderPass = renderPass;
        info.subpass = subpass;

        VkPipeline pipeline = VK_NULL_HANDLE;
        VK_CHECK(vkCreateGraphicsPipelines(VulkanDevice::Get().Handle(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline));
        return pipeline;
    }
}
