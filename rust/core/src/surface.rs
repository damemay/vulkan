use crate::error::Error;
use ash::{khr, vk};
use winit::raw_window_handle::{RawDisplayHandle, RawWindowHandle};

#[derive(Clone)]
pub struct Surface {
    pub loader: khr::surface::Instance,
    pub surface: vk::SurfaceKHR,
}

impl Surface {
    pub fn new(
        entry: &ash::Entry,
        instance: &ash::Instance,
        raw_display_handle: &RawDisplayHandle,
        raw_window_handle: &RawWindowHandle,
    ) -> Result<Self, Error> {
        let surface = unsafe {
            ash_window::create_surface(
                entry,
                instance,
                *raw_display_handle,
                *raw_window_handle,
                None,
            )
        }
        .map_err(|_| Error::SurfaceCreate)?;
        let loader = khr::surface::Instance::new(entry, instance);
        Ok(Self { loader, surface })
    }
}

impl Drop for Surface {
    fn drop(&mut self) {
        unsafe { self.loader.destroy_surface(self.surface, None) }
    }
}
