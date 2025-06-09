use std::fmt::Display;

use ash::vk;

#[derive(Debug)]
pub enum Error {
    Load,
    UnsupportedSurface,
    InstanceCreate,
    DebugCreate,
    SurfaceCreate,
    NoVulkanGPUFound,
    NoGPUExtensionsFound,
    NoRequestedGPUExtensions,
    NoRequestedGPUQueues,
    DeviceCreate,
    NoSurfaceFormatFound,
    NoPresentModeFound,
    NoSurfaceCapabilitiesFound,
    NoSwapchainImages,
    SwapchainCreate,
    DeviceLost,
    AllocationError,
    MemoryBindingError,
    BufferCreate,
    ImageCreate,
    ImageViewCreate,
    Result(vk::Result),
}

impl std::error::Error for Error {}
impl Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self)
    }
}

impl From<vk::Result> for Error {
    fn from(value: vk::Result) -> Self {
        Error::Result(value)
    }
}
