#include <SDL3/SDL_events.h>
#include <iterator>
#include <vb.h>
#include <vulkan/vulkan_core.h>
#include <fstream>

struct Frame {
    VkCommandBuffer cmd;
    VkSemaphore image_available;
    VkSemaphore finish_render;
    VkFence render;
};

int main(int argc, char** argv) {
    VkPhysicalDeviceShaderObjectFeaturesEXT shader_objects = {
	.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT,
	.shaderObject = VK_TRUE,
    };
    auto vbc = vb::Context();
    assert(vbc.init());
    vb::ContextInstanceWindowInfo iwinfo = {.vulkan_api = VK_API_VERSION_1_3};
    iwinfo.require_debug();
    assert(vbc.create_instance_window(iwinfo));
    vb::ContextDeviceInfo dinfo = {
	.required_extensions = {VK_EXT_SHADER_OBJECT_EXTENSION_NAME},
	.vk13features = {
	    .pNext = &shader_objects,
	    .dynamicRendering = VK_TRUE,
	},
    };
    assert(vbc.create_device(dinfo));
    vb::ContextSwapchainInfo sinfo = {};
    assert(vbc.create_surface_swapchain(sinfo));
    assert(vbc.init_vma());
    vb::QueueIndex* graphics_queue = vbc.find_queue(vb::Queue::Graphics);
    assert(graphics_queue);

    VkShaderEXT vertex_shader;
    std::ifstream file {"../samples/shaders/triangle.vert.spv", std::ios::binary};
    assert(file.is_open());
    std::vector<char> vshbuffer(std::istreambuf_iterator<char>(file), {});
    file.close();
    VkShaderCreateInfoEXT vshinfo = {
	.sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
	.flags = VK_SHADER_CREATE_LINK_STAGE_BIT_EXT,
	.stage = VK_SHADER_STAGE_VERTEX_BIT,
	.nextStage = VK_SHADER_STAGE_FRAGMENT_BIT,
	.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
	.codeSize = vshbuffer.size()*sizeof(char),
	.pCode = vshbuffer.data(),
	.pName = "main",
    };
    VkShaderEXT fragment_shader;
    std::ifstream ffile {"../samples/shaders/triangle.frag.spv", std::ios::binary};
    assert(ffile.is_open());
    std::vector<char> fshbuffer (std::istreambuf_iterator<char>(ffile), {});
    ffile.close();
    VkShaderCreateInfoEXT fshinfo = {
	.sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
	.flags = VK_SHADER_CREATE_LINK_STAGE_BIT_EXT,
	.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
	.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
	.codeSize = fshbuffer.size() * sizeof(char),
	.pCode = fshbuffer.data(),
	.pName = "main",
    };
    VkShaderCreateInfoEXT shinfos[2] = {vshinfo, fshinfo};
    VkShaderEXT shs[2] = {vertex_shader, fragment_shader};
    assert(vkCreateShadersEXT(vbc.device, 2, shinfos, nullptr, shs) == VK_SUCCESS);
    vertex_shader = shs[0];
    fragment_shader = shs[1];

    auto frames_cmdpool = vb::CommandPool(&vbc);
    frames_cmdpool.create(graphics_queue->index);
    assert(frames_cmdpool.all_valid());
    std::vector<Frame> frames(vbc.swapchain_image_views.size());
    for(auto& frame: frames) {
        frame.cmd = frames_cmdpool.allocate();
        assert(frame.cmd);
        frame.finish_render = vb::create_semaphore(vbc.device);
        assert(frame.finish_render);
        frame.image_available = vb::create_semaphore(vbc.device);
        assert(frame.image_available);
        frame.render = vb::create_fence(vbc.device, VK_FENCE_CREATE_SIGNALED_BIT);
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

	auto frame = &frames[frame_index%frames.size()];
	assert(vkWaitForFences(vbc.device, 1, &frame->render, VK_TRUE, UINT64_MAX)
		== VK_SUCCESS);
	auto next = vbc.acquire_next_image(frame->image_available);
	if(!next.has_value()) continue;
	uint32_t image_index = next.value();
	vkResetFences(vbc.device, 1, &frame->render);

 	vkResetCommandBuffer(frame->cmd, 0);
 	VkCommandBufferBeginInfo begin {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
 	assert(vkBeginCommandBuffer(frame->cmd, &begin) == VK_SUCCESS);

	VkViewport viewport {0.0f, 0.0f, (float)vbc.swapchain_extent.width, (float)vbc.swapchain_extent.height};
	VkRect2D scissor {{0,0}, vbc.swapchain_extent};
	vkCmdSetVertexInputEXT(frame->cmd, 0, nullptr, 0, nullptr);
	vkCmdSetViewportWithCountEXT(frame->cmd, 1, &viewport);
	vkCmdSetScissorWithCountEXT(frame->cmd, 1, &scissor);
	vkCmdSetRasterizerDiscardEnableEXT(frame->cmd, VK_FALSE);
	VkColorBlendEquationEXT ext {};
	vkCmdSetColorBlendEquationEXT(frame->cmd, 0, 1, &ext);
	vkCmdSetPrimitiveTopologyEXT(frame->cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	vkCmdSetPrimitiveRestartEnableEXT(frame->cmd, VK_FALSE);
	vkCmdSetRasterizationSamplesEXT(frame->cmd, VK_SAMPLE_COUNT_1_BIT);
	const VkSampleMask sample_mask = 0x1;
	vkCmdSetSampleMaskEXT(frame->cmd, VK_SAMPLE_COUNT_1_BIT, &sample_mask);
	vkCmdSetAlphaToCoverageEnableEXT(frame->cmd, VK_FALSE);
	vkCmdSetPolygonModeEXT(frame->cmd, VK_POLYGON_MODE_FILL);
	vkCmdSetCullModeEXT(frame->cmd, VK_CULL_MODE_NONE);
	vkCmdSetFrontFaceEXT(frame->cmd, VK_FRONT_FACE_CLOCKWISE);
	vkCmdSetDepthTestEnableEXT(frame->cmd, VK_FALSE);
	vkCmdSetDepthBiasEnableEXT(frame->cmd, VK_FALSE);
	vkCmdSetStencilTestEnableEXT(frame->cmd, VK_FALSE);
	vkCmdSetLogicOpEnableEXT(frame->cmd, VK_FALSE);
	vkCmdSetDepthWriteEnableEXT(frame->cmd, VK_FALSE);
	VkBool32 color_blend_enables[] = {VK_FALSE};
	vkCmdSetColorBlendEnableEXT(frame->cmd, 0, 1, color_blend_enables);
	VkColorComponentFlags color_component_flags[] = {VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_A_BIT};
	vkCmdSetColorWriteMaskEXT(frame->cmd, 0, 1, color_component_flags);
	vb::transition_image(frame->cmd, vbc.swapchain_images[image_index],
	    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	VkClearValue color[2] = {
	    {.color = {0.0f, 0.0f, 0.0f, 1.0f}},
	    {.depthStencil = {0.0f, 0}},
	};
	VkRenderingAttachmentInfo color_attach = {
	    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
	    .imageView = vbc.swapchain_image_views[image_index],
	    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
	    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
	    .clearValue = color[0],
	};
	VkRenderingInfo rendering = {
	    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
	    .renderArea = {{0,0}, vbc.swapchain_extent},
	    .layerCount = 1,
	    .colorAttachmentCount = 1,
	    .pColorAttachments = &color_attach,
	};
	vkCmdBeginRendering(frame->cmd, &rendering);

	VkShaderStageFlagBits stages[2] = {VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT};
	VkShaderEXT shaders[2] = {vertex_shader, fragment_shader};
	vkCmdBindShadersEXT(frame->cmd, 2, stages, shaders);
	vkCmdDraw(frame->cmd, 3, 1, 0, 0);

	vkCmdEndRendering(frame->cmd);

	vb::transition_image(frame->cmd, vbc.swapchain_images[image_index],
	    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	assert(vkEndCommandBuffer(frame->cmd) == VK_SUCCESS);
 	VkPipelineStageFlags wait[1] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
 	VkSubmitInfo submit = {
 	    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
 	    .waitSemaphoreCount = 1,
 	    .pWaitSemaphores = &frame->image_available,
 	    .pWaitDstStageMask = wait,
 	    .commandBufferCount = 1,
 	    .pCommandBuffers = &frame->cmd,
 	    .signalSemaphoreCount = 1,
 	    .pSignalSemaphores = &frame->finish_render,
 	};
 	assert(vkQueueSubmit(graphics_queue->queue, 1, &submit, frame->render) == VK_SUCCESS);
 
 	VkPresentInfoKHR present = {
 	    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
 	    .waitSemaphoreCount = 1,
 	    .pWaitSemaphores = &frame->finish_render,
 	    .swapchainCount = 1,
 	    .pSwapchains = &vbc.swapchain,
 	    .pImageIndices = &image_index,
 	};
 	vkQueuePresentKHR(graphics_queue->queue, &present);

	frame_index++;
    }
    vkDeviceWaitIdle(vbc.device);

    vkDestroyShaderEXT(vbc.device, vertex_shader, nullptr);
    vkDestroyShaderEXT(vbc.device, fragment_shader, nullptr);
    for(auto& frame: frames) {
	vkDestroySemaphore(vbc.device, frame.image_available, nullptr);
	vkDestroySemaphore(vbc.device, frame.finish_render, nullptr);
	vkDestroyFence(vbc.device, frame.render, nullptr);
    }
    frames_cmdpool.clean();
}
