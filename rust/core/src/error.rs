use std::fmt::Display;

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
}

impl std::error::Error for Error {}
impl Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self)
    }
}
