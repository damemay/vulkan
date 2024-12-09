use core::winit::{
    application::ApplicationHandler,
    dpi::PhysicalSize,
    event::WindowEvent,
    event_loop::{ControlFlow, EventLoop},
    raw_window_handle::{HasDisplayHandle, HasWindowHandle},
    window::Window,
};
use std::error::Error;

mod interface;
use interface::Interface;

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
                    .resize(size.width, size.height)
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
