use core::{
    ash::{khr, vk},
    winit::{
        application::ApplicationHandler,
        dpi::PhysicalSize,
        event::WindowEvent,
        event_loop::{ControlFlow, EventLoop},
        raw_window_handle::{HasDisplayHandle, HasWindowHandle, RawDisplayHandle, RawWindowHandle},
        window::Window,
    },
    Base, Debug, Dev, DevRequestInfo, QueueRequestInfo, Surface, Swapchain,
};
use std::{error::Error, rc::Rc};

pub struct Interface {
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

        let surface = Rc::new(Surface::new(
            &base.entry,
            &base.instance,
            display_handle,
            window_handle,
        )?);

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

        let dev_info = DevRequestInfo {
            queues: vec![QueueRequestInfo {
                flags: vk::QueueFlags::GRAPHICS,
                present: true,
            }],
            extensions: vec![khr::swapchain::NAME],
            features_2: Some(features_2),
            ..Default::default()
        };

        let device = Rc::new(Dev::new(&base.instance, Some(&surface), dev_info)?);

        let swapchain = Swapchain::new(
            &base.instance,
            Rc::clone(&device),
            Rc::clone(&surface),
            vk::Extent2D {
                width: 1280,
                height: 720,
            },
            None,
            None,
        )?;

        Ok(Self {
            swapchain,
            device,
            surface,
            debug,
            base,
        })
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

        let interface = Interface::new(
            &window.display_handle().unwrap().as_raw(),
            &window.window_handle().unwrap().as_raw(),
        )
        .unwrap();

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
