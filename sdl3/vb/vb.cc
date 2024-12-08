#include <algorithm>
#define VK_NO_PROTOTYPES
#define VOLK_IMPLEMENTATION
#include <volk.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <set>
#include <format>
#include <fstream>
#include <SDL3/SDL_vulkan.h>
#include <unistd.h>
#include <math.h>
#include <vb.h>

namespace vb {
    std::vector<std::string> loop_through_vector(const std::vector<std::string>& requested,
	    const std::vector<std::string>& available) {
	std::vector<std::string> found;
	for(auto& r: requested) for(auto& a: available) if(r == a) found.push_back(a);
	return found;
    }

    std::vector<const char*> combine_vectors(const std::vector<std::string>& one,
	    const std::vector<std::string>& two) {
	std::vector<const char*> new_v;
	for(auto& o: one) new_v.push_back(o.c_str());
	for(auto& t: two) new_v.push_back(t.c_str());
	return new_v;
    }


    VkQueueFlags QueueIndex::queue_to_flag(const Queue& queue) {
	switch(queue) {
	    case Queue::Graphics: return VK_QUEUE_GRAPHICS_BIT;
	    case Queue::Compute: return VK_QUEUE_COMPUTE_BIT;
	    case Queue::Transfer: return VK_QUEUE_TRANSFER_BIT;
	    default: return 0;
	}
    }

    Context::~Context() {
	if(command_submitter.has_value()) vkDestroyFence(device, command_submitter->fence, nullptr);
	if(allocator != VK_NULL_HANDLE) vmaDestroyAllocator(allocator);
	if(swapchain != VK_NULL_HANDLE) destroy_swapchain();
	if(device != VK_NULL_HANDLE) vkDestroyDevice(device, nullptr);
	if(surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance, surface, nullptr);
	if(debug_messenger != VK_NULL_HANDLE)
	    vkDestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
	if(instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
	if(window) SDL_DestroyWindow(window);
	SDL_Quit();
    }

    bool Context::init(SDL_InitFlags flags) {
	if(volkInitialize() != VK_SUCCESS) return false;
	if(!(flags & SDL_INIT_VIDEO)) flags |= SDL_INIT_VIDEO;
	return SDL_Init(flags);
    }

    bool Context::create_instance_window(ContextInstanceWindowInfo& info) {
	if(!(info.window_flags & SDL_WINDOW_VULKAN)) info.window_flags |= SDL_WINDOW_VULKAN;
	window = SDL_CreateWindow(info.title.c_str(), info.width, info.height, info.window_flags);
	if(!window) return false;
	SDL_SetWindowMinimumSize(window, info.width, info.height);

	uint32_t extension_count = 0;
	auto sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
	if(!sdl_extensions) return false;
	std::vector<const char*> extensions {sdl_extensions, sdl_extensions + extension_count};

	std::vector<std::string> available_extensions;
	for(auto& ext: extensions) available_extensions.push_back(ext);
	{
	    uint32_t count;
	    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
	    VkExtensionProperties instance_ext[count];
	    vkEnumerateInstanceExtensionProperties(nullptr, &count, instance_ext);
	    for(auto& ext: instance_ext) available_extensions.push_back(ext.extensionName);
	}

	auto found_opt_extensions = loop_through_vector(info.optional_extensions,
		available_extensions);
	auto found_req_extensions = loop_through_vector(info.required_extensions,
		available_extensions);
	if(found_req_extensions.size() != info.required_extensions.size()) return false;
	auto request_extensions = combine_vectors(found_opt_extensions, found_req_extensions);
	request_extensions.insert(request_extensions.end(), extensions.begin(), extensions.end());

	std::vector<std::string> available_layers;
	{
	    uint32_t count;
	    vkEnumerateInstanceLayerProperties(&count, nullptr);
	    VkLayerProperties inst_layers[count];
	    vkEnumerateInstanceLayerProperties(&count, inst_layers);
	    for(auto& layer: inst_layers) available_layers.push_back(layer.layerName);
	}
	auto found_opt_layers = loop_through_vector(info.optional_layers, available_layers);
	auto found_req_layers = loop_through_vector(info.required_layers, available_layers);
	if(found_req_layers.size() != info.required_layers.size()) return false;
	auto request_layers = combine_vectors(found_opt_layers, found_req_layers);

	VkApplicationInfo app = {
    	    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
	    .pApplicationName = info.title.c_str(),
    	    .apiVersion = info.vulkan_api,
	};
	VkInstanceCreateInfo inst_info = {
	    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
	    .pNext = info.pNext,
	    .flags = info.instance_flags,
	    .pApplicationInfo = &app,
	    .enabledLayerCount = (uint32_t)request_layers.size(),
	    .ppEnabledLayerNames = (const char**)request_layers.data(),
	    .enabledExtensionCount = (uint32_t)request_extensions.size(),
	    .ppEnabledExtensionNames = (const char**)request_extensions.data(),
	};
	if(vkCreateInstance(&inst_info, nullptr, &instance) != VK_SUCCESS) return false;
	volkLoadInstanceOnly(instance);

	if(info.pNext) {
	    auto dbginfo = (VkDebugUtilsMessengerCreateInfoEXT*)info.pNext;
	    if(dbginfo->sType == VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT) {
		if(vkCreateDebugUtilsMessengerEXT(instance, &ContextDebugUtilsInfo,
		    nullptr, &debug_messenger) != VK_SUCCESS) return false;
	    }
	}

	return true;
    }

    bool Context::create_device(ContextDeviceInfo& info) {
	uint32_t device_count = 0;
    	vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
	if(!device_count) return false;
    	VkPhysicalDevice devices[device_count];
    	vkEnumeratePhysicalDevices(instance, &device_count, devices);
    	for(size_t i = 0; i < device_count; i++) {
    	    VkPhysicalDeviceProperties properties;
    	    vkGetPhysicalDeviceProperties(devices[i], &properties);
    	    if(properties.deviceType == info.preferred_device_type)
    	        physical_device = devices[i];
    	}
    	if(physical_device == VK_NULL_HANDLE) physical_device = devices[0];
       	VkPhysicalDeviceProperties properties;
    	vkGetPhysicalDeviceProperties(physical_device, &properties);
    	log(std::format("Picked {} as GPU", properties.deviceName));
	std::vector<std::string> available_extensions;
	{
	    uint32_t count = 0;
	    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr);
	    VkExtensionProperties extensions[count];
	    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, extensions);
	    for(auto& ext: extensions) available_extensions.push_back(ext.extensionName);
	}
	auto found_opt_extensions = loop_through_vector(info.optional_extensions,
		available_extensions);
	auto found_req_extensions = loop_through_vector(info.required_extensions,
		available_extensions);
	if(found_req_extensions.size() != info.required_extensions.size()) return false;
	auto request_extensions = combine_vectors(found_opt_extensions, found_req_extensions);

	uint32_t queue_family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);
	VkQueueFamilyProperties queue_families[queue_family_count];
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count,
		queue_families);

	std::vector<QueueIndex> queue_idx;
	for(auto& requested: info.queues_to_request) {
	    for(uint32_t i = 0; i < queue_family_count; i++) {
		if(requested == Queue::Present) {
	    	    VkBool32 present = false;
		    vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface, &present);
	    	    if(present) {
			QueueIndex nqueue {
			    .type = Queue::Present,
			    .index = i,
			};
			queue_idx.push_back(nqueue);
		    }
	    	} else {
		    if(queue_families[i].queueFlags & QueueIndex::queue_to_flag(requested)) {
			QueueIndex nqueue {
			    .type = requested,
			    .index = i,
			};
			queue_idx.push_back(nqueue);
		    }
		}
	    }
    	}

	if(queue_idx.size() != info.queues_to_request.size()) return false;
	std::set<uint32_t> unique_indices;
	for(auto& queue: queue_idx) unique_indices.insert(queue.index);
    	float priority = 1.0f;
	std::vector<VkDeviceQueueCreateInfo> queue_infos;
	for(auto& index: unique_indices) {
    	    VkDeviceQueueCreateInfo info = {
    	        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = index,
    	        .queueCount = 1,
    	        .pQueuePriorities = &priority,
    	    };
	    queue_infos.push_back(info);
    	}
    	VkPhysicalDeviceVulkan13Features vk13features = info.vk13features;
	vk13features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    	VkPhysicalDeviceVulkan12Features vk12features = info.vk12features;
    	vk12features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    	vk12features.pNext = &vk13features;
    	VkPhysicalDeviceVulkan11Features vk11features = info.vk11features;
    	vk11features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    	vk11features.pNext = &vk12features;
	VkPhysicalDeviceFeatures vk10features = info.vk10features;
    	VkPhysicalDeviceFeatures2 features = {
    	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
    	    .pNext = &vk11features,
	    .features = vk10features,
    	};
    	request_extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    	VkDeviceCreateInfo dinfo = {
    	    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    	    .pNext = &vk12features,
    	    .queueCreateInfoCount = (uint32_t)queue_infos.size(),
    	    .pQueueCreateInfos = queue_infos.data(),
    	    .enabledExtensionCount = (uint32_t)request_extensions.size(),
    	    .ppEnabledExtensionNames = request_extensions.data(),
    	    .pEnabledFeatures = &features.features,
    	};
    	if(vkCreateDevice(physical_device, &dinfo, nullptr, &device) != VK_SUCCESS)
	    return false;
	volkLoadDevice(device);
	for(auto& queue: queue_idx)
	    vkGetDeviceQueue(device, queue.index, 0, &queue.queue);
	queues = queue_idx;
	return true;
    }

    bool Context::create_surface_swapchain(ContextSwapchainInfo& info) {
	if(!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) return false;
	VkSurfaceCapabilitiesKHR surface_capabilities;
	VkExtent2D extent;
	VkPresentModeKHR present_mode;
	VkSurfaceFormatKHR format;
	uint32_t image_count;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_capabilities);
    	uint32_t format_count = 0, present_mode_count = 0;
    	vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, nullptr);
    	vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface,
		&present_mode_count, nullptr);
    	if(!format_count || !present_mode_count) return false;
    	VkSurfaceFormatKHR formats[format_count];
    	VkPresentModeKHR present_modes[present_mode_count];
    	vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats);
    	vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count, present_modes);
	for(auto surface: formats) {
	    if(surface.format == info.surface_format.format
    		    && surface.colorSpace == info.surface_format.colorSpace) {
		format = surface;
		break;
	    }
	}
    	for(size_t i = 0; i < present_mode_count; i++) {
    	    if(present_modes[i] == info.present_mode) {
    	        present_mode = present_modes[i];
    	        break;
    	    }
    	    present_mode = VK_PRESENT_MODE_FIFO_KHR;
    	}
    	if(surface_capabilities.currentExtent.width != UINT32_MAX) {
    	    extent = surface_capabilities.currentExtent;
    	} else {
    	    extent.width = std::clamp(info.width,
		    surface_capabilities.minImageExtent.width, 
		    surface_capabilities.maxImageExtent.width);
    	    extent.height = std::clamp(info.height,
		    surface_capabilities.minImageExtent.height,
		    surface_capabilities.maxImageExtent.height);
    	}
    	image_count = surface_capabilities.minImageCount + 1;
    	if(surface_capabilities.maxImageCount > 0
		&& image_count > surface_capabilities.maxImageCount)
    	    image_count = surface_capabilities.maxImageCount;
	std::set<uint32_t> unique_indices;
	for(auto& queue: queues) unique_indices.insert(queue.index);
	std::vector<uint32_t> indices {unique_indices.begin(), unique_indices.end()};
    	VkSwapchainCreateInfoKHR swp_info = {
    	    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    	    .surface = surface,
    	    .minImageCount = image_count,
    	    .imageFormat = format.format,
    	    .imageColorSpace = format.colorSpace,
    	    .imageExtent = extent,
    	    .imageArrayLayers = 1,
    	    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
		| VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    	    .imageSharingMode = indices.size() > 1 
		? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
    	    .queueFamilyIndexCount = (uint32_t)indices.size(),
    	    .pQueueFamilyIndices = indices.data(),
    	    .preTransform = surface_capabilities.currentTransform,
    	    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    	    .presentMode = present_mode,
    	    .clipped = VK_TRUE,
    	};
    	if(vkCreateSwapchainKHR(device, &swp_info, nullptr, &swapchain) != VK_SUCCESS)
	    return false;
	vkGetSwapchainImagesKHR(device, swapchain, &image_count, nullptr);
	swapchain_images.resize(image_count);
    	vkGetSwapchainImagesKHR(device, swapchain, &image_count, swapchain_images.data());
    	swapchain_format = format.format;
    	swapchain_extent = extent;
	swapchain_support_data = {format, present_mode, surface_capabilities, image_count,
	    indices.size() > 1 ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE, indices};
	return create_swapchain_image_views();
    }

    bool Context::init_vma(VmaAllocatorCreateFlags flags) {
	VmaVulkanFunctions vma_vulkan_func {
	    .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
	    .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
	    .vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties,
	    .vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties,
	    .vkAllocateMemory = vkAllocateMemory,
	    .vkFreeMemory = vkFreeMemory,
	    .vkMapMemory = vkMapMemory,
	    .vkUnmapMemory = vkUnmapMemory,
	    .vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges,
	    .vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges,
	    .vkBindBufferMemory = vkBindBufferMemory,
	    .vkBindImageMemory = vkBindImageMemory,
	    .vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements,
	    .vkGetImageMemoryRequirements = vkGetImageMemoryRequirements,
	    .vkCreateBuffer = vkCreateBuffer,
	    .vkDestroyBuffer = vkDestroyBuffer,
	    .vkCreateImage = vkCreateImage,
	    .vkDestroyImage = vkDestroyImage,
	    .vkCmdCopyBuffer = vkCmdCopyBuffer,
	};
	VmaAllocatorCreateInfo info = {
	    .flags = flags,
	    .physicalDevice = physical_device,
	    .device = device,
	    .pVulkanFunctions = &vma_vulkan_func,
	    .instance = instance,
	};
        if(vmaCreateAllocator(&info, &allocator) != VK_SUCCESS) return false;
	return true;
    }

    bool Context::init_command_submitter(VkCommandBuffer cmd, VkQueue queue, uint32_t queue_index) {
	CommandSubmitter cmdsub = {
	    .queue = queue,
	    .index = queue_index,
	    .fence = create_fence(device),
	    .buffer = cmd,
	};
	if(cmdsub.fence == VK_NULL_HANDLE) return false;
	command_submitter = cmdsub;
	return true;
    }

    bool Context::submit_command_to_queue(std::function<void(VkCommandBuffer cmd)>&& fn) {
	vkResetFences(device, 1, &command_submitter->fence);
	vkResetCommandBuffer(command_submitter->buffer, 0);
	VkCommandBufferBeginInfo begin = {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	if(vkBeginCommandBuffer(command_submitter->buffer, &begin) != VK_SUCCESS)
	    return false;
	fn(command_submitter->buffer);
	if(vkEndCommandBuffer(command_submitter->buffer) != VK_SUCCESS) return false;
	VkSubmitInfo submit = {
	    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
	    .commandBufferCount = 1,
	    .pCommandBuffers = &command_submitter->buffer,
	};
 	if(vkQueueSubmit(command_submitter->queue, 1, &submit,
		    command_submitter->fence) != VK_SUCCESS) return false;
	vkWaitForFences(device, 1, &command_submitter->fence, 1, UINT64_MAX);
	return true;
    }

    QueueIndex* Context::find_queue(const Queue& type) {
	QueueIndex* queue = nullptr;
	for(auto& q: queues) {
    	    if(q.type == vb::Queue::Graphics) {
    	        queue = &q;
    	        break;
    	    }
    	}
	return queue;
    }

    std::optional<uint32_t> Context::acquire_next_image(VkSemaphore signal_semaphore) {
	uint32_t image_index;
	VkResult result = vkAcquireNextImageKHR(device, swapchain,
	    UINT64_MAX, signal_semaphore, VK_NULL_HANDLE, &image_index);
	if(result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
	    resize_callback();
	    return std::nullopt;
	}
	return image_index;
    }

    void Context::recreate_swapchain(std::function<void(uint32_t,uint32_t)>&& call_before_swapchain_create) {
	vkDeviceWaitIdle(device);
	int w,h;
	SDL_GetWindowSize(window, &w, &h);
	if(call_before_swapchain_create) call_before_swapchain_create(w,h);
    	if(swapchain_support_data.surface_capabilities.currentExtent.width != UINT32_MAX) {
    	    swapchain_extent = swapchain_support_data.surface_capabilities.currentExtent;
    	} else {
    	    swapchain_extent.width = std::clamp((uint32_t)w,
		    swapchain_support_data.surface_capabilities.minImageExtent.width, 
		    swapchain_support_data.surface_capabilities.maxImageExtent.width);
    	    swapchain_extent.height = std::clamp((uint32_t)h,
		    swapchain_support_data.surface_capabilities.minImageExtent.height,
		    swapchain_support_data.surface_capabilities.maxImageExtent.height);
    	}
    	VkSwapchainCreateInfoKHR info = {
    	    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    	    .surface = surface,
    	    .minImageCount = swapchain_support_data.image_count,
    	    .imageFormat = swapchain_support_data.format.format,
    	    .imageColorSpace = swapchain_support_data.format.colorSpace,
    	    .imageExtent = swapchain_extent,
    	    .imageArrayLayers = 1,
    	    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    	    .imageSharingMode = swapchain_support_data.image_sharing_mode,
    	    .queueFamilyIndexCount = (uint32_t)swapchain_support_data.queue_family_indices.size(),
    	    .pQueueFamilyIndices = swapchain_support_data.queue_family_indices.data(),
    	    .preTransform = swapchain_support_data.surface_capabilities.currentTransform,
    	    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    	    .presentMode = swapchain_support_data.present_mode,
    	    .clipped = VK_TRUE,
	    .oldSwapchain = swapchain,
    	};
	VkSwapchainKHR temp_swapchain;
    	assert(vkCreateSwapchainKHR(device, &info, nullptr, &temp_swapchain) == VK_SUCCESS);
	destroy_swapchain();
	swapchain = temp_swapchain;
    	vkGetSwapchainImagesKHR(device, swapchain, &swapchain_support_data.image_count, swapchain_images.data());
	create_swapchain_image_views();
    }

    bool Context::create_swapchain_image_views() {
	swapchain_image_views.resize(swapchain_images.size());
    	for(size_t i = 0; i < swapchain_images.size(); i++) {
    	    VkImageViewCreateInfo info = {
    	        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    	        .image = swapchain_images[i],
    	        .viewType = VK_IMAGE_VIEW_TYPE_2D,
    	        .format = swapchain_format,
    	        .components = {
		    VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
		    VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
    	        },
    	        .subresourceRange = {
		    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		    .levelCount = 1,
		    .layerCount = 1,
    	        },
    	    };
    	    if(vkCreateImageView(device, &info, nullptr, &swapchain_image_views[i])
		    != VK_SUCCESS) return false;
    	}
	return true;
    }

    void Context::destroy_swapchain() {
	for(auto& image_view : swapchain_image_views)
	    vkDestroyImageView(device, image_view, nullptr);
	vkDestroySwapchainKHR(device, swapchain, nullptr);
    }

    void transition_image(VkCommandBuffer cmd, VkImage image,
	    VkImageLayout old_layout, VkImageLayout new_layout) {
	VkImageAspectFlags aspect_mask;
	if(new_layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
	    aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT;
	else if(new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
	    aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	else
	    aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
	VkImageMemoryBarrier barrier = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
	    .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
	    .dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
	    .oldLayout = old_layout,
	    .newLayout = new_layout,
	    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	    .image = image,
	    .subresourceRange = {
		.aspectMask = aspect_mask,
		.levelCount = VK_REMAINING_MIP_LEVELS,
		.layerCount = 1,
	    },
	};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 
	    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
	    1, &barrier);
    }

    void blit_image(VkCommandBuffer cmd, VkImage source, VkImage dest,
	    VkExtent3D src_extent, VkExtent3D dst_extent, uint32_t mip_level,
	    VkImageAspectFlags aspect_mask) {
	VkImageBlit region = {
	    .srcSubresource = {
		.aspectMask = aspect_mask,
		.mipLevel = mip_level,
		.layerCount = 1,
	    },
	    .srcOffsets = {{}, {
		(int32_t)src_extent.width,
		(int32_t)src_extent.height,
		(int32_t)src_extent.depth}},
	    .dstSubresource = {
		.aspectMask = aspect_mask,
		.mipLevel = mip_level,
		.layerCount = 1,
	    },
	    .dstOffsets = {{}, {
		(int32_t)dst_extent.width,
		(int32_t)dst_extent.height,
		(int32_t)dst_extent.depth}},
	};
	vkCmdBlitImage(cmd, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    	    dest, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region,
	    VK_FILTER_LINEAR);
    }

    VkShaderModule create_shader_module(VkDevice device, const char* path) {
	std::ifstream file {path, std::ios::binary};
	if(!file.is_open()) return VK_NULL_HANDLE;
    	std::vector<char> buffer(std::istreambuf_iterator<char>(file), {});
    	file.close();
        VkShaderModuleCreateInfo info = {
	    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
	    .codeSize = buffer.size() * sizeof(char),
	    .pCode = (uint32_t*)buffer.data(),
        };
        VkShaderModule shader;
        if(vkCreateShaderModule(device, &info, NULL, &shader) != VK_SUCCESS)
	    return VK_NULL_HANDLE;
        return shader;
    }

    VkCommandPool create_cmd_pool(VkDevice device, uint32_t queue_family_index,
	    VkCommandPoolCreateFlags flags) {
	VkCommandPoolCreateInfo info = {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    	    .flags = flags,
    	    .queueFamilyIndex = queue_family_index,
    	};
	VkCommandPool pool;
	if(vkCreateCommandPool(device, &info, nullptr, &pool) != VK_SUCCESS)
	    return VK_NULL_HANDLE;
	return pool;
    }

    VkSemaphore create_semaphore(VkDevice device, VkSemaphoreCreateFlags flags) {
        VkSemaphoreCreateInfo info = {
	   .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    	   .flags = flags,
           };
        VkSemaphore semaphore;
        if(vkCreateSemaphore(device, &info, NULL, &semaphore) != VK_SUCCESS)
	    return VK_NULL_HANDLE;
        return semaphore;
    }
    
    VkFence create_fence(VkDevice device, VkFenceCreateFlags flags) {
        VkFenceCreateInfo info = {
	    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
	    .flags = flags,
        };
        VkFence fence;
        if(vkCreateFence(device, &info, NULL, &fence) != VK_SUCCESS)
	    return VK_NULL_HANDLE;
        return fence;
    }

    void CommandPool::create(uint32_t queue_index, VkCommandPoolCreateFlags flags) {
	this->queue_index = queue_index;
	pool = create_cmd_pool(ctx->device, queue_index, flags);
    }

    [[nodiscard]] VkCommandBuffer CommandPool::allocate() {
	VkCommandBufferAllocateInfo info = {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
	    .commandPool = pool,
	    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
	    .commandBufferCount = 1,
	};
	VkCommandBuffer buffer = VK_NULL_HANDLE;
	vkAllocateCommandBuffers(ctx->device, &info, &buffer);
	return buffer;
    }

    void CommandPool::clean() {
	vkDestroyCommandPool(ctx->device, pool, nullptr);
	pool = VK_NULL_HANDLE;
    }

    void DescriptorPool::add_binding(VkDescriptorType type,
	    VkShaderStageFlags stage, uint32_t binding, uint32_t count) {
	bindings.push_back({
	    .binding = binding,
	    .descriptorType = type,
	    .descriptorCount = count,
	    .stageFlags = stage,
	});
    }

    void DescriptorPool::create(std::span<VkDescriptorPoolSize> sizes,
	    uint32_t max_sets, VkDescriptorPoolCreateFlags flags) {
	VkDescriptorPoolCreateInfo info = {
	    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
	    .flags = flags,
	    .maxSets = max_sets,
	    .poolSizeCount = (uint32_t)sizes.size(),
	    .pPoolSizes = sizes.data(),
	};
	if(vkCreateDescriptorPool(ctx->device, &info, nullptr, &pool) != VK_SUCCESS)
	    return;
    }

    VkDescriptorSet DescriptorPool::create_set(VkDescriptorSetLayout layout,
	    uint32_t count, void* pNext) {
	VkDescriptorSetAllocateInfo info = {
	    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
	    .pNext = pNext,
	    .descriptorPool = pool,
	    .descriptorSetCount = count,
	    .pSetLayouts = &layout,
	};
	VkDescriptorSet set;
	if(vkAllocateDescriptorSets(ctx->device, &info, &set) != VK_SUCCESS)
	    return VK_NULL_HANDLE;
	return set;
    }

    VkDescriptorSetLayout DescriptorPool::create_layout(VkDescriptorSetLayoutCreateFlags flags,
	    void* pNext) {
	VkDescriptorSetLayoutCreateInfo info {
	    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
	    .pNext = pNext,
	    .flags = flags,
	    .bindingCount = (uint32_t)bindings.size(),
	    .pBindings = bindings.data(),
	};
	VkDescriptorSetLayout layout;
	if(vkCreateDescriptorSetLayout(ctx->device, &info, nullptr, &layout) != VK_SUCCESS)
	    return VK_NULL_HANDLE;
	return layout;
    }

    void DescriptorPool::clean_bindings() {
	bindings.clear();
    }

    void DescriptorPool::clean_layout(VkDescriptorSetLayout& layout) {
	vkDestroyDescriptorSetLayout(ctx->device, layout, nullptr);
	layout = VK_NULL_HANDLE;
    }

    void DescriptorPool::clean() {
	vkDestroyDescriptorPool(ctx->device, pool, nullptr);
	pool = VK_NULL_HANDLE;
	clean_bindings();
    }

    void Buffer::create(const size_t size, VkBufferCreateFlags usage, VmaMemoryUsage mem_usage) {
	VkBufferCreateInfo buffer_info = {
	    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
	    .size = size,
	    .usage = usage,
	};
	VmaAllocationCreateInfo allocation_info = {
	    .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
	    .usage = mem_usage,
	};

	if(vmaCreateBuffer(ctx->allocator, &buffer_info, &allocation_info,
	    &buffer, &allocation, &info) != VK_SUCCESS) return;
    }

    void Buffer::clean() {
	vmaDestroyBuffer(ctx->allocator, buffer, allocation);
	buffer = VK_NULL_HANDLE;
	allocation = VK_NULL_HANDLE;
    }

    void Image::create(VkExtent3D extent, bool mipmap, VkSampleCountFlagBits samples,
	    VkFormat format, VkImageUsageFlags usage) {
    	this->format = format;
    	this->extent = extent;
    	VkImageCreateInfo image_info = {
    	    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
	    .imageType = VK_IMAGE_TYPE_2D,
	    .format = format,
	    .extent = extent,
	    .mipLevels = mip_level,
	    .arrayLayers = 1,
	    .samples = samples,
	    .tiling = VK_IMAGE_TILING_OPTIMAL,
	    .usage = usage,
    	};
	if(mipmap) {
	    image_info.mipLevels = (uint32_t)(floorf(log2(std::max(extent.width, extent.height))))+1;
	    mip_level = image_info.mipLevels;
	}
    	VmaAllocationCreateInfo allocation_info = {
    	    .usage = VMA_MEMORY_USAGE_GPU_ONLY,
	    .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    	};

    	if(vmaCreateImage(ctx->allocator, &image_info, &allocation_info,
	    &image, &allocation, nullptr) != VK_SUCCESS) return;

	VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	if(format == VK_FORMAT_D32_SFLOAT) aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    	VkImageViewCreateInfo info = {
    	    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    	    .image = image,
    	    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    	    .format = format,
    	    .subresourceRange = {
		.aspectMask = aspect,
		.levelCount = mip_level,
		.layerCount = 1,
    	    },
    	};
	if(vkCreateImageView(ctx->device, &info, nullptr, &image_view) != VK_SUCCESS)
	    return;
    }

    void Image::create(void* data, VkExtent3D extent, bool mipmap, VkSampleCountFlagBits samples,
	    VkFormat format, VkImageUsageFlags usage) {
	if(!ctx->command_submitter.has_value()) return;
	size_t data_size = extent.depth * extent.width * extent.height * 4;
	auto staging_buffer = Buffer(ctx);
	staging_buffer.create(data_size,
	    VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	memcpy(staging_buffer.info.pMappedData, data, data_size);
	create(extent, mipmap, samples, format, usage);
	if(!all_valid()) return;
	ctx->submit_command_to_queue([&](VkCommandBuffer cmd) {
	    transition_image(cmd, image,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	    VkBufferImageCopy copy = {
	        .imageSubresource = {
	            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	            .layerCount = 1,
	        },
	        .imageExtent = extent,
	    };
	    vkCmdCopyBufferToImage(cmd, staging_buffer.buffer, image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
	    
	    if(mipmap) {
		int32_t mip_width = extent.width;
	    	int32_t mip_height = extent.height;
		VkImageMemoryBarrier barrier = {
		    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		    .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
		    .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
		    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		    .image = image,
		    .subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.levelCount = 1,
			.layerCount = 1,
		    },
		};
		for(uint32_t i = 1; i < mip_level; i++) {
		    barrier.subresourceRange.baseMipLevel = i - 1;
		    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, 
			VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
			1, &barrier);
		    VkImageBlit blit = {
			.srcSubresource = {
			    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			    .mipLevel = i - 1,
			    .layerCount = 1,
			},
			.srcOffsets = {{}, {mip_width, mip_height, 1}},
			.dstSubresource = {
			    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			    .mipLevel = i,
			    .layerCount = 1,
			},
			.dstOffsets = {{}, {
			    mip_width > 1 ? mip_width/2 : 1,
			    mip_height > 1 ? mip_height/2 : 1, 1}},
		    };
		    vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			    image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			    1, &blit, VK_FILTER_LINEAR);
		    if(mip_width > 1) mip_width /= 2;
		    if(mip_height > 1) mip_height /= 2;
		    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, 
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
			1, &barrier);
		}
		barrier.subresourceRange.baseMipLevel = mip_level - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, 
    		    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
	    	    1, &barrier);
	    } else {
		transition_image(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	    }
	});
	staging_buffer.clean();
    }

    void Image::create(const char* path, bool mipmap, VkSampleCountFlagBits samples,
	    VkFormat format, VkImageUsageFlags usage) {
	int w, h, c;
	auto data = stbi_load(path, &w, &h, &c, 4);
	if(!data) return;
	VkExtent3D img_extent = {(uint32_t)w, (uint32_t)h, 1};
	create(data, img_extent, mipmap, samples, format, usage);
	stbi_image_free(data);
    }

    void Image::clean() {
	vkDestroyImageView(ctx->device, image_view, nullptr);
	vmaDestroyImage(ctx->allocator, image, allocation);
	image_view = VK_NULL_HANDLE;
	image = VK_NULL_HANDLE;
	allocation = VK_NULL_HANDLE;
    }

    void GraphicsPipeline::add_shader(VkShaderModule& shader_module, VkShaderStageFlagBits stage) {
	VkPipelineShaderStageCreateInfo info = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
	    .stage = stage,
	    .module = shader_module,
	    .pName = "main",
	};
	shader_stages.push_back(info);
	shader_modules.push_back(shader_module);
    }

    void GraphicsPipeline::add_shader(const char* path, VkShaderStageFlagBits stage) {
	auto module = create_shader_module(ctx->device, path);
	add_shader(module, stage);
    }

    void GraphicsPipeline::add_push_constant(const uint32_t size,
	    VkShaderStageFlagBits stage, const uint32_t offset) {
	VkPushConstantRange range = {stage, offset, size};
	push_constants.push_back(range);
    }

    void GraphicsPipeline::create(void* pNext, VkPipelineCreateFlags flags) {
        VkPipelineDynamicStateCreateInfo dynamic_state = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
	    .dynamicStateCount = (uint32_t)dynamic_states.size(),
	    .pDynamicStates = dynamic_states.data(),
        };
	VkPipelineLayoutCreateInfo pipeline_layout = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
	    .setLayoutCount = (uint32_t)descriptor_set_layouts.size(),
	    .pSetLayouts = descriptor_set_layouts.empty() 
		? nullptr : descriptor_set_layouts.data(),
	    .pushConstantRangeCount = (uint32_t)push_constants.size(),
	    .pPushConstantRanges = push_constants.empty() 
		? nullptr : push_constants.data(),
	};
	if(vkCreatePipelineLayout(ctx->device, &pipeline_layout, nullptr, &layout) != VK_SUCCESS)
	    return;
        VkGraphicsPipelineCreateInfo info = {
	   .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
	   .pNext = pNext,
	   .flags = flags,
    	   .stageCount = (uint32_t)shader_stages.size(),
    	   .pStages = shader_stages.data(),
    	   .pVertexInputState = &vertex_input,
    	   .pInputAssemblyState = &input_assembly,
    	   .pTessellationState = &tessellation,
    	   .pViewportState = &viewport,
    	   .pRasterizationState = &rasterization,
    	   .pMultisampleState = &multisample,
    	   .pDepthStencilState = &depth_stencil,
    	   .pColorBlendState = &color_blend,
    	   .pDynamicState = &dynamic_state,
    	   .layout = layout,
	   .renderPass = render_pass,
	   .subpass = subpass_index,
        };
        if(vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1, &info,
		    NULL, &pipeline) != VK_SUCCESS) return;
    }

    void GraphicsPipeline::clean() {
	vkDestroyPipeline(ctx->device, pipeline, nullptr);
	vkDestroyPipelineLayout(ctx->device, layout, nullptr);
	pipeline = VK_NULL_HANDLE;
	layout = VK_NULL_HANDLE;
    }

    void GraphicsPipeline::clean_shaders() {
	for(auto& shader: shader_modules)
	    vkDestroyShaderModule(ctx->device, shader, nullptr);
	shader_modules.clear();
    }
}
