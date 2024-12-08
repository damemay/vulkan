use std::ffi::CStr;

use ash::{khr, vk};

pub enum GPUProfile {
    Min,
    Max,
}

pub const VK_API: u32 = vk::API_VERSION_1_3;

pub const VK_MAX_EXT: [&CStr; 6] = [
    khr::pipeline_library::NAME,
    khr::acceleration_structure::NAME,
    khr::shader_float_controls::NAME,
    khr::ray_tracing_pipeline::NAME,
    khr::ray_query::NAME,
    khr::deferred_host_operations::NAME,
];

pub const VK_MIN_EXT: [&CStr; 1] = [khr::push_descriptor::NAME];
