#include <SDL3/SDL_events.h>
#include <vb.h>
#include <vulkan/vulkan_core.h>

struct Frame {
    VkCommandBuffer cmd;
    VkSemaphore image_available;
    VkSemaphore finish_render;
    VkFence render;
};

int main(int argc, char** argv) {
    auto vbc = vb::Context();
    assert(vbc.init());

    vb::ContextInstanceWindowInfo iwinfo = {};
    iwinfo.require_debug();
    assert(vbc.create_instance_window(iwinfo));

    vb::ContextDeviceInfo dinfo = {};
    assert(vbc.create_device(dinfo));

    vb::ContextSwapchainInfo sinfo = {};
    assert(vbc.create_surface_swapchain(sinfo));
    assert(vbc.init_vma());

    auto graphics_pipeline = vb::GraphicsPipeline{&vbc};
    graphics_pipeline.add_shader("../samples/shaders/triangle.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    graphics_pipeline.add_shader("../samples/shaders/triangle.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    VkAttachmentDescription color_attachment {
        .format = vbc.swapchain_format,
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
    VkSubpassDescription subpass {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_ref,
    };
    VkRenderPassCreateInfo render_pass_info {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };
    VkRenderPass render_pass;
    assert(vkCreateRenderPass(vbc.device, &render_pass_info, nullptr, &render_pass) == VK_SUCCESS);

    graphics_pipeline.set_render_pass(render_pass);
    graphics_pipeline.create();
    assert(graphics_pipeline.pipeline);

    VkFramebuffer framebuffers[vbc.swapchain_image_views.size()];
    for(size_t i = 0; i < vbc.swapchain_image_views.size(); i++) {
        VkImageView attachments[1] = {vbc.swapchain_image_views[i]};
        VkFramebufferCreateInfo framebuffer_info {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    	    .renderPass = render_pass,
    	    .attachmentCount = 1,
            .pAttachments = attachments,
    	    .width = vbc.swapchain_extent.width,
    	    .height = vbc.swapchain_extent.height,
    	    .layers = 1,
    	};
    	assert(vkCreateFramebuffer(vbc.device, &framebuffer_info, nullptr, &framebuffers[i]) == VK_SUCCESS);
    }

    vb::QueueIndex* graphics_queue = vbc.find_queue(vb::Queue::Graphics);
    assert(graphics_queue);
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

        VkClearValue color {{{0.0f, 0.0f, 0.0f, 1.0f}}};
        assert(framebuffers[image_index] != VK_NULL_HANDLE);
        VkRenderPassBeginInfo render_begin {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = render_pass,
            .framebuffer = framebuffers[image_index],
            .renderArea = {{0,0}, vbc.swapchain_extent},
            .clearValueCount = 1,
            .pClearValues = &color,
        };
        vkCmdBeginRenderPass(frame->cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(frame->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline.pipeline);
        VkViewport viewport {0.0f, 0.0f, (float)vbc.swapchain_extent.width, (float)vbc.swapchain_extent.height};
        vkCmdSetViewport(frame->cmd, 0, 1, &viewport);
        VkRect2D scissor {{0,0}, vbc.swapchain_extent};
        vkCmdSetScissor(frame->cmd, 0, 1, &scissor);
        vkCmdDraw(frame->cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(frame->cmd);

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

    vkDestroyRenderPass(vbc.device, render_pass, nullptr);
    graphics_pipeline.clean();
    graphics_pipeline.clean_shaders();
    for(size_t i=0; i<vbc.swapchain_image_views.size(); i++) vkDestroyFramebuffer(vbc.device, framebuffers[i], nullptr);
    for(auto& frame: frames) {
        vkDestroySemaphore(vbc.device, frame.image_available, nullptr);
        vkDestroySemaphore(vbc.device, frame.finish_render, nullptr);
        vkDestroyFence(vbc.device, frame.render, nullptr);
    }
    frames_cmdpool.clean();
}
