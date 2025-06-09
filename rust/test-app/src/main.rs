use core::winit::{
    application::ApplicationHandler,
    dpi::PhysicalSize,
    event::WindowEvent,
    event_loop::{ControlFlow, EventLoop},
    raw_window_handle::{HasDisplayHandle, HasWindowHandle},
    window::Window,
};
use std::error::Error;

use iface::*;

#[derive(Default)]
struct TestApp {
    window: Option<Window>,
    vk: Option<GeneralContext>,
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

        let vk = GeneralContext::new(GeneralContextRequestInfo {
            debug: true,
            width: 1280,
            height: 720,
            display_handle: &window.display_handle().unwrap().as_raw(),
            window_handle: &window.window_handle().unwrap().as_raw(),
        })
        .unwrap();

        window.set_visible(true);
        self.window = Some(window);
        self.vk = Some(vk);
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
