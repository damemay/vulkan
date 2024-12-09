use ash::{khr, vk};
use std::rc::Rc;

use crate::{Dev, Error, Surface};

pub struct SwapchainRequestInfo<'a> {
    pub instance: &'a ash::Instance,
    pub device: &'a Rc<Dev>,
    pub surface: &'a Rc<Surface>,
    pub extent: vk::Extent2D,
    pub surface_format: Option<vk::SurfaceFormatKHR>,
    pub present_mode: Option<vk::PresentModeKHR>,
}

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

    device: Rc<Dev>,
    surface: Rc<Surface>,
}

impl Swapchain {
    pub fn new(info: SwapchainRequestInfo) -> Result<Self, Error> {
        let surface_format = if info.surface_format.is_some() {
            unsafe {
                info.surface
                    .loader
                    .get_physical_device_surface_formats(info.device.pdev, info.surface.surface)
            }
            .map_err(|_| Error::NoSurfaceFormatFound)?
            .into_iter()
            .find(|sf| {
                sf.format == info.surface_format.unwrap().format
                    && sf.color_space == info.surface_format.unwrap().color_space
            })
            .unwrap_or(vk::SurfaceFormatKHR {
                format: vk::Format::B8G8R8A8_SRGB,
                color_space: vk::ColorSpaceKHR::SRGB_NONLINEAR,
            })
        } else {
            vk::SurfaceFormatKHR {
                format: vk::Format::B8G8R8A8_SRGB,
                color_space: vk::ColorSpaceKHR::SRGB_NONLINEAR,
            }
        };

        let present_mode = if info.present_mode.is_some() {
            unsafe {
                info.surface
                    .loader
                    .get_physical_device_surface_present_modes(
                        info.device.pdev,
                        info.surface.surface,
                    )
            }
            .map_err(|_| Error::NoPresentModeFound)?
            .into_iter()
            .find(|&pm| pm == info.present_mode.unwrap())
            .unwrap_or(vk::PresentModeKHR::FIFO)
        } else {
            vk::PresentModeKHR::FIFO
        };

        let capabilities = unsafe {
            info.surface
                .loader
                .get_physical_device_surface_capabilities(info.device.pdev, info.surface.surface)
        }
        .map_err(|_| Error::NoSurfaceCapabilitiesFound)?;

        let extent = if capabilities.current_extent.width != u32::MAX {
            capabilities.current_extent
        } else {
            vk::Extent2D {
                width: info.extent.width.clamp(
                    capabilities.min_image_extent.width,
                    capabilities.max_image_extent.width,
                ),
                height: info.extent.height.clamp(
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

        let sharing_mode = if info.device.unique_indices.len() > 1 {
            vk::SharingMode::CONCURRENT
        } else {
            vk::SharingMode::EXCLUSIVE
        };

        let swapchain_info = vk::SwapchainCreateInfoKHR::default()
            .surface(info.surface.surface)
            .min_image_count(image_count)
            .image_format(surface_format.format)
            .image_color_space(surface_format.color_space)
            .image_extent(extent)
            .image_array_layers(1)
            .image_usage(vk::ImageUsageFlags::COLOR_ATTACHMENT | vk::ImageUsageFlags::TRANSFER_DST)
            .image_sharing_mode(sharing_mode)
            .queue_family_indices(&info.device.unique_indices)
            .pre_transform(capabilities.current_transform)
            .composite_alpha(vk::CompositeAlphaFlagsKHR::OPAQUE)
            .present_mode(present_mode)
            .clipped(true);

        let loader = khr::swapchain::Device::new(info.instance, &info.device.dev);
        let handle = unsafe { loader.create_swapchain(&swapchain_info, None) }
            .map_err(|_| Error::SwapchainCreate)?;

        let (images, image_views) =
            Self::get_images(&info.device.dev, &loader, &handle, surface_format.format)?;

        Ok(Swapchain {
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
            device: Rc::clone(&info.device),
            surface: Rc::clone(&info.surface),
        })
    }

    pub fn destroy(&mut self) {
        unsafe {
            for &i in self.image_views.iter() {
                self.device.dev.destroy_image_view(i, None);
            }
            self.loader.destroy_swapchain(self.handle, None);
        }
    }

    fn get_images(
        device: &ash::Device,
        loader: &khr::swapchain::Device,
        handle: &vk::SwapchainKHR,
        format: vk::Format,
    ) -> Result<(Vec<vk::Image>, Vec<vk::ImageView>), Error> {
        let images = unsafe { loader.get_swapchain_images(*handle) }
            .map_err(|_| Error::NoSwapchainImages)?;
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
                unsafe { device.create_image_view(&info, None) }.unwrap()
            })
            .collect();
        Ok((images, image_views))
    }

    pub fn recreate(&mut self, width: u32, height: u32) -> Result<(), Error> {
        unsafe { self.device.dev.device_wait_idle() }.map_err(|_| Error::DeviceLost)?;

        self.capabilities = unsafe {
            self.surface
                .loader
                .get_physical_device_surface_capabilities(self.device.pdev, self.surface.surface)
        }
        .map_err(|_| Error::NoSurfaceCapabilitiesFound)?;

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
            .surface(self.surface.surface)
            .min_image_count(self.image_count)
            .image_format(self.surface_format.format)
            .image_color_space(self.surface_format.color_space)
            .image_extent(extent)
            .image_array_layers(1)
            .image_usage(vk::ImageUsageFlags::COLOR_ATTACHMENT | vk::ImageUsageFlags::TRANSFER_DST)
            .image_sharing_mode(self.sharing_mode)
            .queue_family_indices(&self.device.unique_indices)
            .pre_transform(self.capabilities.current_transform)
            .composite_alpha(vk::CompositeAlphaFlagsKHR::OPAQUE)
            .present_mode(self.present_mode)
            .clipped(true)
            .old_swapchain(self.handle);

        let temp_handle = unsafe { self.loader.create_swapchain(&swapchain_info, None) }
            .map_err(|_| Error::SwapchainCreate)?;

        self.destroy();
        self.handle = temp_handle;
        (self.images, self.image_views) = Self::get_images(
            &self.device.dev,
            &self.loader,
            &self.handle,
            self.surface_format.format,
        )?;

        Ok(())
    }
}

impl Drop for Swapchain {
    fn drop(&mut self) {
        self.destroy()
    }
}
