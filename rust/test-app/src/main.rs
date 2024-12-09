use core::{
    ash::{khr, vk},
    winit::{
        application::ApplicationHandler,
        dpi::PhysicalSize,
        event::WindowEvent,
        event_loop::{ControlFlow, EventLoop},
        raw_window_handle::{HasDisplayHandle, HasWindowHandle},
        window::Window,
    },
    Base, Debug, Dev, DevRequestInfo, QueueRequestInfo, Surface, Swapchain,
};
use std::{error::Error, rc::Rc};

#[derive(Default)]
struct TestApp {
    window: Option<Window>,
    swapchain: Option<Swapchain>,
    device: Option<Rc<Dev>>,
    surface: Option<Rc<Surface>>,
    debug: Option<Debug>,
    base: Option<Base>,
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
        let base = Base::new(
            vk::API_VERSION_1_0,
            Some(&window.display_handle().unwrap().as_raw()),
        )
        .unwrap();
        let debug = if base.debug {
            Some(Debug::new(&base.entry, &base.instance).unwrap())
        } else {
            None
        };
        let surface = Rc::new(
            Surface::new(
                &base.entry,
                &base.instance,
                &window.display_handle().unwrap().as_raw(),
                &window.window_handle().unwrap().as_raw(),
            )
            .unwrap(),
        );

        let queues = vec![QueueRequestInfo {
            flags: vk::QueueFlags::GRAPHICS,
            present: true,
        }];

        let dev_info = DevRequestInfo {
            queues,
            extensions: vec![khr::swapchain::NAME],
            preferred_type: None,
            features: None,
            features_2: None,
        };
        let device = Rc::new(Dev::new(&base.instance, Some(&surface), dev_info).unwrap());
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
        )
        .unwrap();

        window.set_visible(true);
        self.window = Some(window);
        self.base = Some(base);
        self.debug = debug;
        self.surface = Some(surface);
        self.device = Some(device);
        self.swapchain = Some(swapchain);
    }

    fn window_event(
        &mut self,
        event_loop: &core::winit::event_loop::ActiveEventLoop,
        _: core::winit::window::WindowId,
        event: core::winit::event::WindowEvent,
    ) {
        match event {
            WindowEvent::RedrawRequested => self.window.as_ref().unwrap().request_redraw(),
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
