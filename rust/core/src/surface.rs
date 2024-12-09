use crate::error::Error;
use ash::{khr, vk};
use winit::raw_window_handle::{RawDisplayHandle, RawWindowHandle};

pub struct SurfaceRequestInfo<'a> {
    pub entry: &'a ash::Entry,
    pub instance: &'a ash::Instance,
    pub raw_display_handle: &'a RawDisplayHandle,
    pub raw_window_handle: &'a RawWindowHandle,
}

#[derive(Clone)]
pub struct Surface {
    pub loader: khr::surface::Instance,
    pub surface: vk::SurfaceKHR,
}

impl Surface {
    pub fn new(info: SurfaceRequestInfo) -> Result<Self, Error> {
        let surface = unsafe {
            ash_window::create_surface(
                info.entry,
                info.instance,
                *info.raw_display_handle,
                *info.raw_window_handle,
                None,
            )
        }
        .map_err(|_| Error::SurfaceCreate)?;
        let loader = khr::surface::Instance::new(info.entry, info.instance);
        Ok(Self { loader, surface })
    }
}

impl Drop for Surface {
    fn drop(&mut self) {
        unsafe { self.loader.destroy_surface(self.surface, None) }
    }
}
