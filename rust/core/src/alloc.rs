use std::{cell::RefCell, rc::Rc};

use ash::vk;
use gpu_allocator::{
    vulkan::{Allocation, AllocationCreateDesc, AllocationScheme, Allocator},
    MemoryLocation,
};

use crate::{Dev, Error};

pub struct Buffer {
    pub buf: vk::Buffer,
    pub alloc: Option<Allocation>,

    allocator: Rc<RefCell<Allocator>>,
    device: Rc<Dev>,
}

impl Buffer {
    pub fn new(
        allocator: &Rc<RefCell<Allocator>>,
        device: &Rc<Dev>,
        linear: bool,
        location: MemoryLocation,
        dedicated: bool,
        info: &vk::BufferCreateInfo,
    ) -> Result<Self, Error> {
        let buf =
            unsafe { device.dev.create_buffer(&info, None) }.map_err(|_| Error::BufferCreate)?;

        let requirements = unsafe { device.dev.get_buffer_memory_requirements(buf) };

        let alloc_desc = AllocationCreateDesc {
            name: Default::default(),
            requirements,
            location,
            linear,
            allocation_scheme: if dedicated {
                AllocationScheme::DedicatedBuffer(buf)
            } else {
                AllocationScheme::GpuAllocatorManaged
            },
        };

        let alloc = allocator
            .borrow_mut()
            .allocate(&alloc_desc)
            .map_err(|_| Error::AllocationError)?;

        unsafe {
            device
                .dev
                .bind_buffer_memory(buf, alloc.memory(), alloc.offset())
        }
        .map_err(|_| Error::MemoryBindingError)?;

        Ok(Self {
            buf,
            alloc: Some(alloc),
            allocator: Rc::clone(&allocator),
            device: Rc::clone(&device),
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
    pub fn new(
        allocator: &Rc<RefCell<Allocator>>,
        device: &Rc<Dev>,
        linear: bool,
        location: MemoryLocation,
        dedicated: bool,
        img_info: &vk::ImageCreateInfo,
        img_view_info: &vk::ImageViewCreateInfo,
    ) -> Result<Self, Error> {
        let img =
            unsafe { device.dev.create_image(&img_info, None) }.map_err(|_| Error::ImageCreate)?;

        let requirements = unsafe { device.dev.get_image_memory_requirements(img) };

        let alloc_desc = AllocationCreateDesc {
            name: Default::default(),
            requirements,
            location,
            linear,
            allocation_scheme: if dedicated {
                AllocationScheme::DedicatedImage(img)
            } else {
                AllocationScheme::GpuAllocatorManaged
            },
        };

        let alloc = allocator
            .borrow_mut()
            .allocate(&alloc_desc)
            .map_err(|_| Error::AllocationError)?;

        unsafe {
            device
                .dev
                .bind_image_memory(img, alloc.memory(), alloc.offset())
        }
        .map_err(|_| Error::MemoryBindingError)?;

        let mut imgv_info = *img_view_info;
        imgv_info.image = img;

        let img_view = unsafe { device.dev.create_image_view(&imgv_info, None) }
            .map_err(|_| Error::ImageViewCreate)?;

        Ok(Self {
            img,
            img_view,
            alloc: Some(alloc),
            mip_level: img_info.mip_levels,
            format: img_info.format,
            extent: img_info.extent,
            allocator: Rc::clone(&allocator),
            device: Rc::clone(&device),
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
