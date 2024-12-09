use core::{
    alloc::{self, BufferRequestInfo, ImageRequestInfo},
    ash::{khr, vk},
    gpu_allocator::{
        vulkan::{AllocationScheme, Allocator, AllocatorCreateDesc},
        MemoryLocation,
    },
    winit::{
        application::ApplicationHandler,
        dpi::PhysicalSize,
        event::WindowEvent,
        event_loop::{ControlFlow, EventLoop},
        raw_window_handle::{HasDisplayHandle, HasWindowHandle, RawDisplayHandle, RawWindowHandle},
        window::Window,
    },
    Base, Debug, Dev, DevRequestInfo, QueueRequestInfo, Surface, SurfaceRequestInfo, Swapchain,
    SwapchainRequestInfo,
};
use std::{cell::RefCell, error::Error, rc::Rc};

pub struct Interface {
    pub allocator: Rc<RefCell<Allocator>>,
    pub swapchain: Swapchain,
    pub device: Rc<Dev>,
    pub surface: Rc<Surface>,
    pub debug: Option<Debug>,
    pub base: Base,
}

impl Interface {
    pub fn new(
        display_handle: &RawDisplayHandle,
        window_handle: &RawWindowHandle,
    ) -> Result<Self, Box<dyn Error>> {
        let base = Base::new(vk::API_VERSION_1_3, Some(display_handle))?;

        let debug = if base.debug {
            Some(Debug::new(&base.entry, &base.instance)?)
        } else {
            None
        };

        let surface = Rc::new(Surface::new(SurfaceRequestInfo {
            entry: &base.entry,
            instance: &base.instance,
            raw_display_handle: display_handle,
            raw_window_handle: window_handle,
        })?);

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
            .push_next(&mut features_13)
            .push_next(&mut features_12);

        let device = Rc::new(Dev::new(DevRequestInfo {
            instance: &base.instance,
            surface: Some(&surface),
            queues: vec![QueueRequestInfo {
                flags: vk::QueueFlags::GRAPHICS,
                present: true,
            }],
            extensions: vec![khr::swapchain::NAME],
            preferred_type: None,
            features: None,
            features_2: Some(features_2),
        })?);

        let swapchain = Swapchain::new(SwapchainRequestInfo {
            instance: &base.instance,
            device: &device,
            surface: &surface,
            extent: vk::Extent2D {
                width: 1280,
                height: 720,
            },
            surface_format: None,
            present_mode: None,
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

        Ok(Self {
            allocator,
            swapchain,
            device,
            surface,
            debug,
            base,
        })
    }

    fn alloc_test(&mut self) {
        {
            let _ = alloc::Buffer::new(&BufferRequestInfo {
                allocator: &self.allocator,
                device: &self.device,
                linear: true,
                location: MemoryLocation::CpuToGpu,
                dedicated: true,
                info: &vk::BufferCreateInfo::default()
                    .size(512)
                    .usage(vk::BufferUsageFlags::TRANSFER_SRC),
            })
            .unwrap();
        }
        let _ = alloc::Image::new(&ImageRequestInfo {
            allocator: &self.allocator,
            device: &self.device,
            linear: false,
            location: MemoryLocation::GpuOnly,
            dedicated: true,
            img_info: &vk::ImageCreateInfo::default()
                .image_type(vk::ImageType::TYPE_2D)
                .format(vk::Format::R16G16B16A16_SFLOAT)
                .extent(vk::Extent3D {
                    width: self.swapchain.extent.width,
                    height: self.swapchain.extent.height,
                    depth: 1,
                })
                .mip_levels(1)
                .array_layers(1)
                .samples(vk::SampleCountFlags::TYPE_1)
                .tiling(vk::ImageTiling::OPTIMAL)
                .usage(
                    vk::ImageUsageFlags::COLOR_ATTACHMENT
                        | vk::ImageUsageFlags::STORAGE
                        | vk::ImageUsageFlags::TRANSFER_SRC
                        | vk::ImageUsageFlags::TRANSFER_DST,
                ),
            img_view_info: &vk::ImageViewCreateInfo::default()
                .view_type(vk::ImageViewType::TYPE_2D)
                .format(vk::Format::R16G16B16A16_SFLOAT)
                .subresource_range(
                    vk::ImageSubresourceRange::default()
                        .aspect_mask(vk::ImageAspectFlags::COLOR)
                        .level_count(1)
                        .layer_count(1),
                ),
        })
        .unwrap();
    }
}

#[derive(Default)]
struct TestApp {
    window: Option<Window>,
    vk: Option<Interface>,
}

impl ApplicationHandler for TestApp {
    fn resumed(&mut self, event_loop: &core::winit::event_loop::ActiveEventLoop) {
        let window = event_loop
            .create_window(
                Window::default_attributes()
                    .with_visible(false)
                    .with_resizable(false)
                    .with_inner_size(PhysicalSize {
                        width: 1280,
                        height: 720,
                    })
                    .with_min_inner_size(PhysicalSize {
                        width: 1280,
                        height: 720,
                    }),
            )
            .unwrap();

        let mut interface = Interface::new(
            &window.display_handle().unwrap().as_raw(),
            &window.window_handle().unwrap().as_raw(),
        )
        .unwrap();

        interface.alloc_test();

        event_loop.exit();

        window.set_visible(true);
        self.window = Some(window);
        self.vk = Some(interface);
    }

    fn window_event(
        &mut self,
        event_loop: &core::winit::event_loop::ActiveEventLoop,
        _: core::winit::window::WindowId,
        event: core::winit::event::WindowEvent,
    ) {
        match event {
            WindowEvent::RedrawRequested => self.window.as_ref().unwrap().request_redraw(),
            WindowEvent::Resized(size) => {
                self.vk
                    .as_mut()
                    .unwrap()
                    .swapchain
                    .recreate(size.width, size.height)
                    .unwrap();
            }
            WindowEvent::CloseRequested => event_loop.exit(),
            _ => (),
        }
    }
}

fn main() -> Result<(), Box<dyn Error>> {
    let event_loop = EventLoop::new()?;
    event_loop.set_control_flow(ControlFlow::Poll);

    let mut app = TestApp::default();
    event_loop.run_app(&mut app)?;

    Ok(())
}
