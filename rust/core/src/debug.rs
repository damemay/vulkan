use crate::error::Error;
use ash::{ext, vk};
use std::ffi::{c_void, CStr};

unsafe extern "system" fn vk_debug_cbk(
    _: vk::DebugUtilsMessageSeverityFlagsEXT,
    _: vk::DebugUtilsMessageTypeFlagsEXT,
    data: *const vk::DebugUtilsMessengerCallbackDataEXT<'_>,
    _: *mut c_void,
) -> vk::Bool32 {
    data.as_ref().map(|d| {
        d.p_message
            .as_ref()
            .map(|m| println!("{:?}", CStr::from_ptr(m)))
    });
    vk::FALSE
}

pub struct Debug {
    pub loader: ext::debug_utils::Instance,
    pub messenger: vk::DebugUtilsMessengerEXT,
}

impl Debug {
    pub fn new(entry: &ash::Entry, instance: &ash::Instance) -> Result<Self, Error> {
        let debug_info = vk::DebugUtilsMessengerCreateInfoEXT::default()
            .message_type(
                vk::DebugUtilsMessageTypeFlagsEXT::GENERAL
                    | vk::DebugUtilsMessageTypeFlagsEXT::VALIDATION
                    | vk::DebugUtilsMessageTypeFlagsEXT::PERFORMANCE
                    | vk::DebugUtilsMessageTypeFlagsEXT::DEVICE_ADDRESS_BINDING,
            )
            .message_severity(
                vk::DebugUtilsMessageSeverityFlagsEXT::INFO
                    | vk::DebugUtilsMessageSeverityFlagsEXT::WARNING
                    | vk::DebugUtilsMessageSeverityFlagsEXT::ERROR,
            )
            .pfn_user_callback(Some(vk_debug_cbk));

        let loader = ext::debug_utils::Instance::new(entry, instance);
        let messenger = unsafe { loader.create_debug_utils_messenger(&debug_info, None) }
            .map_err(|_| Error::DebugCreate)?;
        Ok(Self { loader, messenger })
    }
}

impl Drop for Debug {
    fn drop(&mut self) {
        unsafe {
            self.loader
                .destroy_debug_utils_messenger(self.messenger, None)
        }
    }
}
