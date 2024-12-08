#pragma once

#define GLM_FORCE_RADIANS
#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <chrono>
#include <algorithm>
#include <format>
#include <vb.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

struct InteractiveCamera {
    glm::vec3 velocity {0.0f};
    glm::vec3 position {0.0f};
    float aspect_ratio {1280.0f/720.0f};
    float fov {60.0f};
    float near {1000.0f};
    float far {0.1f};

    glm::vec2 pitch_limit {-2.0f, 2.0f};
    glm::vec3 pitch_axis {1.0f, 0.0f, 0.0f};
    glm::vec3 yaw_axis {0.0f, -1.0f, 0.0f};
    float pitch;
    float yaw;
    float move_speed {0.1f};
    float mouse_sensitivity {0.01f};
    bool use {true};
    bool lock {false};

    glm::mat4 rotation() {
        return glm::mat4(glm::angleAxis(yaw, yaw_axis) * glm::angleAxis(pitch, pitch_axis));
    }
    
    glm::mat4 view() {
        return glm::inverse(glm::translate(glm::mat4(1.0f), position) * rotation());
    }
    
    glm::mat4 projection() {
        auto proj = glm::perspective(glm::radians(fov), aspect_ratio, near, far);
	proj[1][1] *= -1;
	return proj;
    }
    
    void update_mouse(int x, int y) {
        yaw += x * mouse_sensitivity;
        pitch = std::clamp(pitch - y * mouse_sensitivity, pitch_limit.x, pitch_limit.y);
    }
    
    void update(const float dt) {
        position += glm::vec3(rotation() * glm::vec4(velocity * move_speed * dt, 0.0f));
    }

    void handle_event(const SDL_Event& event) {
        if(event.type == SDL_EVENT_KEY_DOWN && event.key.repeat == false) {
           if(event.key.key == SDLK_W) velocity.z = -1.0f;
           if(event.key.key == SDLK_S) velocity.z =  1.0f;
           if(event.key.key == SDLK_A) velocity.x = -1.0f;
           if(event.key.key == SDLK_D) velocity.x =  1.0f;
        }
        if(event.type == SDL_EVENT_KEY_UP) {
           if(event.key.key == SDLK_W) velocity.z = 0.0f;
           if(event.key.key == SDLK_S) velocity.z = 0.0f;
           if(event.key.key == SDLK_A) velocity.x = 0.0f;
           if(event.key.key == SDLK_D) velocity.x = 0.0f;
        }
        if(event.type == SDL_EVENT_MOUSE_MOTION)
            update_mouse(event.motion.xrel, event.motion.yrel);
    }
};

struct App {
    vb::Context vbc;
    uint32_t width;
    uint32_t height;

    struct {
        uint64_t fps;
        float frametime;
        uint64_t triangles;
        uint64_t drawcalls;
        float update_time;
        float draw_time;
    } stats;

    bool running {true};
    bool resize {false};

    vb::CommandPool frames_cmdpool {&vbc};
    struct Frame {
	VkCommandBuffer cmd;
	VkSemaphore image_available;
	VkSemaphore finish_render;
	VkFence render;
    };
    std::vector<Frame> frames;
    uint8_t frame_index;

    vb::DescriptorPool imgui_descriptor_pool {&vbc};

    vb::QueueIndex* queue;
    vb::CommandPool cmdpool {&vbc};
    VkCommandBuffer global_cmd_buffer;

    float aspect_ratio {0.0f};
    VkExtent2D render_extent;
    vb::Image render_target {&vbc};
    vb::Image depth_target {&vbc};

    InteractiveCamera interactive_camera;

    App() {};
    void create(vb::ContextInstanceWindowInfo& window_info,
	    vb::ContextDeviceInfo& device_info, vb::ContextSwapchainInfo& swapchain_info,
	    VmaAllocatorCreateFlags allocator_flags) {
	width = window_info.width;
	height = window_info.height;
	assert(vbc.init());
	assert(vbc.create_instance_window(window_info));
	assert(vbc.create_device(device_info));
	assert(vbc.create_surface_swapchain(swapchain_info));
	assert(vbc.init_vma(allocator_flags));
	queue = vbc.find_queue(vb::Queue::Graphics);
	assert(queue);
	cmdpool.create(queue->index);
	assert(cmdpool.all_valid());
	global_cmd_buffer = cmdpool.allocate();
	assert(vbc.init_command_submitter(global_cmd_buffer, queue->queue, queue->index));
	init_frames();
	init_imgui();
	vbc.set_resize_callback([&]() {recreate_targets();});
	create_target_images();
	SDL_SetWindowRelativeMouseMode(vbc.window, true);
    }

    ~App() {
	destroy_target_images();
	cmdpool.clean();
	ImGui_ImplVulkan_Shutdown();
	imgui_descriptor_pool.clean();
	for(auto& frame: frames) {
	    vkDestroyFence(vbc.device, frame.render, nullptr);
	    vkDestroySemaphore(vbc.device, frame.finish_render, nullptr);
	    vkDestroySemaphore(vbc.device, frame.image_available, nullptr);
	}
	frames_cmdpool.clean();
    }

    void init_frames() {
	frames_cmdpool.create(queue->index);
	assert(frames_cmdpool.all_valid());
	frames.resize(vbc.swapchain_image_views.size());
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
    }

    void create_target_images() {
	aspect_ratio = (float)height / (float)width;
	render_extent.width = std::min(width, vbc.swapchain_extent.width);
	render_extent.height = std::min(width,
		(uint32_t)(aspect_ratio * (float)vbc.swapchain_extent.width));

	render_target.create({render_extent.width, render_extent.height, 1},
		false, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_B8G8R8A8_SRGB,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	assert(render_target.all_valid());

	depth_target.create({render_extent.width, render_extent.height, 1},
		false, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_D32_SFLOAT,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	assert(depth_target.all_valid());
    } 

    void destroy_target_images() { render_target.clean(); depth_target.clean(); }
    void recreate_targets() {
	vbc.recreate_swapchain([&](uint32_t w, uint32_t h) {destroy_target_images();});
	create_target_images();
	resize = false;
    }

    void init_imgui() {
	VkDescriptorPoolSize pool_sizes[] = {
    	    {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
    	    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
    	    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
    	    {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
    	    {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
    	    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
    	    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
    	    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
    	    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
    	    {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
    	};
	imgui_descriptor_pool.create(pool_sizes, 1000,
		VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
	ImGui::CreateContext();
    	assert(ImGui_ImplSDL3_InitForVulkan(vbc.window));
    	ImGui_ImplVulkan_InitInfo init_info = {
    	    .Instance = vbc.instance,
    	    .PhysicalDevice = vbc.physical_device,
    	    .Device = vbc.device,
    	    .Queue = queue->queue,
    	    .DescriptorPool = imgui_descriptor_pool.pool,
    	    .MinImageCount = (uint32_t)vbc.swapchain_images.size(),
    	    .ImageCount = (uint32_t)vbc.swapchain_images.size(),
    	    .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
    	    .UseDynamicRendering = true,
    	    .PipelineRenderingCreateInfo = {
    	        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
    	        .colorAttachmentCount = 1,
    	        .pColorAttachmentFormats = &vbc.swapchain_format,
    	    },
    	};
    	assert(ImGui_ImplVulkan_Init(&init_info));
    	assert(ImGui_ImplVulkan_CreateFontsTexture());
    }

    VkImageLayout render_imgui(VkCommandBuffer cmd, 
	    VkImageLayout input_layout, uint32_t index) {
	vb::transition_image(cmd, vbc.swapchain_images[index],
		input_layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingAttachmentInfo color_attach = {
	    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
	    .imageView = vbc.swapchain_image_views[index],
	    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	    .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
	    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
	};
	VkRenderingInfo rendering = {
	    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
	    .renderArea = {{0,0}, vbc.swapchain_extent},
	    .layerCount = 1,
	    .colorAttachmentCount = 1,
	    .pColorAttachments = &color_attach,
	};
	vkCmdBeginRendering(cmd, &rendering);
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
	vkCmdEndRendering(cmd);
	return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    void imgui_interface() {
	//ImGui::ShowDemoWindow();
	ImGui::SetNextWindowSize(ImVec2(300,600));
	ImGui::Begin("vkgfxrenderer");
	ImGui::Text("[Q] lock camera");
	// STATISTICS
	ImGui::SeparatorText("statistics");
	ImGui::Text("fps:        %ld", stats.fps);
	ImGui::Text("frame time: %.3f ms", stats.frametime);
	ImGui::Text("draw time:  %.3f ms", stats.draw_time);
	ImGui::Text("triangles:  %ld", stats.triangles);
	ImGui::Text("draw calls: %ld", stats.drawcalls);
	ImGui::Separator();
	// SCREENSHOT
	static std::string screenshot_filename = "";
	if(ImGui::Button("screenshot")) {
	    screenshot_filename = save_screenshot(render_target.image);
	    ImGui::OpenPopup("save_screenshot");
	}
	if(ImGui::BeginPopupContextItem("save_screenshot")) {
	    ImGui::Text("saved to %s", screenshot_filename.c_str());
	    if(ImGui::Button("close")) ImGui::CloseCurrentPopup();
	    ImGui::EndPopup();
	}
	ImGui::End();
    }

    void run() {
        SDL_Event event;
	SDL_ShowWindow(vbc.window);
        while(running) {
            while(SDL_PollEvent(&event) != 0) {
                switch(event.type) {
		    case SDL_EVENT_QUIT:
            	        running = false;
            	        break;
            	    case SDL_EVENT_WINDOW_RESIZED: case SDL_EVENT_WINDOW_MAXIMIZED:
            	    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
		    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            	        resize = true;
            	        break;
            	    case SDL_EVENT_WINDOW_HIDDEN: case SDL_EVENT_WINDOW_MINIMIZED:
		    case SDL_EVENT_WINDOW_OCCLUDED:
            	        SDL_WaitEvent(&event);
            	        break;
		    case SDL_EVENT_KEY_UP:
			if(event.key.key == SDLK_Q) {
			    interactive_camera.lock = interactive_camera.lock ? false : true;
			    SDL_SetWindowRelativeMouseMode(vbc.window, interactive_camera.lock ? false : true);
			}
			break;
                }
		if(!interactive_camera.lock) interactive_camera.handle_event(event);
		ImGui_ImplSDL3_ProcessEvent(&event);
            }

	    if(resize) recreate_targets();

	    auto start = std::chrono::high_resolution_clock::now();

	    ImGui_ImplVulkan_NewFrame();
	    ImGui_ImplSDL3_NewFrame();
	    ImGui::NewFrame();
	    imgui_interface();
	    ImGui::Render();

	    auto frame = &frames[frame_index%frames.size()];
	    assert(vkWaitForFences(vbc.device, 1, &frame->render, VK_TRUE, UINT64_MAX)
		    == VK_SUCCESS);
	    auto next = vbc.acquire_next_image(frame->image_available);
	    if(!next.has_value()) continue;
	    uint32_t index = next.value();
	    vkResetFences(vbc.device, 1, &frame->render);
	    vkResetCommandBuffer(frame->cmd, 0);
	    VkCommandBufferBeginInfo begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	    };
	    assert(vkBeginCommandBuffer(frame->cmd, &begin) == VK_SUCCESS);

	    auto layout = VK_IMAGE_LAYOUT_UNDEFINED;
	    stats.drawcalls = 0;
	    stats.triangles = 0;
	    auto draw_start = std::chrono::high_resolution_clock::now();
	    layout = render(frame->cmd, layout, index);
    	    auto draw_end = std::chrono::high_resolution_clock::now();
	    auto draw_elapsed = std::chrono::duration_cast<std::chrono::microseconds>
		(draw_end - draw_start);
	    stats.draw_time = draw_elapsed.count() / 1000.0f;
	    layout = render_imgui(frame->cmd, layout, index);

	    vb::transition_image(frame->cmd, vbc.swapchain_images[index],
		    layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	    assert(vkEndCommandBuffer(frame->cmd) == VK_SUCCESS);

	    VkPipelineStageFlags mask[1] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	    VkSubmitInfo submit = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &frame->image_available,
		.pWaitDstStageMask = mask,
		.commandBufferCount = 1,
		.pCommandBuffers = &frame->cmd,
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &frame->finish_render,
	    };
	    assert(vkQueueSubmit(queue->queue, 1, &submit, frame->render)
		    == VK_SUCCESS);

	    VkPresentInfoKHR present = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &frame->finish_render,
		.swapchainCount = 1,
		.pSwapchains = &vbc.swapchain,
		.pImageIndices = &index,
	    };
	    vkQueuePresentKHR(queue->queue, &present);
	    frame_index++;

	    auto end = std::chrono::high_resolution_clock::now();
	    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	    stats.frametime = elapsed.count() / 1000.0f;
	    stats.fps = (float)(1.0f / stats.frametime) * 1000.0f;
        }
        vkDeviceWaitIdle(vbc.device);
    }

    virtual VkImageLayout render(VkCommandBuffer cmd, VkImageLayout input_layout, uint32_t index) = 0;

    std::string save_screenshot(VkImage source) {
	vb::log("Saving screenshot of render target...");
	VkFormatProperties format_p;
	VkExtent3D size = {render_extent.width, render_extent.height, 1};
	VkImageCreateInfo image_info = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
	    .imageType = VK_IMAGE_TYPE_2D,
	    .format = VK_FORMAT_R8G8B8A8_SRGB,
	    .extent = size,
	    .mipLevels = 1,
	    .arrayLayers = 1,
	    .samples = VK_SAMPLE_COUNT_1_BIT,
	    .tiling = VK_IMAGE_TILING_LINEAR,
	    .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
	};
	VmaAllocationCreateInfo allocation_info = {
	    .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
		    | VMA_ALLOCATION_CREATE_MAPPED_BIT,
	    .usage = VMA_MEMORY_USAGE_AUTO,
	    .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
		| VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
	};
	VkImage image;
	VmaAllocation allocation;
	VmaAllocationInfo info;
	if(vmaCreateImage(vbc.allocator, &image_info, &allocation_info,
		    &image, &allocation, &info) != VK_SUCCESS) {
	    vb::log("Failed to create VkImage for screenshot target");
	    return "";
	}
	vbc.submit_command_to_queue([&](VkCommandBuffer cmd) {
		vb::transition_image(cmd, source,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		vb::transition_image(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		vb::blit_image(cmd, source, image, size, size);
		vb::transition_image(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL);
	});
	VkImageSubresource subresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
	VkSubresourceLayout subres_layout;
	vkGetImageSubresourceLayout(vbc.device, image, &subresource, &subres_layout);
	char* data = (char*)info.pMappedData;
	data += subres_layout.offset;
	auto now_tp = std::chrono::system_clock::now();
	auto filename = std::format("{:%d%m%Y%H%M%OS}.png", now_tp);
	assert(stbi_write_png(filename.c_str(), size.width, size.height, 4, data,
		    subres_layout.rowPitch) != 0);
	vmaDestroyImage(vbc.allocator, image, allocation);
	vb::log(std::format("Saved screenshot: {}", filename));
	return filename;
    }
};
