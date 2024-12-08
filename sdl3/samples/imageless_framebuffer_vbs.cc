#define GLM_FORCE_RADIANS
#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
#include <SDL3/SDL_events.h>
#include <vbs.h>
#include <vulkan/vulkan_core.h>
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

struct Frame {
    VkCommandBuffer cmd;
    VkSemaphore image_available;
    VkSemaphore finish_render;
    VkFence render;
};

struct Rectangle {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vb::SharedBuffer vertex_buffer;
    vb::SharedBuffer index_buffer;
    VkDeviceAddress vertex_buffer_address;

    Rectangle(vb::Context* context, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
	: vertices{vertices}, indices{indices} {
	    const size_t vertices_size = sizeof(Vertex) * vertices.size();
	    vertex_buffer = vb::create_shared_buffer(context, vertices_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT 
		    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		    VMA_MEMORY_USAGE_GPU_ONLY);
	    assert(vertex_buffer->all_valid());
	    VkBufferDeviceAddressInfo address = {
    	        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
    	        .buffer = vertex_buffer->buffer,
    	    };
    	    vertex_buffer_address = vkGetBufferDeviceAddress(context->device, &address);

	    const size_t indices_size = sizeof(uint32_t) * indices.size();
	    index_buffer = vb::create_shared_buffer(context, indices_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT
		    | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
	    assert(index_buffer->all_valid());

	    auto staging_buffer = vb::Buffer{context};
    	    staging_buffer.create(vertices_size + indices_size,
		    VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
	    assert(staging_buffer.all_valid());
    	    memcpy(staging_buffer.info.pMappedData, vertices.data(), vertices_size);
    	    memcpy((char*)(staging_buffer.info.pMappedData)+vertices_size, indices.data(),
		    indices_size);
	    context->submit_command_to_queue([&](VkCommandBuffer cmd) {
    	        VkBufferCopy copy = { .size = vertices_size };
    	        vkCmdCopyBuffer(cmd, staging_buffer.buffer, vertex_buffer->buffer, 1, &copy);
    	        VkBufferCopy copy2 = { .srcOffset = vertices_size, .size = indices_size };
    	        vkCmdCopyBuffer(cmd, staging_buffer.buffer, index_buffer->buffer, 1, &copy2);
    	    });
    	    staging_buffer.clean();
    }
};

int main(int argc, char** argv) {
    vb::ContextInstanceWindowInfo iwinfo = { .vulkan_api = VK_API_VERSION_1_3 };
    iwinfo.require_debug();
    vb::ContextDeviceInfo dinfo = {
	.vk10features = {
	    .samplerAnisotropy = VK_TRUE,
	},
	.vk12features = {
	    .imagelessFramebuffer = VK_TRUE,
	    .separateDepthStencilLayouts = VK_TRUE,
	    .bufferDeviceAddress = VK_TRUE,
	},
	.vk13features = {
	    .synchronization2 = VK_TRUE,
	},
    };
    vb::ContextSwapchainInfo sinfo = {};

    auto vbc = vb::create_unique_context(iwinfo, dinfo, sinfo, VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT);

    vb::QueueIndex* graphics_queue = vbc->find_queue(vb::Queue::Graphics);
    assert(graphics_queue);

    auto cmdpool = vb::create_shared_command_pool(vbc.get(), graphics_queue->index);
    auto cmdbf = cmdpool->allocate();
    assert(vbc->init_command_submitter(cmdbf, graphics_queue->queue, graphics_queue->index));

    auto texture = vb::create_shared_image(vbc.get(), "../samples/textures/texture.jpg");
    assert(texture->all_valid());

    VkPhysicalDeviceProperties pdev_prop{};
    vkGetPhysicalDeviceProperties(vbc->physical_device, &pdev_prop);
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
    VkSampler sampler;
    assert(vkCreateSampler(vbc->device, &sampler_info, nullptr, &sampler) == VK_SUCCESS);

    const std::vector<Vertex> vertices = {
    	{{-0.5f, -0.5f, 0.0f}, 1.0f, {1.0f, 1.0f, 1.0f}, 0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
	{{0.5f,  -0.5f, 0.0f}, 0.0f, {1.0f, 1.0f, 1.0f}, 0.0f, {0.0f, 1.0f, 0.0f, 1.0f}},
    	{{0.5f,  0.5f,  0.0f}, 0.0f, {1.0f, 1.0f, 1.0f}, 1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
    	{{-0.5f, 0.5f,  0.0f}, 1.0f, {1.0f, 1.0f, 1.0f}, 1.0f, {1.0f, 1.0f, 0.0f, 1.0f}}
    };
    const std::vector<uint32_t> indices = {0,1,2,2,3,0};
    Rectangle rectangle {vbc.get(), vertices, indices};
    const std::vector<Vertex> vertices2 = {
    	{{-0.5f, -0.5f, -0.5f}, 1.0f, {1.0f, 1.0f, 1.0f}, 0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
	{{0.5f,  -0.5f, -0.5f}, 0.0f, {1.0f, 1.0f, 1.0f}, 0.0f, {0.0f, 1.0f, 0.0f, 1.0f}},
    	{{0.5f,  0.5f,  -0.5f}, 0.0f, {1.0f, 1.0f, 1.0f}, 1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
    	{{-0.5f, 0.5f,  -0.5f}, 1.0f, {1.0f, 1.0f, 1.0f}, 1.0f, {1.0f, 1.0f, 0.0f, 1.0f}}
    };
    Rectangle rectangle2 {vbc.get(), vertices2, indices};

    std::vector<VkDescriptorPoolSize> sizes {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2}};
    auto descriptor_builder = vb::create_shared_descriptor_pool(vbc.get(), sizes, 1);
    assert(descriptor_builder->all_valid());
    descriptor_builder->add_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	VK_SHADER_STAGE_FRAGMENT_BIT, 0);
    auto layout = descriptor_builder->create_layout();
    assert(layout);
    auto set = descriptor_builder->create_set(layout);
    assert(set);

    VkDescriptorImageInfo image_info {
	.sampler = sampler,
	.imageView = texture->image_view,
	.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet descriptor_write {
	.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	.dstSet = set,
	.dstBinding = 0,
	.dstArrayElement = 0,
	.descriptorCount = 1,
	.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	.pImageInfo = &image_info,
    };
    vkUpdateDescriptorSets(vbc->device, 1, &descriptor_write, 0, nullptr);

    auto graphics_pipeline = vb::GraphicsPipeline{vbc.get()};
    graphics_pipeline.set_front_face(VK_FRONT_FACE_COUNTER_CLOCKWISE);
    graphics_pipeline.enable_depth_test();

    graphics_pipeline.add_shader("../samples/shaders/full_vert.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    graphics_pipeline.add_shader("../samples/shaders/textured.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    graphics_pipeline.add_push_constant(sizeof(PushConstants), VK_SHADER_STAGE_VERTEX_BIT);

    auto depth_image = vb::create_shared_image(vbc.get(), 
	{vbc->swapchain_extent.width, vbc->swapchain_extent.height, 1},
    	false, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_D32_SFLOAT,
	VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    assert(depth_image->all_valid());

    VkAttachmentDescription color_attachment {
	.format = vbc->swapchain_format,
	.samples = VK_SAMPLE_COUNT_1_BIT,
	.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
	.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
	.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    VkAttachmentReference color_attachment_ref {
	.attachment = 0,
	.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentDescription depth_attachment {
	.format = VK_FORMAT_D32_SFLOAT,
	.samples = VK_SAMPLE_COUNT_1_BIT,
	.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
	.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
	.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
	.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
	.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	.finalLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference depth_attachment_ref {
	.attachment = 1,
	.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass {
	.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
	.colorAttachmentCount = 1,
	.pColorAttachments = &color_attachment_ref,
	.pDepthStencilAttachment = &depth_attachment_ref,
    };

    VkAttachmentDescription attachments[2] = {color_attachment, depth_attachment};
    VkRenderPassCreateInfo render_pass_info {
	.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
	.attachmentCount = 2,
	.pAttachments = attachments,
	.subpassCount = 1,
	.pSubpasses = &subpass,
    };
    VkRenderPass render_pass;
    assert(vkCreateRenderPass(vbc->device, &render_pass_info, nullptr, &render_pass) == VK_SUCCESS);

    graphics_pipeline.set_render_pass(render_pass);
    graphics_pipeline.add_descriptor_set_layout(layout);
    graphics_pipeline.create();
    assert(graphics_pipeline.all_valid());

    VkFramebufferAttachmentImageInfo framebuffer_color_attachment_info {
	.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO,
	.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
	.width = vbc->swapchain_extent.width,
	.height = vbc->swapchain_extent.height,
	.layerCount = 1,
	.viewFormatCount = 1,
	.pViewFormats = &vbc->swapchain_format,
    };

    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
    VkFramebufferAttachmentImageInfo framebuffer_depth_attachment_info {
	.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO,
	.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
	.width = vbc->swapchain_extent.width,
	.height = vbc->swapchain_extent.height,
	.layerCount = 1,
	.viewFormatCount = 1,
	.pViewFormats = &depth_format,
    };

    VkFramebufferAttachmentImageInfo framebuffer_attachments[2] = {framebuffer_color_attachment_info, framebuffer_depth_attachment_info};

    VkFramebufferAttachmentsCreateInfo framebuffer_attachments_info {
	.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO,
	.attachmentImageInfoCount = 2,
	.pAttachmentImageInfos = framebuffer_attachments,
    };

    VkFramebufferCreateInfo framebuffer_info {
    	.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
	.pNext = &framebuffer_attachments_info,
	.flags = VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT,
	.renderPass = render_pass,
	.attachmentCount = 2,
	.width = vbc->swapchain_extent.width,
	.height = vbc->swapchain_extent.height,
	.layers = 1,
    };
    VkFramebuffer framebuffer;
    assert(vkCreateFramebuffer(vbc->device, &framebuffer_info, nullptr, &framebuffer) == VK_SUCCESS);

    auto frames_cmdpool = vb::create_shared_command_pool(vbc.get(), graphics_queue->index);
    assert(frames_cmdpool->all_valid());
    std::vector<Frame> frames(vbc->swapchain_image_views.size());
    for(auto& frame: frames) {
        frame.cmd = frames_cmdpool->allocate();
        assert(frame.cmd);
        frame.finish_render = vb::create_semaphore(vbc->device);
        assert(frame.finish_render);
        frame.image_available = vb::create_semaphore(vbc->device);
        assert(frame.image_available);
        frame.render = vb::create_fence(vbc->device, VK_FENCE_CREATE_SIGNALED_BIT);
        assert(frame.render);
    }

    bool running = true;
    bool resize = false;
    uint8_t frame_index = 0;
    SDL_Event event;
    while(running) {
	while(SDL_PollEvent(&event) != 0) {
	    switch(event.type) {
		case SDL_EVENT_QUIT:
		    running = false;
		    break;
		case SDL_EVENT_WINDOW_RESIZED: case SDL_EVENT_WINDOW_MAXIMIZED:
		case SDL_EVENT_WINDOW_ENTER_FULLSCREEN: case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
		    resize = true;
		    break;
		case SDL_EVENT_WINDOW_HIDDEN: case SDL_EVENT_WINDOW_MINIMIZED: case SDL_EVENT_WINDOW_OCCLUDED:
		    SDL_WaitEvent(&event);
		    break;
	    }
	}

	if(resize) {
	    vbc->recreate_swapchain([&](uint32_t,uint32_t) {
		depth_image->clean();
		vkDestroyFramebuffer(vbc->device, framebuffer, nullptr);
	    });
	    depth_image->create({vbc->swapchain_extent.width, vbc->swapchain_extent.height, 1},
		false, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_D32_SFLOAT,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	    VkFramebufferAttachmentImageInfo framebuffer_color_attachment_info {
    	        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO,
    	        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    	        .width = vbc->swapchain_extent.width,
    	        .height = vbc->swapchain_extent.height,
    	        .layerCount = 1,
    	        .viewFormatCount = 1,
    	        .pViewFormats = &vbc->swapchain_format,
    	    };

    	    VkFramebufferAttachmentImageInfo framebuffer_depth_attachment_info {
    	        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO,
    	        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
    	        .width = vbc->swapchain_extent.width,
    	        .height = vbc->swapchain_extent.height,
    	        .layerCount = 1,
    	        .viewFormatCount = 1,
    	        .pViewFormats = &depth_format,
    	    };

    	    VkFramebufferAttachmentImageInfo framebuffer_attachments[2] = {framebuffer_color_attachment_info, framebuffer_depth_attachment_info};

    	    VkFramebufferAttachmentsCreateInfo framebuffer_attachments_info {
    	        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO,
    	        .attachmentImageInfoCount = 2,
    	        .pAttachmentImageInfos = framebuffer_attachments,
    	    };
	    VkFramebufferCreateInfo framebuffer_info {
    	    	.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    	        .pNext = &framebuffer_attachments_info,
    	        .flags = VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT,
    	        .renderPass = render_pass,
    	        .attachmentCount = 2,
    	        .width = vbc->swapchain_extent.width,
    	        .height = vbc->swapchain_extent.height,
    	        .layers = 1,
    	    };
    	    assert(vkCreateFramebuffer(vbc->device, &framebuffer_info, nullptr, &framebuffer) == VK_SUCCESS);
	}

	auto frame = &frames[frame_index%frames.size()];
	assert(vkWaitForFences(vbc->device, 1, &frame->render, VK_TRUE, UINT64_MAX)
		== VK_SUCCESS);
	auto next = vbc->acquire_next_image(frame->image_available);
	if(!next.has_value()) continue;
	uint32_t image_index = next.value();
	vkResetFences(vbc->device, 1, &frame->render);

 	vkResetCommandBuffer(frame->cmd, 0);
 	VkCommandBufferBeginInfo begin {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
 	assert(vkBeginCommandBuffer(frame->cmd, &begin) == VK_SUCCESS);

	VkClearValue color[2] {
	    {.color = {0.0f, 0.0f, 0.0f, 1.0f}},
	    {.depthStencil = {1.0f, 0}},
	};
	VkImageView views[2] = {vbc->swapchain_image_views[image_index], depth_image->image_view};
	VkRenderPassAttachmentBeginInfo render_begin_attachments {
	    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO,
	    .attachmentCount = 2,
	    .pAttachments = views,
	};
	VkRenderPassBeginInfo render_begin {
	    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
	    .pNext = &render_begin_attachments,
	    .renderPass = render_pass,
	    .framebuffer = framebuffer,
	    .renderArea = {{0,0}, vbc->swapchain_extent},
	    .clearValueCount = 2,
	    .pClearValues = color,
	};
	vkCmdBeginRenderPass(frame->cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(frame->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline.pipeline);
	VkViewport viewport {0.0f, 0.0f, (float)vbc->swapchain_extent.width, (float)vbc->swapchain_extent.height};
	vkCmdSetViewport(frame->cmd, 0, 1, &viewport);
	VkRect2D scissor {{0,0}, vbc->swapchain_extent};
	vkCmdSetScissor(frame->cmd, 0, 1, &scissor);

	vkCmdBindDescriptorSets(frame->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline.layout, 0, 1, &set, 0, nullptr);
	// glm::mat4 model = glm::mat4(1);
	// model = glm::rotate(model, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	glm::mat4 view = glm::lookAt(glm::vec3(2.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)vbc->swapchain_extent.width/(float)vbc->swapchain_extent.height, 0.1f, 100.0f);
	proj[1][1] *= -1;
	auto push_constants = PushConstants{proj * view, rectangle.vertex_buffer_address};
	vkCmdPushConstants(frame->cmd, graphics_pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push_constants);
	vkCmdBindIndexBuffer(frame->cmd, rectangle.index_buffer->buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(frame->cmd, (uint32_t)indices.size(), 2, 0, 0, 0);
	push_constants = PushConstants{proj * view, rectangle2.vertex_buffer_address};
	vkCmdPushConstants(frame->cmd, graphics_pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push_constants);
	vkCmdBindIndexBuffer(frame->cmd, rectangle2.index_buffer->buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(frame->cmd, (uint32_t)indices.size(), 2, 0, 0, 0);
	vkCmdEndRenderPass(frame->cmd);

	assert(vkEndCommandBuffer(frame->cmd) == VK_SUCCESS);
	VkCommandBufferSubmitInfo cmd_info = {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
	    .commandBuffer = frame->cmd,
	};
	VkSemaphoreSubmitInfo wait_info = {
	    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
	    .semaphore = frame->image_available,
	    .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
	};
	VkSemaphoreSubmitInfo signal_info = {
	    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
	    .semaphore = frame->finish_render,
	    .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
	};
 	VkSubmitInfo2 submit = {
 	    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
	    .waitSemaphoreInfoCount = 1,
	    .pWaitSemaphoreInfos = &wait_info,
	    .commandBufferInfoCount = 1,
	    .pCommandBufferInfos = &cmd_info,
	    .signalSemaphoreInfoCount = 1,
	    .pSignalSemaphoreInfos = &signal_info,
 	};
 	assert(vkQueueSubmit2(graphics_queue->queue, 1, &submit, frame->render) == VK_SUCCESS);
 
 	VkPresentInfoKHR present = {
 	    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
 	    .waitSemaphoreCount = 1,
 	    .pWaitSemaphores = &frame->finish_render,
 	    .swapchainCount = 1,
 	    .pSwapchains = &vbc->swapchain,
 	    .pImageIndices = &image_index,
 	};
 	vkQueuePresentKHR(graphics_queue->queue, &present);

	frame_index++;
    }
    vkDeviceWaitIdle(vbc->device);

    vkDestroyRenderPass(vbc->device, render_pass, nullptr);
    graphics_pipeline.clean();
    graphics_pipeline.clean_shaders();
    vkDestroySampler(vbc->device, sampler, nullptr);
    vkDestroyDescriptorSetLayout(vbc->device, layout, nullptr);
    vkDestroyFramebuffer(vbc->device, framebuffer, nullptr);
    for(auto& frame: frames) {
	vkDestroySemaphore(vbc->device, frame.finish_render, nullptr);
	vkDestroySemaphore(vbc->device, frame.image_available, nullptr);
	vkDestroyFence(vbc->device, frame.render, nullptr);
    }
}
