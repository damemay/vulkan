use core::gpu_allocator::vulkan::{Allocator, AllocatorCreateDesc};
use core::*;
use std::{cell::RefCell, rc::Rc};

use ash::{ext, khr, vk};
use winit::raw_window_handle::{RawDisplayHandle, RawWindowHandle};

pub struct GeneralContextRequestInfo<'a> {
    pub debug: bool,
    pub width: u32,
    pub height: u32,
    pub display_handle: &'a RawDisplayHandle,
    pub window_handle: &'a RawWindowHandle,
}

pub struct Frame {
    pub cmd: vk::CommandBuffer,
    pub img_available: vk::Semaphore,
    pub render_done: vk::Semaphore,
    pub can_render: vk::Fence,
}

impl Frame {
    pub fn new(pool: &vk::CommandPool, device: &Rc<Dev>) -> Result<Self, Error> {
        let cmd = unsafe {
            device.dev.allocate_command_buffers(
                &vk::CommandBufferAllocateInfo::default()
                    .level(vk::CommandBufferLevel::PRIMARY)
                    .command_pool(*pool)
                    .command_buffer_count(1),
            )
        }?
        .first()
        .ok_or(Error::AllocationError)?
        .to_owned();
        let img_available = unsafe {
            device
                .dev
                .create_semaphore(&vk::SemaphoreCreateInfo::default(), None)
        }?;
        let render_done = unsafe {
            device
                .dev
                .create_semaphore(&vk::SemaphoreCreateInfo::default(), None)
        }?;
        let can_render = unsafe {
            device.dev.create_fence(
                &vk::FenceCreateInfo::default().flags(vk::FenceCreateFlags::SIGNALED),
                None,
            )
        }?;
        Ok(Self {
            cmd,
            img_available,
            render_done,
            can_render,
        })
    }
}

pub struct GeneralContext {
    pub frames: Vec<Frame>,
    pub frame_idx: u8,
    pub frames_cmd_pool: vk::CommandPool,

    pub allocator: Rc<RefCell<Allocator>>,
    pub swapchain: Swapchain,
    pub device: Rc<Dev>,
    pub surface: Rc<Surface>,
    pub debug: Option<Debug>,
    pub base: Base,
}

impl GeneralContext {
    pub fn new(info: GeneralContextRequestInfo) -> Result<Self, Error> {
        let base = Base::new(vk::API_VERSION_1_3, Some(info.display_handle))?;

        let debug = if base.debug && info.debug {
            Some(Debug::new(&base.entry, &base.instance)?)
        } else {
            None
        };

        let surface = Rc::new(Surface::new(SurfaceRequestInfo {
            entry: &base.entry,
            instance: &base.instance,
            raw_display_handle: info.display_handle,
            raw_window_handle: info.window_handle,
        })?);

        let mut host_image_copy =
            vk::PhysicalDeviceHostImageCopyFeaturesEXT::default().host_image_copy(true);
        let mut features_13 = vk::PhysicalDeviceVulkan13Features::default()
            .dynamic_rendering(true)
            .synchronization2(true)
            .maintenance4(true);
        let mut features_12 = vk::PhysicalDeviceVulkan12Features::default()
            .descriptor_indexing(true)
            .buffer_device_address(true);
        let features = vk::PhysicalDeviceFeatures::default().sampler_anisotropy(true);
        let features_2 = vk::PhysicalDeviceFeatures2::default()
            .features(features)
            .push_next(&mut host_image_copy)
            .push_next(&mut features_13)
            .push_next(&mut features_12);

        let device = Rc::new(Dev::new(DevRequestInfo {
            instance: &base.instance,
            surface: Some(&surface),
            queues: vec![QueueRequestInfo {
                flags: vk::QueueFlags::GRAPHICS,
                present: true,
            }],
            extensions: vec![khr::swapchain::NAME, ext::host_image_copy::NAME],
            preferred_type: Some(vk::PhysicalDeviceType::DISCRETE_GPU),
            features: None,
            features_2: Some(features_2),
        })?);

        let swapchain = Swapchain::new(SwapchainRequestInfo {
            instance: &base.instance,
            device: &device,
            surface: &surface,
            extent: vk::Extent2D {
                width: info.width,
                height: info.height,
            },
            surface_format: None,
            present_mode: Some(vk::PresentModeKHR::IMMEDIATE),
        })?;

        let allocator = Rc::new(RefCell::new(
            Allocator::new(&AllocatorCreateDesc {
                instance: base.instance.clone(),
                device: device.dev.clone(),
                physical_device: device.pdev,
                debug_settings: Default::default(),
                buffer_device_address: true,
                allocation_sizes: Default::default(),
            })
            .unwrap(),
        ));

        let frames_cmd_pool = unsafe {
            device.dev.create_command_pool(
                &vk::CommandPoolCreateInfo::default()
                    .flags(vk::CommandPoolCreateFlags::RESET_COMMAND_BUFFER)
                    .queue_family_index(
                        device.queues.get(&vk::QueueFlags::GRAPHICS).unwrap().index,
                    ),
                None,
            )
        }?;

        let mut frames = Vec::new();
        for _ in 0..swapchain.image_count {
            frames.push(Frame::new(&frames_cmd_pool, &device)?);
        }

        Ok(Self {
            frames,
            frame_idx: 0,
            frames_cmd_pool,
            allocator,
            swapchain,
            device,
            surface,
            debug,
            base,
        })
    }
}

impl Drop for GeneralContext {
    fn drop(&mut self) {
        unsafe {
            for f in &self.frames {
                self.device
                    .dev
                    .free_command_buffers(self.frames_cmd_pool, std::slice::from_ref(&f.cmd));
                self.device.dev.destroy_semaphore(f.img_available, None);
                self.device.dev.destroy_semaphore(f.render_done, None);
                self.device.dev.destroy_fence(f.can_render, None);
            }
            self.device
                .dev
                .destroy_command_pool(self.frames_cmd_pool, None);
        }
    }
}
