use std::{cell::RefCell, rc::Rc};

use ash::vk;
use gpu_allocator::{
    vulkan::{Allocation, AllocationCreateDesc, AllocationScheme, Allocator},
    MemoryLocation,
};

use crate::{Dev, Error};

pub struct BufferRequestInfo<'a> {
    pub allocator: &'a Rc<RefCell<Allocator>>,
    pub device: &'a Rc<Dev>,
    pub linear: bool,
    pub location: MemoryLocation,
    pub dedicated: bool,
    pub info: &'a vk::BufferCreateInfo<'a>,
}

pub struct Buffer {
    pub buf: vk::Buffer,
    pub alloc: Option<Allocation>,

    allocator: Rc<RefCell<Allocator>>,
    device: Rc<Dev>,
}

impl Buffer {
    pub fn new(info: &BufferRequestInfo) -> Result<Self, Error> {
        let buf = unsafe { info.device.dev.create_buffer(&info.info, None) }
            .map_err(|_| Error::BufferCreate)?;

        let requirements = unsafe { info.device.dev.get_buffer_memory_requirements(buf) };

        let alloc_desc = AllocationCreateDesc {
            name: Default::default(),
            requirements,
            location: info.location,
            linear: info.linear,
            allocation_scheme: if info.dedicated {
                AllocationScheme::DedicatedBuffer(buf)
            } else {
                AllocationScheme::GpuAllocatorManaged
            },
        };

        let alloc = info
            .allocator
            .borrow_mut()
            .allocate(&alloc_desc)
            .map_err(|_| Error::AllocationError)?;

        unsafe {
            info.device
                .dev
                .bind_buffer_memory(buf, alloc.memory(), alloc.offset())
        }
        .map_err(|_| Error::MemoryBindingError)?;

        Ok(Self {
            buf,
            alloc: Some(alloc),
            allocator: Rc::clone(&info.allocator),
            device: Rc::clone(&info.device),
        })
    }
}

impl Drop for Buffer {
    fn drop(&mut self) {
        let alloc = self.alloc.take().unwrap();
        self.allocator.borrow_mut().free(alloc).unwrap();
        unsafe { self.device.dev.destroy_buffer(self.buf, None) };
    }
}

pub struct ImageRequestInfo<'a> {
    pub allocator: &'a Rc<RefCell<Allocator>>,
    pub device: &'a Rc<Dev>,
    pub linear: bool,
    pub location: MemoryLocation,
    pub dedicated: bool,
    pub img_info: &'a vk::ImageCreateInfo<'a>,
    pub img_view_info: &'a vk::ImageViewCreateInfo<'a>,
}

pub struct Image {
    pub img: vk::Image,
    pub img_view: vk::ImageView,
    pub alloc: Option<Allocation>,

    pub format: vk::Format,
    pub extent: vk::Extent3D,
    pub mip_level: u32,

    allocator: Rc<RefCell<Allocator>>,
    device: Rc<Dev>,
}

impl Image {
    pub fn new(info: &ImageRequestInfo) -> Result<Self, Error> {
        let img = unsafe { info.device.dev.create_image(&info.img_info, None) }
            .map_err(|_| Error::ImageCreate)?;

        let requirements = unsafe { info.device.dev.get_image_memory_requirements(img) };

        let alloc_desc = AllocationCreateDesc {
            name: Default::default(),
            requirements,
            location: info.location,
            linear: info.linear,
            allocation_scheme: if info.dedicated {
                AllocationScheme::DedicatedImage(img)
            } else {
                AllocationScheme::GpuAllocatorManaged
            },
        };

        let alloc = info
            .allocator
            .borrow_mut()
            .allocate(&alloc_desc)
            .map_err(|_| Error::AllocationError)?;

        unsafe {
            info.device
                .dev
                .bind_image_memory(img, alloc.memory(), alloc.offset())
        }
        .map_err(|_| Error::MemoryBindingError)?;

        let mut imgv_info = *info.img_view_info;
        imgv_info.image = img;

        let img_view = unsafe { info.device.dev.create_image_view(&imgv_info, None) }
            .map_err(|_| Error::ImageViewCreate)?;

        Ok(Self {
            img,
            img_view,
            alloc: Some(alloc),
            mip_level: info.img_info.mip_levels,
            format: info.img_info.format,
            extent: info.img_info.extent,
            allocator: Rc::clone(&info.allocator),
            device: Rc::clone(&info.device),
        })
    }
}

impl Drop for Image {
    fn drop(&mut self) {
        unsafe { self.device.dev.destroy_image_view(self.img_view, None) };
        let alloc = self.alloc.take().unwrap();
        self.allocator.borrow_mut().free(alloc).unwrap();
        unsafe { self.device.dev.destroy_image(self.img, None) };
    }
}
