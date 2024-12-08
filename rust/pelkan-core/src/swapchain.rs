use ash::{khr, vk};
use std::collections::HashSet;

pub struct Swapchain {
    pub loader: khr::swapchain::Device,
    pub handle: vk::SwapchainKHR,

    pub image_count: u32,
    pub images: Vec<vk::Image>,
    pub image_views: Vec<vk::ImageView>,

    pub extent: vk::Extent2D,
    pub surface_format: vk::SurfaceFormatKHR,
    pub present_mode: vk::PresentModeKHR,
    pub sharing_mode: vk::SharingMode,
    pub capabilities: vk::SurfaceCapabilitiesKHR,

    pub unique_indices: Vec<u32>,

    device: ash::Device,
    physical_device: vk::PhysicalDevice,
    surface_loader: khr::surface::Instance,
    surface: vk::SurfaceKHR,
}

impl Swapchain {
    fn get_images(
        device: &ash::Device,
        loader: &khr::swapchain::Device,
        handle: &vk::SwapchainKHR,
        format: vk::Format,
    ) -> (Vec<vk::Image>, Vec<vk::ImageView>) {
        let images = unsafe { loader.get_swapchain_images(*handle) }
            .expect("Failed to get swapchain VkImages! This should not happen!");
        let image_views: Vec<_> = images
            .iter()
            .map(|&img| {
                let info = vk::ImageViewCreateInfo::default()
                    .image(img)
                    .view_type(vk::ImageViewType::TYPE_2D)
                    .format(format)
                    .components(
                        vk::ComponentMapping::default()
                            .r(vk::ComponentSwizzle::IDENTITY)
                            .g(vk::ComponentSwizzle::IDENTITY)
                            .b(vk::ComponentSwizzle::IDENTITY)
                            .a(vk::ComponentSwizzle::IDENTITY),
                    )
                    .subresource_range(
                        vk::ImageSubresourceRange::default()
                            .aspect_mask(vk::ImageAspectFlags::COLOR)
                            .level_count(1)
                            .layer_count(1),
                    );
                unsafe { device.create_image_view(&info, None) }
                    .expect("Failed to create swapchain VkImageView! This should not happen!")
            })
            .collect();
        (images, image_views)
    }

    pub fn recreate(&mut self, width: u32, height: u32) -> Result<(), vk::Result> {
        unsafe { self.device.device_wait_idle() }?;

        self.capabilities = unsafe {
            self.surface_loader
                .get_physical_device_surface_capabilities(self.physical_device, self.surface)
        }?;

        let extent = if self.capabilities.current_extent.width != u32::MAX {
            self.capabilities.current_extent
        } else {
            vk::Extent2D {
                width: width.clamp(
                    self.capabilities.min_image_extent.width,
                    self.capabilities.max_image_extent.width,
                ),
                height: height.clamp(
                    self.capabilities.min_image_extent.height,
                    self.capabilities.max_image_extent.height,
                ),
            }
        };

        let swapchain_info = vk::SwapchainCreateInfoKHR::default()
            .surface(self.surface)
            .min_image_count(self.image_count)
            .image_format(self.surface_format.format)
            .image_color_space(self.surface_format.color_space)
            .image_extent(extent)
            .image_array_layers(1)
            .image_usage(vk::ImageUsageFlags::COLOR_ATTACHMENT | vk::ImageUsageFlags::TRANSFER_DST)
            .image_sharing_mode(self.sharing_mode)
            .queue_family_indices(&self.unique_indices)
            .pre_transform(self.capabilities.current_transform)
            .composite_alpha(vk::CompositeAlphaFlagsKHR::OPAQUE)
            .present_mode(self.present_mode)
            .clipped(true)
            .old_swapchain(self.handle);

        let temp_handle = unsafe { self.loader.create_swapchain(&swapchain_info, None) }?;

        self.destroy();
        self.handle = temp_handle;
        (self.images, self.image_views) = Self::get_images(
            &self.device,
            &self.loader,
            &self.handle,
            self.surface_format.format,
        );

        Ok(())
    }

    pub fn new(
        instance: &ash::Instance,
        device: ash::Device,
        pdev: vk::PhysicalDevice,
        surface_loader: khr::surface::Instance,
        surface: vk::SurfaceKHR,
        indices: &[u32],
        u_extent: vk::Extent2D,
        u_surface_format: vk::SurfaceFormatKHR,
        u_present_mode: vk::PresentModeKHR,
    ) -> Swapchain {
        let surface_format =
            unsafe { surface_loader.get_physical_device_surface_formats(pdev, surface) }
                .expect("Failed to get GPU VkSurfaceFormatKHR! This should not happen!")
                .into_iter()
                .find(|sf| {
                    sf.format == u_surface_format.format
                        && sf.color_space == u_surface_format.color_space
                })
                .unwrap_or(vk::SurfaceFormatKHR {
                    format: vk::Format::B8G8R8A8_SRGB,
                    color_space: vk::ColorSpaceKHR::SRGB_NONLINEAR,
                });

        let present_mode =
            unsafe { surface_loader.get_physical_device_surface_present_modes(pdev, surface) }
                .expect("Failed to get GPU VkPresentModeKHR! This should not happen!")
                .into_iter()
                .find(|&pm| pm == u_present_mode)
                .unwrap_or(vk::PresentModeKHR::FIFO);

        let capabilities =
            unsafe { surface_loader.get_physical_device_surface_capabilities(pdev, surface) }
                .expect("Failed to get GPU VkSurfaceCapabilitiesKHR! This should not happen!");

        let extent = if capabilities.current_extent.width != u32::MAX {
            capabilities.current_extent
        } else {
            vk::Extent2D {
                width: u_extent.width.clamp(
                    capabilities.min_image_extent.width,
                    capabilities.max_image_extent.width,
                ),
                height: u_extent.height.clamp(
                    capabilities.min_image_extent.height,
                    capabilities.max_image_extent.height,
                ),
            }
        };

        let image_count = if capabilities.min_image_count == capabilities.max_image_count {
            capabilities.max_image_count
        } else {
            capabilities.min_image_count + 1
        };

        let mut unique_indices_set = HashSet::new();
        for i in indices {
            unique_indices_set.insert(i);
        }
        let unique_indices: Vec<_> = unique_indices_set.into_iter().map(|i| *i).collect();

        let sharing_mode = if unique_indices.len() > 1 {
            vk::SharingMode::CONCURRENT
        } else {
            vk::SharingMode::EXCLUSIVE
        };

        let swapchain_info = vk::SwapchainCreateInfoKHR::default()
            .surface(surface)
            .min_image_count(image_count)
            .image_format(surface_format.format)
            .image_color_space(surface_format.color_space)
            .image_extent(extent)
            .image_array_layers(1)
            .image_usage(vk::ImageUsageFlags::COLOR_ATTACHMENT | vk::ImageUsageFlags::TRANSFER_DST)
            .image_sharing_mode(sharing_mode)
            .queue_family_indices(&unique_indices)
            .pre_transform(capabilities.current_transform)
            .composite_alpha(vk::CompositeAlphaFlagsKHR::OPAQUE)
            .present_mode(present_mode)
            .clipped(true);

        let loader = khr::swapchain::Device::new(instance, &device);
        let handle = unsafe { loader.create_swapchain(&swapchain_info, None) }
            .expect("Failed to create VkSwapchainKHR! This should not happen!");

        let (images, image_views) =
            Self::get_images(&device, &loader, &handle, surface_format.format);

        Swapchain {
            loader,
            handle,
            image_count,
            images,
            image_views,
            extent,
            surface_format,
            present_mode,
            sharing_mode,
            capabilities,
            unique_indices,
            device,
            physical_device: pdev,
            surface_loader,
            surface,
        }
    }

    pub fn destroy(&mut self) {
        unsafe {
            for &i in self.image_views.iter() {
                self.device.destroy_image_view(i, None);
            }
            self.loader.destroy_swapchain(self.handle, None);
        }
    }
}
