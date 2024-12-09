use crate::error::Error;
use ash::{ext, vk};
use winit::raw_window_handle::RawDisplayHandle;

pub struct Base {
    pub entry: ash::Entry,
    pub instance: ash::Instance,
    pub debug: bool,
}

impl Base {
    pub fn new(version: u32, display_handle: Option<&RawDisplayHandle>) -> Result<Self, Error> {
        let entry = Self::load()?;
        let (instance, debug) = Self::create_instance(&entry, version, display_handle)?;
        Ok(Self {
            entry,
            instance,
            debug,
        })
    }

    fn load() -> Result<ash::Entry, Error> {
        match unsafe { ash::Entry::load() } {
            Ok(entry) => Ok(entry),
            Err(_) => Err(Error::Load),
        }
    }

    fn create_instance(
        entry: &ash::Entry,
        version: u32,
        raw_display_handle: Option<&RawDisplayHandle>,
    ) -> Result<(ash::Instance, bool), Error> {
        let app_info = vk::ApplicationInfo::default().api_version(version);

        let mut extensions = if raw_display_handle.is_some() {
            ash_window::enumerate_required_extensions(*raw_display_handle.unwrap())
                .map_err(|_| Error::UnsupportedSurface)?
                .to_vec()
        } else {
            Vec::new()
        };

        let debug_available = {
            let ext = unsafe { entry.enumerate_instance_extension_properties(None) }.unwrap();
            let mut available = false;
            for e in ext {
                if e.extension_name_as_c_str().unwrap() == ext::debug_utils::NAME {
                    extensions.push(ext::debug_utils::NAME.as_ptr());
                    available = true;
                    break;
                }
            }
            available
        };

        let instance_info = vk::InstanceCreateInfo::default()
            .application_info(&app_info)
            .enabled_extension_names(&extensions);

        let instance = unsafe { entry.create_instance(&instance_info, None) }
            .expect("Failed to create VkInstance! This should not happen!");
        Ok((instance, debug_available))
    }
}

impl Drop for Base {
    fn drop(&mut self) {
        unsafe { self.instance.destroy_instance(None) }
    }
}
