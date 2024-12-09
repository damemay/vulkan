use crate::error::Error;
use crate::surface::Surface;
use ash::vk::{self, Handle};
use std::{
    collections::{HashMap, HashSet},
    ffi::{c_void, CStr},
};

#[derive(Default)]
pub struct QueueRequestInfo {
    pub flags: vk::QueueFlags,
    pub present: bool,
}

#[derive(Clone)]
pub struct QueueInfo {
    pub queue: vk::Queue,
    pub index: u32,
    pub present: bool,
}

pub struct DevRequestInfo<'a> {
    pub queues: Vec<QueueRequestInfo>,
    pub extensions: Vec<&'a CStr>,
    pub preferred_type: Option<vk::PhysicalDeviceType>,
    pub features: Option<vk::PhysicalDeviceFeatures>,
    pub features_2: Option<vk::PhysicalDeviceFeatures2<'a>>,
}

pub type QueueMap = HashMap<vk::QueueFlags, QueueInfo>;

#[derive(Clone)]
pub struct Dev {
    pub dev: ash::Device,
    pub pdev: vk::PhysicalDevice,
    pub queues: QueueMap,
    pub unique_indices: Vec<u32>,
}

impl Dev {
    pub fn new(
        instance: &ash::Instance,
        surface: Option<&Surface>,
        info: DevRequestInfo,
    ) -> Result<Self, Error> {
        let pdev = Self::get_physical_device(instance, &info)?;
        let queues = Self::get_queue_indices(instance, &pdev, surface, &info)?;
        let unique_indices: Vec<u32> = {
            let set: HashSet<u32> = queues.iter().map(|x| x.1.index).collect();
            set.into_iter().collect()
        };
        let dev = Self::create_device(instance, &pdev, &unique_indices, &info)?;
        Ok(Self {
            dev,
            pdev,
            queues,
            unique_indices,
        })
    }

    fn get_physical_device(
        instance: &ash::Instance,
        info: &DevRequestInfo,
    ) -> Result<vk::PhysicalDevice, Error> {
        let pdevices = unsafe { instance.enumerate_physical_devices() }
            .map_err(|_| Error::NoVulkanGPUFound)?;
        let mut ext_choice = vk::PhysicalDevice::null();
        let mut type_choice = vk::PhysicalDevice::null();
        for pdev in pdevices {
            let exts = unsafe { instance.enumerate_device_extension_properties(pdev) }
                .map_err(|_| Error::NoGPUExtensionsFound)?;
            let mut ext_count = 0;
            for pe in exts {
                for &ue in &info.extensions {
                    if ue == pe.extension_name_as_c_str().unwrap() {
                        ext_count += 1;
                        continue;
                    }
                }
                for &ue in &info.extensions {
                    if ue == pe.extension_name_as_c_str().unwrap() {
                        ext_count += 1;
                        continue;
                    }
                }
            }
            if ext_count != info.extensions.len() {
                continue;
            }
            if info.preferred_type.is_some() {
                let prop = unsafe { instance.get_physical_device_properties(pdev) };
                if prop.device_type == info.preferred_type.unwrap() {
                    type_choice = pdev;
                    break;
                }
            }
            ext_choice = pdev;
        }
        if type_choice.is_null() && !ext_choice.is_null() {
            Ok(ext_choice)
        } else if !type_choice.is_null() && ext_choice.is_null() {
            Ok(type_choice)
        } else {
            Err(Error::NoRequestedGPUExtensions)
        }
    }

    fn get_queue_indices(
        instance: &ash::Instance,
        pdev: &vk::PhysicalDevice,
        surface: Option<&Surface>,
        info: &DevRequestInfo,
    ) -> Result<QueueMap, Error> {
        let queue_families = unsafe { instance.get_physical_device_queue_family_properties(*pdev) };
        let mut queues = HashMap::new();

        for (i, qf) in queue_families.iter().enumerate() {
            for uqf in &info.queues {
                if qf.queue_flags.contains(uqf.flags) {
                    let index = i as u32;
                    let mut present = false;

                    if surface.is_some() && uqf.present {
                        present = unsafe {
                            surface.unwrap().loader.get_physical_device_surface_support(
                                *pdev,
                                i as u32,
                                surface.unwrap().surface,
                            )
                        }
                        .unwrap();
                    }

                    queues.insert(
                        uqf.flags,
                        QueueInfo {
                            queue: vk::Queue::null(),
                            index,
                            present,
                        },
                    );
                }
            }
        }

        if queues.len() != info.queues.len() {
            Err(Error::NoRequestedGPUQueues)
        } else {
            Ok(queues)
        }
    }

    fn create_device(
        instance: &ash::Instance,
        pdev: &vk::PhysicalDevice,
        unique_indices: &Vec<u32>,
        info: &DevRequestInfo,
    ) -> Result<ash::Device, Error> {
        let priority = [1.0];
        let unique_indices_vec: Vec<_> = unique_indices
            .iter()
            .map(|&i| {
                vk::DeviceQueueCreateInfo::default()
                    .queue_family_index(i)
                    .queue_priorities(&priority)
            })
            .collect();

        let extensions: Vec<_> = info.extensions.iter().map(|x| x.as_ptr()).collect();

        let mut device_info = vk::DeviceCreateInfo::default()
            .queue_create_infos(&unique_indices_vec)
            .enabled_extension_names(&extensions);

        if info.features.is_some() {
            device_info.p_enabled_features = &info.features.unwrap();
        }

        if info.features_2.is_some() {
            device_info.p_next = &info.features_2 as *const _ as *const c_void;
        }

        unsafe { instance.create_device(*pdev, &device_info, None) }
            .map_err(|_| Error::DeviceCreate)
    }
}

impl Drop for Dev {
    fn drop(&mut self) {
        unsafe { self.dev.destroy_device(None) }
    }
}
