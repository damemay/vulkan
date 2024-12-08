use crate::glob::*;
use crate::swapchain::*;
use ash::{
    ext, khr,
    vk::{self, Handle},
};
use std::{
    collections::HashSet,
    ffi::{c_void, CStr},
};
use winit::raw_window_handle::{RawDisplayHandle, RawWindowHandle};

pub struct InterfaceOptions<'a> {
    pub extent: vk::Extent2D,
    pub surface_format: vk::SurfaceFormatKHR,
    pub present_mode: vk::PresentModeKHR,
    pub raw_display_handle: Option<&'a RawDisplayHandle>,
    pub raw_window_handle: Option<&'a RawWindowHandle>,
}

impl Default for InterfaceOptions<'_> {
    fn default() -> InterfaceOptions<'static> {
        InterfaceOptions {
            extent: vk::Extent2D {
                width: 1280,
                height: 720,
            },
            surface_format: vk::SurfaceFormatKHR {
                format: vk::Format::B8G8R8A8_SRGB,
                color_space: vk::ColorSpaceKHR::SRGB_NONLINEAR,
            },
            present_mode: vk::PresentModeKHR::FIFO,
            raw_display_handle: None,
            raw_window_handle: None,
        }
    }
}

pub struct QueueInfo {
    queue: vk::Queue,
    index: u32,
}

pub struct Interface {
    pub version: u32,
    pub gpu_profile: GPUProfile,

    pub entry: ash::Entry,
    pub instance: ash::Instance,

    pub debug_loader: Option<ext::debug_utils::Instance>,
    pub debug_msg: Option<vk::DebugUtilsMessengerEXT>,

    pub surface_loader: Option<khr::surface::Instance>,
    pub surface: Option<vk::SurfaceKHR>,

    pub device: ash::Device,

    pub graphics_queue: QueueInfo,
    pub compute_queue: QueueInfo,
    pub transfer_queue: QueueInfo,
    pub present_queue: Option<QueueInfo>,

    pub swapchain: Option<Swapchain>,
}

impl Interface {
    fn load() -> ash::Entry {
        match unsafe { ash::Entry::load() } {
            Ok(entry) => entry,
            Err(_) => panic!("Failed to load Vulkan on this device!"),
        }
    }

    fn get_vk_version(entry: &ash::Entry) -> u32 {
        match unsafe { entry.try_enumerate_instance_version() }.unwrap() {
            Some(_) => VK_API,
            None => panic!("pelkan requires at least Vulkan 1.1 support!"),
        }
    }

    fn create_instance(
        entry: &ash::Entry,
        raw_display_handle: Option<&RawDisplayHandle>,
    ) -> (ash::Instance, bool) {
        let app_info = vk::ApplicationInfo::default().api_version(Self::get_vk_version(entry));

        let mut extensions = if raw_display_handle.is_some() {
            ash_window::enumerate_required_extensions(*raw_display_handle.unwrap())
                .expect("Unsupported surface! This should not happen!")
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
        #[cfg(target_os = "macos")]
        {
            extensions.push(khr::portability_enumeration::NAME.as_ptr());
            extensions.push(khr::get_physical_device_properties2::NAME.as_ptr());
        }

        let flags = if cfg!(target_os = "macos") {
            vk::InstanceCreateFlags::ENUMERATE_PORTABILITY_KHR
        } else {
            vk::InstanceCreateFlags::default()
        };

        let instance_info = vk::InstanceCreateInfo::default()
            .application_info(&app_info)
            .enabled_extension_names(&extensions)
            .flags(flags);

        let instance = unsafe { entry.create_instance(&instance_info, None) }
            .expect("Failed to create VkInstance! This should not happen!");
        (instance, debug_available)
    }

    fn create_debug(
        entry: &ash::Entry,
        instance: &ash::Instance,
    ) -> (ext::debug_utils::Instance, vk::DebugUtilsMessengerEXT) {
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
            .expect("Failed to create VkDebugUtilsMessengerEXT! This should not happen!");
        (loader, messenger)
    }

    fn create_surface(
        entry: &ash::Entry,
        instance: &ash::Instance,
        raw_display_handle: &RawDisplayHandle,
        raw_window_handle: &RawWindowHandle,
    ) -> (khr::surface::Instance, vk::SurfaceKHR) {
        let surface = unsafe {
            ash_window::create_surface(
                entry,
                instance,
                *raw_display_handle,
                *raw_window_handle,
                None,
            )
        }
        .expect("Failed to create VkSurfaceKHR! This should not happen!");
        let loader = khr::surface::Instance::new(entry, instance);
        (loader, surface)
    }

    fn get_physical_device(instance: &ash::Instance) -> (vk::PhysicalDevice, GPUProfile) {
        let pdevices =
            unsafe { instance.enumerate_physical_devices() }.expect("No GPU with Vulkan support!");
        let mut min_choice = vk::PhysicalDevice::null();
        let mut max_choice = vk::PhysicalDevice::null();
        for pdev in pdevices {
            let exts = unsafe { instance.enumerate_device_extension_properties(pdev) }
                .expect("Failed to get GPU extensions! This should not happen!");
            let mut min_supported_exts = 0;
            let mut max_supported_exts = 0;
            for e in exts {
                for min in VK_MIN_EXT {
                    if min == e.extension_name_as_c_str().unwrap() {
                        min_supported_exts += 1;
                        continue;
                    }
                }
                for max in VK_MAX_EXT {
                    if max == e.extension_name_as_c_str().unwrap() {
                        max_supported_exts += 1;
                        continue;
                    }
                }
            }
            if min_supported_exts != VK_MIN_EXT.len() {
                continue;
            }
            if max_supported_exts == VK_MAX_EXT.len() {
                max_choice = pdev;
                continue;
            }
            min_choice = pdev;
        }
        if max_choice.is_null() {
            (min_choice, GPUProfile::Min)
        } else {
            (max_choice, GPUProfile::Max)
        }
    }

    fn get_queue_indices(
        instance: &ash::Instance,
        pdev: &vk::PhysicalDevice,
        surface_loader: &Option<khr::surface::Instance>,
        surface: &Option<vk::SurfaceKHR>,
    ) -> (u32, u32, u32, Option<u32>) {
        let queue_families = unsafe { instance.get_physical_device_queue_family_properties(*pdev) };
        let mut gfx_queue_i: Option<u32> = None;
        let mut com_queue_i: Option<u32> = None;
        let mut trf_queue_i: Option<u32> = None;
        let mut pnt_queue_i: Option<u32> = None;
        for (i, qf) in queue_families.iter().enumerate() {
            if qf.queue_flags.contains(vk::QueueFlags::GRAPHICS) {
                gfx_queue_i = Some(i as u32);
            } else if qf.queue_flags.contains(vk::QueueFlags::COMPUTE) {
                com_queue_i = Some(i as u32);
            } else if qf.queue_flags.contains(vk::QueueFlags::TRANSFER) {
                trf_queue_i = Some(i as u32);
            }
            if surface_loader.is_some() && surface.is_some() {
                let present = unsafe {
                    surface_loader
                        .as_ref()
                        .unwrap()
                        .get_physical_device_surface_support(
                            *pdev,
                            i as u32,
                            *surface.as_ref().unwrap(),
                        )
                }
                .unwrap();
                if present {
                    pnt_queue_i = Some(i as u32);
                }
            }
            if gfx_queue_i.is_some() && com_queue_i.is_some() && trf_queue_i.is_some() {
                break;
            }
        }

        if gfx_queue_i.is_none()
            || com_queue_i.is_none()
            || trf_queue_i.is_none()
            || (surface_loader.is_some() && surface.is_some() && pnt_queue_i.is_none())
        {
            panic!("Failed to get supported GPU queues!");
        }
        println!(
            "GFX:{}, COM:{}, TRF:{}, PNT:{}",
            gfx_queue_i.unwrap(),
            com_queue_i.unwrap(),
            trf_queue_i.unwrap(),
            pnt_queue_i.unwrap_or(999),
        );
        (
            gfx_queue_i.unwrap(),
            com_queue_i.unwrap(),
            trf_queue_i.unwrap(),
            pnt_queue_i,
        )
    }

    fn create_device(
        instance: &ash::Instance,
        pdev: &vk::PhysicalDevice,
        unique_indices: HashSet<u32>,
        profile: &GPUProfile,
        surface_support: bool,
    ) -> ash::Device {
        let priority = [1.0];
        let unique_indices_vec: Vec<_> = unique_indices
            .into_iter()
            .map(|i| {
                vk::DeviceQueueCreateInfo::default()
                    .queue_family_index(i)
                    .queue_priorities(&priority)
            })
            .collect();

        let features = vk::PhysicalDeviceFeatures::default().sampler_anisotropy(true);

        let mut features_12 = vk::PhysicalDeviceVulkan12Features::default()
            .descriptor_indexing(true)
            .buffer_device_address(true)
            .separate_depth_stencil_layouts(true)
            .imageless_framebuffer(true);

        let mut features_13 = vk::PhysicalDeviceVulkan13Features::default()
            .maintenance4(true)
            .synchronization2(true)
            .dynamic_rendering(true);

        // VK_MAX_EXT
        let mut acceleration_structure =
            vk::PhysicalDeviceAccelerationStructureFeaturesKHR::default()
                .acceleration_structure(true);
        let mut ray_tracing_pipeline =
            vk::PhysicalDeviceRayTracingPipelineFeaturesKHR::default().ray_tracing_pipeline(true);
        let mut ray_query = vk::PhysicalDeviceRayQueryFeaturesKHR::default().ray_query(true);

        let features_2 = match profile {
            GPUProfile::Min => vk::PhysicalDeviceFeatures2::default()
                .features(features)
                .push(&mut features_13)
                .push(&mut features_12),
            GPUProfile::Max => vk::PhysicalDeviceFeatures2::default()
                .features(features)
                .push(&mut ray_query)
                .push(&mut ray_tracing_pipeline)
                .push(&mut acceleration_structure)
                .push(&mut features_13)
                .push(&mut features_12),
        };

        let mut extensions: Vec<_> = match profile {
            GPUProfile::Min => VK_MIN_EXT.into_iter().map(|x| x.as_ptr()).collect(),
            GPUProfile::Max => {
                let min: Vec<_> = VK_MIN_EXT.into_iter().map(|x| x.as_ptr()).collect();
                let max: Vec<_> = VK_MAX_EXT.into_iter().map(|x| x.as_ptr()).collect();
                [min, max].concat()
            }
        };

        if surface_support {
            extensions.push(khr::swapchain::NAME.as_ptr());
        }

        let mut device_info = vk::DeviceCreateInfo::default()
            .queue_create_infos(&unique_indices_vec)
            .enabled_extension_names(&extensions);
        device_info.p_next = &features_2 as *const _ as *const c_void;

        unsafe { instance.create_device(*pdev, &device_info, None) }
            .expect("Failed to create VkDevice! This should not happen!")
    }

    fn create_device_with_queues(
        instance: &ash::Instance,
        pdev: &vk::PhysicalDevice,
        profile: &GPUProfile,
        surface_loader: &Option<khr::surface::Instance>,
        surface: &Option<vk::SurfaceKHR>,
    ) -> (
        ash::Device,
        QueueInfo,
        QueueInfo,
        QueueInfo,
        Option<QueueInfo>,
    ) {
        let (gfx, com, trf, pnt) = Self::get_queue_indices(instance, pdev, surface_loader, surface);
        let mut unique_indices = HashSet::new();
        unique_indices.insert(gfx);
        unique_indices.insert(com);
        unique_indices.insert(trf);
        if pnt.is_some() {
            unique_indices.insert(pnt.unwrap());
        }

        let device = Self::create_device(
            instance,
            pdev,
            unique_indices,
            profile,
            surface_loader.is_some() && surface.is_some(),
        );

        let gfx_q = unsafe { device.get_device_queue(gfx, 0) };
        let com_q = unsafe { device.get_device_queue(com, 0) };
        let trf_q = unsafe { device.get_device_queue(trf, 0) };
        let pnt_q = pnt.and_then(|i| unsafe { Some(device.get_device_queue(i, 0)) });

        (
            device,
            QueueInfo {
                queue: gfx_q,
                index: gfx,
            },
            QueueInfo {
                queue: com_q,
                index: com,
            },
            QueueInfo {
                queue: trf_q,
                index: trf,
            },
            pnt.and_then(|i| {
                Some(QueueInfo {
                    queue: pnt_q.unwrap(),
                    index: i,
                })
            }),
        )
    }

    pub fn new(opt: InterfaceOptions) -> Self {
        let entry = Self::load();
        let version = Self::get_vk_version(&entry);

        let (instance, debug_support) = Self::create_instance(&entry, opt.raw_display_handle);
        let (debug_loader, debug_msg) = if debug_support {
            let (loader, msg) = Self::create_debug(&entry, &instance);
            (Some(loader), Some(msg))
        } else {
            (None, None)
        };

        let (surface_loader, surface) =
            if opt.raw_display_handle.is_some() && opt.raw_window_handle.is_some() {
                let (loader, surface) = Self::create_surface(
                    &entry,
                    &instance,
                    opt.raw_display_handle.unwrap(),
                    opt.raw_window_handle.unwrap(),
                );
                (Some(loader), Some(surface))
            } else {
                (None, None)
            };

        let (physical_device, gpu_profile) = Self::get_physical_device(&instance);
        if physical_device.is_null() {
            panic!("Failed to get supported GPU!");
        }

        let (device, graphics_queue, compute_queue, transfer_queue, present_queue) =
            Self::create_device_with_queues(
                &instance,
                &physical_device,
                &gpu_profile,
                &surface_loader,
                &surface,
            );

        let swapchain = if surface_loader.is_some() && surface.is_some() {
            let mut indices = vec![
                graphics_queue.index,
                compute_queue.index,
                transfer_queue.index,
            ];
            if present_queue.is_some() {
                indices.push(present_queue.as_ref().unwrap().index);
            }
            Some(Swapchain::new(
                &instance,
                device.clone(),
                physical_device.clone(),
                surface_loader.as_ref().unwrap().clone(),
                surface.as_ref().unwrap().clone(),
                &indices,
                opt.extent,
                opt.surface_format,
                opt.present_mode,
            ))
        } else {
            None
        };

        Self {
            version,
            gpu_profile,
            entry,
            instance,
            debug_loader,
            debug_msg,
            surface_loader,
            surface,
            device,
            graphics_queue,
            compute_queue,
            transfer_queue,
            present_queue,
            swapchain,
        }
    }
}

impl Drop for Interface {
    fn drop(&mut self) {
        unsafe {
            if self.swapchain.is_some() {
                self.swapchain.as_mut().unwrap_unchecked().destroy();
            }
            self.device.destroy_device(None);
            if self.surface_loader.is_some() && self.surface.is_some() {
                self.surface_loader
                    .as_ref()
                    .unwrap_unchecked()
                    .destroy_surface(self.surface.unwrap_unchecked(), None);
            }
            if self.debug_msg.is_some() && self.debug_loader.is_some() {
                self.debug_loader
                    .as_ref()
                    .unwrap_unchecked()
                    .destroy_debug_utils_messenger(self.debug_msg.unwrap_unchecked(), None);
            }
            self.instance.destroy_instance(None);
        }
    }
}

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
