#define GLM_FORCE_RADIANS
#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
#include <SDL3/SDL_events.h>
#include <vb.h>
#include "app.h"
#include <stb/stb_image.h>

struct Vertex {
    glm::vec3 position;
    float uv_x;
    glm::vec3 normal;
    float uv_y;
    glm::vec4 color;
};

struct PushConstants {
    glm::mat4 render_matrix;
    VkDeviceAddress vertex_buffer;
};

struct Rectangle {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vb::Buffer vertex_buffer;
    vb::Buffer index_buffer;
    VkDeviceAddress vertex_buffer_address;

    Rectangle(vb::Context* context, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
	: vertex_buffer{context}, index_buffer{context}, vertices{vertices}, indices{indices} {
	    const size_t vertices_size = sizeof(Vertex) * vertices.size();
	    vertex_buffer.create(vertices_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT 
		    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		    VMA_MEMORY_USAGE_GPU_ONLY);
	    assert(vertex_buffer.all_valid());
	    VkBufferDeviceAddressInfo address = {
    	        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
    	        .buffer = vertex_buffer.buffer,
    	    };
    	    vertex_buffer_address = vkGetBufferDeviceAddress(context->device, &address);

	    const size_t indices_size = sizeof(uint32_t) * indices.size();
	    index_buffer.create(indices_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT
		    | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
	    assert(index_buffer.all_valid());

	    auto staging_buffer = vb::Buffer{context};
    	    staging_buffer.create(vertices_size + indices_size,
		    VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
	    assert(staging_buffer.all_valid());
    	    memcpy(staging_buffer.info.pMappedData, vertices.data(), vertices_size);
    	    memcpy((char*)(staging_buffer.info.pMappedData)+vertices_size, indices.data(),
		    indices_size);
	    context->submit_command_to_queue([&](VkCommandBuffer cmd) {
    	        VkBufferCopy copy = { .size = vertices_size };
    	        vkCmdCopyBuffer(cmd, staging_buffer.buffer, vertex_buffer.buffer, 1, &copy);
    	        VkBufferCopy copy2 = { .srcOffset = vertices_size, .size = indices_size };
    	        vkCmdCopyBuffer(cmd, staging_buffer.buffer, index_buffer.buffer, 1, &copy2);
    	    });
    	    staging_buffer.clean();
    }
};

inline VkDeviceSize aligned_size(VkDeviceSize value, VkDeviceSize alignment) {
    return (value+alignment-1)&~(alignment-1);
}

struct DescriptorBuffer {
    VkDescriptorSetLayout layout;
    vb::Buffer buffer;
    VkDeviceAddress address;
    VkDescriptorBufferBindingInfoEXT binding_info;
    uint32_t buffer_index;
    size_t binding_offset;
};

struct ComputeDescriptorBuffers: public App {
    vb::CommandPool cmdpool {&vbc};
    vb::Image texture {&vbc};
    vb::Image depth_image {&vbc};
    vb::Image comp_image {&vbc};
    VkSampler sampler;
    Rectangle* rectangle;
    Rectangle* rectangle2;
    VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptor_buffer_prop
	{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT};
    DescriptorBuffer graphics_descriptor{.buffer=&vbc};
    DescriptorBuffer compute_descriptor{.buffer=&vbc};
    vb::GraphicsPipeline graphics_pipeline {&vbc};
    VkPipelineLayout compute_layout;
    VkPipeline compute_pipeline;

    ComputeDescriptorBuffers(): App{} {
	VkPhysicalDeviceDescriptorBufferFeaturesEXT buffer_ext = {
    	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT,
    	    .descriptorBuffer = VK_TRUE,
    	};
	vb::ContextInstanceWindowInfo windowinfo = {
	    .title = "vbc",
	    .width = 800,
	    .height = 600,
	    .vulkan_api = VK_API_VERSION_1_3,
	};
	windowinfo.require_debug();
	vb::ContextDeviceInfo deviceinfo = {
    	    .required_extensions = {VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME},
    	    .vk10features = {.samplerAnisotropy = VK_TRUE},
    	    .vk12features = {
    	        .descriptorIndexing = VK_TRUE,
    	        .bufferDeviceAddress = VK_TRUE,
    	    },
    	    .vk13features = {
    	        .pNext = &buffer_ext,
    	        .synchronization2 = VK_TRUE,
    	        .dynamicRendering = VK_TRUE,
    	    },
    	};
	vb::ContextSwapchainInfo swapchaininfo = {};
	create(windowinfo, deviceinfo, swapchaininfo, VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT);
	create_cmdpool();
	create_images();
	create_sampler();
	create_rectangles();
	setup_descriptor_buffers();
	create_pipelines();
	vbc.set_resize_callback([&]() {
    	    vbc.recreate_swapchain([&](uint32_t,uint32_t) {
    	        depth_image.clean();
    	        comp_image.clean();
    	    });
    	    depth_image.create({vbc.swapchain_extent.width, vbc.swapchain_extent.height, 1},
		false, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_D32_SFLOAT,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    	    comp_image.create({vbc.swapchain_extent.width, vbc.swapchain_extent.height, 1},
    	        VK_FORMAT_R16G16B16A16_SFLOAT);
    	});
    }

    ~ComputeDescriptorBuffers() {
	graphics_pipeline.clean();
	graphics_pipeline.clean_shaders();
	vkDestroyDescriptorSetLayout(vbc.device, graphics_descriptor.layout, nullptr);
    	graphics_descriptor.buffer.clean();
    	vkDestroyPipeline(vbc.device, compute_pipeline, nullptr);
    	vkDestroyPipelineLayout(vbc.device, compute_layout, nullptr);
    	vkDestroyDescriptorSetLayout(vbc.device, compute_descriptor.layout, nullptr);
    	compute_descriptor.buffer.clean();
	rectangle->vertex_buffer.clean();
    	rectangle->index_buffer.clean();
    	rectangle2->vertex_buffer.clean();
    	rectangle2->index_buffer.clean();
	delete rectangle;
	delete rectangle2;
	vkDestroySampler(vbc.device, sampler, nullptr);
	comp_image.clean();
	depth_image.clean();
	texture.clean();
	cmdpool.clean();
    }

    void create_cmdpool() {
	cmdpool.create(queue->index);
	cmdpool.all_valid();
    }

    void create_images() {
    	texture.create("../samples/textures/texture.jpg");
    	assert(texture.all_valid());
	depth_image.create({vbc.swapchain_extent.width, vbc.swapchain_extent.height, 1},
	    false, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_D32_SFLOAT,
	    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	assert(depth_image.all_valid());
    	comp_image.create({vbc.swapchain_extent.width, vbc.swapchain_extent.height, 1},
	    VK_FORMAT_R16G16B16A16_SFLOAT);
	assert(comp_image.all_valid());
    }

    void create_sampler() {
	VkPhysicalDeviceProperties pdev_prop{};
    	vkGetPhysicalDeviceProperties(vbc.physical_device, &pdev_prop);
    	VkSamplerCreateInfo sampler_info = {
    	    .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
    	    .magFilter = VK_FILTER_LINEAR,
    	    .minFilter = VK_FILTER_LINEAR,
    	    .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
    	    .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    	    .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    	    .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    	    .mipLodBias = 0.0f,
    	    .anisotropyEnable = VK_TRUE,
    	    .maxAnisotropy = pdev_prop.limits.maxSamplerAnisotropy,
    	    .compareEnable = VK_FALSE,
    	    .compareOp = VK_COMPARE_OP_ALWAYS,
    	    .minLod = 0.0f,
    	    .maxLod = 0.0f,
    	    .borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE,
    	    .unnormalizedCoordinates = VK_FALSE,
    	};
    	assert(vkCreateSampler(vbc.device, &sampler_info, nullptr, &sampler) == VK_SUCCESS);
    }

    void create_rectangles() {
	const std::vector<Vertex> vertices = {
	    {{-0.5f, -0.5f, 0.0f}, 1.0f, {1.0f, 1.0f, 1.0f}, 0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
    	    {{0.5f,  -0.5f, 0.0f}, 0.0f, {1.0f, 1.0f, 1.0f}, 0.0f, {0.0f, 1.0f, 0.0f, 1.0f}},
	    {{0.5f,  0.5f,  0.0f}, 0.0f, {1.0f, 1.0f, 1.0f}, 1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
	    {{-0.5f, 0.5f,  0.0f}, 1.0f, {1.0f, 1.0f, 1.0f}, 1.0f, {1.0f, 1.0f, 0.0f, 1.0f}}
    	};
    	const std::vector<uint32_t> indices = {0,1,2,2,3,0};
    	rectangle = new Rectangle{&vbc, vertices, indices};
    	const std::vector<Vertex> vertices2 = {
	    {{-0.5f, -0.5f, -0.5f}, 1.0f, {1.0f, 1.0f, 1.0f}, 0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
    	    {{0.5f,  -0.5f, -0.5f}, 0.0f, {1.0f, 1.0f, 1.0f}, 0.0f, {0.0f, 1.0f, 0.0f, 1.0f}},
	    {{0.5f,  0.5f,  -0.5f}, 0.0f, {1.0f, 1.0f, 1.0f}, 1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
	    {{-0.5f, 0.5f,  -0.5f}, 1.0f, {1.0f, 1.0f, 1.0f}, 1.0f, {1.0f, 1.0f, 0.0f, 1.0f}}
    	};
    	rectangle2 = new Rectangle{&vbc, vertices2, indices};
    }

    void setup_descriptor_buffers() {
	VkDescriptorSetLayoutBinding binding {
    	    .binding = 0,
    	    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    	    .descriptorCount = 1,
    	    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    	};
    	VkDescriptorSetLayoutCreateInfo layout_info {
    	    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    	    .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
    	    .bindingCount = 1,
    	    .pBindings = &binding,
    	};
    	assert(vkCreateDescriptorSetLayout(vbc.device, &layout_info, nullptr,
		    &graphics_descriptor.layout) == VK_SUCCESS);
    	VkPhysicalDeviceProperties2 properties = {
    	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
    	    &descriptor_buffer_prop,
    	};
    	vkGetPhysicalDeviceProperties2(vbc.physical_device, &properties);
    	size_t layout_size = 0;
    	vkGetDescriptorSetLayoutSizeEXT(vbc.device, graphics_descriptor.layout, &layout_size);
    	layout_size = aligned_size(layout_size, descriptor_buffer_prop.descriptorBufferOffsetAlignment);
    	size_t binding_offset = 0;
    	vkGetDescriptorSetLayoutBindingOffsetEXT(vbc.device, graphics_descriptor.layout,
		0, &binding_offset);
    	graphics_descriptor.buffer.create(texture.extent.depth*texture.extent.width
		*texture.extent.height*4*layout_size,
    	        VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT
		| VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT 
		| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    	assert(graphics_descriptor.buffer.all_valid());
    	char* buffer_data = (char*)graphics_descriptor.buffer.info.pMappedData;
    	VkDescriptorImageInfo image_info {
    	    .sampler = sampler,
    	    .imageView = texture.image_view,
    	    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    	};
    	VkDescriptorGetInfoEXT image_desc_info {
    	    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
    	    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    	    .data {
    	        .pCombinedImageSampler = &image_info,
    	    },
    	};
    	vkGetDescriptorEXT(vbc.device, &image_desc_info,
		descriptor_buffer_prop.combinedImageSamplerDescriptorSize,
		buffer_data+0 * layout_size + binding_offset);
    	VkBufferDeviceAddressInfo addr_info {
    	    .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
    	    .buffer = graphics_descriptor.buffer.buffer,
    	};
    	graphics_descriptor.address = vkGetBufferDeviceAddress(vbc.device, &addr_info);
    	graphics_descriptor.binding_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
    	graphics_descriptor.binding_info.address = graphics_descriptor.address;
    	graphics_descriptor.binding_info.usage = VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT
	    | VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
    	graphics_descriptor.buffer_index = 0;
	graphics_descriptor.binding_offset = binding_offset;

	VkDescriptorSetLayoutBinding compute_binding {
    	    .binding = 0,
    	    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    	    .descriptorCount = 1,
    	    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    	};
    	VkDescriptorSetLayoutCreateInfo compute_layout_info {
    	    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    	    .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
    	    .bindingCount = 1,
    	    .pBindings = &compute_binding,
    	};
    	assert(vkCreateDescriptorSetLayout(vbc.device, &compute_layout_info,
		    nullptr, &compute_descriptor.layout) == VK_SUCCESS);
    	size_t compute_dlayout_size = 0;
    	vkGetDescriptorSetLayoutSizeEXT(vbc.device, compute_descriptor.layout, &compute_dlayout_size);
    	compute_dlayout_size = aligned_size(compute_dlayout_size, descriptor_buffer_prop.descriptorBufferOffsetAlignment);
    	size_t cbinding_offset = 0;
    	vkGetDescriptorSetLayoutBindingOffsetEXT(vbc.device, compute_descriptor.layout,
		0, &cbinding_offset);
    	compute_descriptor.buffer.create(vbc.swapchain_extent.width*vbc.swapchain_extent.height
		*4*compute_dlayout_size, VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT
		| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    	assert(compute_descriptor.buffer.all_valid());
    	char* cbuffer_data = (char*)compute_descriptor.buffer.info.pMappedData;
    	VkDescriptorImageInfo cimage_info {
    	    .imageView = comp_image.image_view,
    	    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    	};
    	VkDescriptorGetInfoEXT cimage_desc_info {
    	    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
    	    .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    	    .data {
    	        .pStorageImage = &cimage_info,
    	    },
    	};
    	vkGetDescriptorEXT(vbc.device, &cimage_desc_info, descriptor_buffer_prop.storageImageDescriptorSize, cbuffer_data);
    	VkBufferDeviceAddressInfo caddr_info {
    	    .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
    	    .buffer = compute_descriptor.buffer.buffer,
    	};
    	compute_descriptor.address = vkGetBufferDeviceAddress(vbc.device, &caddr_info);
    	compute_descriptor.binding_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
    	compute_descriptor.binding_info.address = compute_descriptor.address;
    	compute_descriptor.binding_info.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
	compute_descriptor.buffer_index = 0;
	compute_descriptor.binding_offset = cbinding_offset;
    }

    void create_pipelines() {
	graphics_pipeline.set_front_face(VK_FRONT_FACE_COUNTER_CLOCKWISE);
    	graphics_pipeline.enable_depth_test();
    	graphics_pipeline.add_shader("../samples/shaders/full_vert.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    	graphics_pipeline.add_shader("../samples/shaders/textured.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    	graphics_pipeline.add_push_constant(sizeof(PushConstants), VK_SHADER_STAGE_VERTEX_BIT);
    	graphics_pipeline.add_descriptor_set_layout(graphics_descriptor.layout);
    	VkFormat color_format[1] = {vbc.swapchain_format};
    	VkPipelineRenderingCreateInfo rendering_info {
    	    .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
    	    .colorAttachmentCount = 1,
    	    .pColorAttachmentFormats = color_format,
    	    .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT,
    	};
    	graphics_pipeline.create(&rendering_info, VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);
    	assert(graphics_pipeline.all_valid());

	VkPipelineLayoutCreateInfo compute_layout_inf {
    	    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    	    .setLayoutCount = 1,
    	    .pSetLayouts = &compute_descriptor.layout, 
    	};
    	vkCreatePipelineLayout(vbc.device, &compute_layout_inf, nullptr, &compute_layout);
    	auto comp_shader = vb::create_shader_module(vbc.device, "../samples/shaders/grad.comp.spv");
    	assert(comp_shader);
    	VkPipelineShaderStageCreateInfo stage = {
    	    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    	    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
    	    .module = comp_shader,
    	    .pName = "main",
    	};
    	VkComputePipelineCreateInfo compute_info = {
    	    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    	    .flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
    	    .stage = stage,
    	    .layout = compute_layout,
    	};
    	assert(vkCreateComputePipelines(vbc.device, 0, 1, &compute_info, nullptr, &compute_pipeline) == VK_SUCCESS);
    	vkDestroyShaderModule(vbc.device, comp_shader, nullptr);
    }

    VkImageLayout render(VkCommandBuffer cmd, VkImageLayout input_layout, uint32_t index) {
	VkClearValue color[2] {
	    {.color = {0.0f, 0.0f, 0.0f, 1.0f}},
	    {.depthStencil = {1.0f, 0}},
	};
	vb::transition_image(cmd, comp_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline);
	vkCmdBindDescriptorBuffersEXT(cmd, 1, &compute_descriptor.binding_info);
	vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
		compute_layout, 0, 1, &compute_descriptor.buffer_index, &compute_descriptor.binding_offset);
	vkCmdDispatch(cmd, ceil(vbc.swapchain_extent.width/16.0),
		ceil(vbc.swapchain_extent.height/16.0), 1);
	vb::transition_image(cmd, comp_image.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	vb::transition_image(cmd, vbc.swapchain_images[index], input_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	vb::blit_image(cmd, comp_image.image, vbc.swapchain_images[index],
		comp_image.extent, {vbc.swapchain_extent.width, vbc.swapchain_extent.height, 1});
	vb::transition_image(cmd, vbc.swapchain_images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	vb::transition_image(cmd, depth_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
	VkRenderingAttachmentInfo color_info = {
	    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
	    .imageView = vbc.swapchain_image_views[index],
	    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	    .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
	    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
	};
	VkRenderingAttachmentInfo depth_info = {
	    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
	    .imageView = depth_image.image_view,
	    .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
	    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
	    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
	    .clearValue = color[1],
	};
	VkRenderingInfo render_info = {
	    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
	    .renderArea = {{0,0}, vbc.swapchain_extent},
	    .layerCount = 1,
	    .colorAttachmentCount = 1,
	    .pColorAttachments = &color_info,
	    .pDepthAttachment = &depth_info,
	};
	vkCmdBeginRendering(cmd, &render_info);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline.pipeline);
	VkViewport viewport {0.0f, 0.0f, (float)vbc.swapchain_extent.width, 
	    (float)vbc.swapchain_extent.height};
	vkCmdSetViewport(cmd, 0, 1, &viewport);
	VkRect2D scissor {{0,0}, vbc.swapchain_extent};
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	vkCmdBindDescriptorBuffersEXT(cmd, 1, &graphics_descriptor.binding_info);
	vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 
		graphics_pipeline.layout, 0, 1, &graphics_descriptor.buffer_index, &graphics_descriptor.binding_offset);
	glm::mat4 view = glm::lookAt(glm::vec3(2.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	glm::mat4 proj = glm::perspective(glm::radians(45.0f),
		(float)vbc.swapchain_extent.width/(float)vbc.swapchain_extent.height, 0.1f, 100.0f);
	proj[1][1] *= -1;
	auto push_constants = PushConstants{proj * view, rectangle->vertex_buffer_address};
	vkCmdPushConstants(cmd, graphics_pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push_constants);
	vkCmdBindIndexBuffer(cmd, rectangle->index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(cmd, 6, 2, 0, 0, 0);
	push_constants = PushConstants{proj * view, rectangle2->vertex_buffer_address};
	vkCmdPushConstants(cmd, graphics_pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push_constants);
	vkCmdBindIndexBuffer(cmd, rectangle2->index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(cmd, 6, 2, 0, 0, 0);
	vkCmdEndRendering(cmd);
	return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
};

int main(int argc, char** argv) {
    ComputeDescriptorBuffers app {};
    app.run();
}
