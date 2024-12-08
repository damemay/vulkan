#pragma once
#define GLM_FORCE_RADIANS
#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <variant>
#include <memory>
#include <unordered_set>
#include <glm/gtx/hash.hpp>
#include <glm/vector_relational.hpp>
#include <glm/packing.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/transform.hpp>
#include <stb/stb_image.h>
#include <format>
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <vb.h>
#include <filesystem>

struct GLTF {
    vb::Context* ctx;

    struct Camera {
        float aspect_ratio;
        float fov;
        float far;
        float near;
	glm::vec3 position;
	glm::quat rotation;
	glm::mat4 matrix;
    };

    enum class LightType {
	EmissiveMaterial,
	Directional,
	Spot,
	Point
    };

    LightType parse_fastgltf_lighttype(const fastgltf::LightType& type) {
	switch(type) {
	    case fastgltf::LightType::Directional: return LightType::Directional;
	    case fastgltf::LightType::Spot: return LightType::Spot;
	    case fastgltf::LightType::Point: return LightType::Point;
	}
	return LightType::EmissiveMaterial;
    }

    template<typename A, typename B>
    std::optional<A> parse_fastgltf_optional(const fastgltf::Optional<B> opt) {
	if(opt.has_value()) return (A)opt.value();
	return std::nullopt;
    }

    struct Light {
	LightType type;
	glm::vec3 position;
	glm::vec3 direction;
	glm::vec3 color;
	float intensity;
	std::optional<float> range;
	std::optional<float> inner_cone_angle;
	std::optional<float> outer_cone_angle;
    };

    struct DirectionalLight {
	glm::vec3 direction;
	glm::vec3 color;
	float intensity;
    };

    struct Vertex {
	glm::vec3 position;
	float uv_x;
	glm::vec3 normal;
	float uv_y;
	glm::vec4 tangent;
    };

    struct Primitive {
        uint32_t first_index;
        uint32_t index_count;
	std::optional<uint32_t> material_index;
    };

    struct Mesh {
        std::vector<Primitive> primitives;
    };

    struct Node;
    struct Node {
        Node* parent;
        std::vector<std::unique_ptr<Node>> children;
        glm::mat4 matrix;
	std::optional<Mesh> mesh;
    };

    struct Material {
        glm::vec4 base_color_factor {1.0f};
        float metallic_factor {1.0f};
        float roughness_factor {1.0f};
        std::optional<uint32_t> base_color_tex_index;
        std::optional<uint32_t> metallic_roughness_tex_index;
        std::optional<uint32_t> normal_tex_index;
	VkDescriptorSet descriptor;
    };

    struct Image {
	vb::Image image;
    };

    vb::DescriptorPool descriptor;
    VkDescriptorSetLayout descriptor_layout;
    VkSampler sampler;

    vb::Buffer vertices;
    vb::Buffer indices;

    std::optional<Camera> first_camera;
    std::vector<Camera> cameras;
    std::vector<Light> lights;
    std::vector<Image> images;
    std::vector<uint32_t> textures;
    std::vector<Material> materials;
    std::vector<std::unique_ptr<Node>> nodes;

    GLTF(vb::Context* context): ctx{context}, descriptor{context},
        vertices{context}, indices{context} {}

    void load(const std::filesystem::path& path) {
        vb::log(std::format("Loading {}...", path.string()));
        fastgltf::Parser parser {fastgltf::Extensions::KHR_lights_punctual};
        auto data = fastgltf::GltfDataBuffer::FromPath(path);
        assert(data.error() == fastgltf::Error::None);
        auto options = fastgltf::Options::LoadExternalBuffers
            | fastgltf::Options::GenerateMeshIndices
            | fastgltf::Options::DecomposeNodeMatrices;
        auto parent_path = path.parent_path();
        auto asset = parser.loadGltf(data.get(), parent_path, options);
        assert(asset.error() == fastgltf::Error::None);

	load_images(asset.get(), parent_path);
	load_textures(asset.get());
	load_materials(asset.get());
	create_dummy_textures();
	load_nodes(asset.get());

        vb::log(std::format("Camera {}", first_camera.has_value() ? "found" : "not found"));
        vb::log("All GLTF data loaded");
        if (images.size() > 0) {
            setup_sampler();
            setup_descriptors();
        }
        vb::log("GLTF object created");
    }

    void clean() {
        if(!textures.empty()) {
            vkDestroySampler(ctx->device, sampler, nullptr);
	    descriptor.clean_layout(descriptor_layout);
	    descriptor.clean();
        }
        vertices.clean();
        indices.clean();
        for(auto& image: images) image.image.clean();
    }
    
    void load_textures(const fastgltf::Asset& asset) {
	textures.resize(asset.textures.size());
        for(size_t i = 0; i < asset.textures.size(); i++)
	    textures[i] = asset.textures[i].imageIndex.value();
    }

    void load_images(const fastgltf::Asset& asset, const std::filesystem::path& parent_path) {
	images.resize(asset.images.size(), {ctx});
	for(size_t i = 0; i < asset.images.size(); i++) {
	    int w, h, c;
	    const auto& data = asset.images[i].data;
	    if(const auto& uri = std::get_if<fastgltf::sources::URI>(&data); uri) {
    		auto path = std::format("{}/{}", parent_path.c_str(), uri->uri.c_str());
		vb::log(std::format("Loading {}...", path.c_str()));
    		assert(uri->fileByteOffset == 0);
    		assert(uri->uri.isLocalPath());
    		images[i].image.create(path.c_str());
    		assert(images[i].image.all_valid());
    	    } else if(const auto& vector = std::get_if<fastgltf::sources::Vector>(&data); vector) {
    		auto data = stbi_load_from_memory((stbi_uc*)vector->bytes.data(),
		    (int)vector->bytes.size(), &w, &h, &c, 4);
		assert(data);
    		VkExtent3D size = {(uint32_t)w, (uint32_t)h, 1};
    		images[i].image.create(data, size);
    		assert(images[i].image.all_valid());
    		stbi_image_free(data);
    	    } else if(const auto& view = std::get_if<fastgltf::sources::BufferView>(&data); view) {
		auto& bfview = asset.bufferViews[view->bufferViewIndex];
		auto& bf = asset.buffers[bfview.bufferIndex];
		const auto& v = std::get_if<fastgltf::sources::Array>(&bf.data);
		auto data = stbi_load_from_memory((stbi_uc*)v->bytes.data() + bfview.byteOffset,
	    	    bfview.byteLength, &w, &h, &c, 4);
		assert(data);
    		VkExtent3D size = {(uint32_t)w, (uint32_t)h, 1};
    		images[i].image.create(data, size);
    		assert(images[i].image.all_valid());
    		stbi_image_free(data);
	    }
	}
    }

    void load_materials(const fastgltf::Asset& asset) {
	materials.resize(asset.materials.size());
	for(size_t i = 0; i < asset.materials.size(); i++) {
    	    const auto& material = asset.materials[i];
 	    materials[i].base_color_factor = glm::make_vec4(material.pbrData.baseColorFactor.data());
  	    materials[i].metallic_factor = material.pbrData.metallicFactor;
   	    materials[i].roughness_factor = material.pbrData.roughnessFactor;
 	    if(material.pbrData.baseColorTexture.has_value())
  		materials[i].base_color_tex_index = material.pbrData.baseColorTexture->textureIndex;
   	    if(material.pbrData.metallicRoughnessTexture.has_value())
    		materials[i].metallic_roughness_tex_index = material.pbrData.metallicRoughnessTexture->textureIndex;
     	    if(material.normalTexture.has_value())
     		materials[i].normal_tex_index = material.normalTexture->textureIndex;
	}
    }

    void create_dummy_textures() {
	std::vector<glm::vec4> colors;
	for(auto& material: materials) {
	    if(!material.base_color_tex_index.has_value())
		colors.push_back(material.base_color_factor);
	    if(!material.metallic_roughness_tex_index.has_value())
		colors.push_back(glm::vec4(1.0f, material.roughness_factor,
			    material.metallic_factor, 1.0f));
	    if(!material.normal_tex_index.has_value())
		colors.push_back(glm::vec4(1.0f));
	}
	for(auto& n: colors) vb::log(std::format("{} {} {} {}", n.x, n.y, n.z, n.w));
	std::unordered_set<glm::vec4> textures_needed {colors.begin(), colors.end()};
	for(auto& n: textures_needed) vb::log(std::format("{} {} {} {}", n.x, n.y, n.z, n.w));

	std::unordered_map<glm::vec4, uint32_t> texmap;
	for(auto& vec: textures_needed) {
	    uint32_t data = glm::packUnorm4x8(vec);
	    vb::Image newtex {ctx};
	    newtex.create((void*)&data, VkExtent3D{1,1,1});
	    assert(newtex.all_valid());
	    images.push_back({newtex});
	    uint32_t imgidx = images.size() - 1;
	    textures.push_back(imgidx);
	    uint32_t texidx = textures.size() - 1;
	    texmap.insert({vec, texidx});
	}

	for(auto& material: materials) {
	    if(!material.base_color_tex_index.has_value()) {
		if(auto i = texmap.find(material.base_color_factor); i != texmap.end()) {
		    material.base_color_tex_index = i->second;
		}
	    }
	    if(!material.metallic_roughness_tex_index.has_value()) {
		auto vec = glm::vec4(1.0f, material.roughness_factor,
			material.metallic_factor, 1.0f);
		if(auto i = texmap.find(vec); i != texmap.end()) {
		    material.metallic_roughness_tex_index = i->second;
		}
	    }
	    if(!material.normal_tex_index.has_value()) {
		if(auto i = texmap.find(glm::vec4(1.0f)); i != texmap.end()) {
		    material.normal_tex_index = i->second;
		}
	    }
	}
    }

    void load_nodes(const fastgltf::Asset& asset) {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        for(auto& node: asset.nodes) load_node(node, nullptr, asset, vertices, indices);
	create_buffers(vertices, indices);
    }

    void load_node(const fastgltf::Node& node_in, GLTF::Node* parent, 
	    const fastgltf::Asset& asset, std::vector<Vertex>& vertex_vec,
	    std::vector<uint32_t>& index_vec) {
	auto node = std::make_unique<Node>();
    	node->matrix = glm::mat4(1.0f);
    	node->parent = parent;

	if(auto trs = std::get_if<fastgltf::TRS>(&node_in.transform); trs) {
	    node->matrix = glm::translate(node->matrix, glm::make_vec3(trs->translation.data()));
	    node->matrix *= glm::mat4(glm::make_quat(trs->rotation.data()));
	    node->matrix = glm::scale(node->matrix, glm::make_vec3(trs->scale.data()));
	}

    	if(node_in.children.size() > 0) {
    	    for(const auto& child_idx: node_in.children)
    		load_node(asset.nodes[child_idx], node.get(), asset, vertex_vec, index_vec);
    	}

    	if(node_in.meshIndex.has_value()) {
	    load_mesh(asset.meshes[node_in.meshIndex.value()], node.get(), asset, vertex_vec, index_vec);
    	}

    	if(parent) parent->children.push_back(std::move(node));
    	else nodes.push_back(std::move(node));
    }

    void load_mesh(const fastgltf::Mesh& mesh, GLTF::Node* node, 
	    const fastgltf::Asset& asset, std::vector<Vertex>& vertex_vec,
	    std::vector<uint32_t>& index_vec) {
	for(const auto& prim: mesh.primitives) {
	    uint32_t first_index = index_vec.size();
	    uint32_t vertex_start = vertex_vec.size();
	    uint32_t index_count = asset.accessors[prim.indicesAccessor.value()].count;
	    size_t v_idx = 0;
	    { // INDEX
		auto& acc = asset.accessors[prim.indicesAccessor.value()];
	       	index_vec.reserve(index_vec.size() + acc.count);
     		fastgltf::iterateAccessor<uint32_t>(asset, acc, [&](uint32_t i) {
		    index_vec.push_back(i + vertex_start);
		});
	    } { // POSITION
		auto& acc = asset.accessors[prim.findAttribute("POSITION")->accessorIndex];
		vertex_vec.resize(vertex_vec.size() + acc.count);
		fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, acc, [&](glm::vec3 v, size_t i) {
    		    v_idx = i;
		    Vertex vert = {.position = v, .normal = {1,0,0}};
		    vertex_vec[vertex_start + i] = vert;
		});
	    } { // NORMALS
		auto att = prim.findAttribute("NORMAL");
		if(att != prim.attributes.end()) {
		    fastgltf::iterateAccessorWithIndex<glm::vec3>(asset,
		    asset.accessors[att->accessorIndex], [&](glm::vec3 v, size_t i) {
    			vertex_vec[vertex_start + i].normal = v;
		    });
		}
	    } { // TANGENT
		auto att = prim.findAttribute("TANGENT");
		if(att != prim.attributes.end()) {
		    fastgltf::iterateAccessorWithIndex<glm::vec4>(asset,
		    asset.accessors[att->accessorIndex], [&](glm::vec4 v, size_t i) {
    			vertex_vec[vertex_start + i].tangent = v;
		    });
		}
	    } { // UV
		auto att = prim.findAttribute("TEXCOORD_0");
		if(att != prim.attributes.end()) {
		    fastgltf::iterateAccessorWithIndex<glm::vec2>(asset,
		    asset.accessors[att->accessorIndex], [&](glm::vec2 v, size_t i) {
		   	vertex_vec[vertex_start + i].uv_x = v.x;
		   	vertex_vec[vertex_start + i].uv_y = v.y;
		    });
		}
	    }
	    Primitive primitive = {
	        .first_index = first_index,
	        .index_count = index_count,
	    };
	    if(prim.materialIndex.has_value())
	        primitive.material_index = prim.materialIndex.value();
	    node->mesh->primitives.push_back(primitive);
	}
    }

    void create_buffers(std::vector<Vertex>& vertex_vec, std::vector<uint32_t>& index_vec) {
        vb::log("Creating buffers...");
        size_t vertices_size = vertex_vec.size() * sizeof(Vertex);
        this->vertices.create(vertices_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        	| VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        	VMA_MEMORY_USAGE_GPU_ONLY);
        assert(this->vertices.all_valid());
        size_t indices_size = index_vec.size() * sizeof(uint32_t);
        this->indices.create(indices_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT
        	| VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        assert(this->indices.all_valid());
        vb::Buffer staging_buffer {ctx};
        staging_buffer.create(vertices_size + indices_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        	VMA_MEMORY_USAGE_CPU_ONLY);
        assert(staging_buffer.all_valid());
        char* buf_data = (char*)staging_buffer.info.pMappedData;
        memcpy(buf_data, vertex_vec.data(), vertices_size);
        memcpy(buf_data + vertices_size, index_vec.data(), indices_size);
        vb::log("Copying data to buffers...");
        ctx->submit_command_to_queue([&](VkCommandBuffer cmd) {
            VkBufferCopy copy = { .size = vertices_size };
            vkCmdCopyBuffer(cmd, staging_buffer.buffer, this->vertices.buffer, 1, &copy);
            VkBufferCopy copy2 = {
        	.srcOffset = vertices_size,
        	.size = indices_size,
            };
            vkCmdCopyBuffer(cmd, staging_buffer.buffer, this->indices.buffer, 1, &copy2);
        });
        staging_buffer.clean();
    }

    void setup_descriptors() {
	std::vector<VkDescriptorPoolSize> sizes = {
	    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (uint32_t)materials.size() * 3}
	};
    	descriptor.create(sizes, images.size());
	descriptor.add_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		VK_SHADER_STAGE_FRAGMENT_BIT, 0);
	descriptor.add_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		VK_SHADER_STAGE_FRAGMENT_BIT, 1);
	descriptor.add_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		VK_SHADER_STAGE_FRAGMENT_BIT, 2);
	descriptor_layout = descriptor.create_layout();
	assert(descriptor_layout);
	for(auto& material: materials) {
	    material.descriptor = descriptor.create_set(descriptor_layout);
	    assert(material.descriptor);
	    assert(material.base_color_tex_index.has_value());
	    assert(material.metallic_roughness_tex_index.has_value());
	    assert(material.normal_tex_index.has_value());
	    VkDescriptorImageInfo color_info = {
		.sampler = sampler,
		.imageView = images[textures[material.base_color_tex_index.value()]]
		    .image.image_view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	    };
	    VkWriteDescriptorSet color_write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = material.descriptor,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = &color_info,
	    };
	    VkDescriptorImageInfo metalrough_info = {
		.sampler = sampler,
		.imageView = images[textures[material.metallic_roughness_tex_index.value()]]
		    .image.image_view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	    };
	    VkWriteDescriptorSet metalrough_write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = material.descriptor,
		.dstBinding = 1,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = &metalrough_info,
	    };
	    VkDescriptorImageInfo normal_info = {
		.sampler = sampler,
		.imageView = images[textures[material.normal_tex_index.value()]]
		    .image.image_view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	    };
	    VkWriteDescriptorSet normal_write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = material.descriptor,
		.dstBinding = 2,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = &normal_info,
	    };
	    VkWriteDescriptorSet writes[3] = {color_write, metalrough_write, normal_write};
	    vkUpdateDescriptorSets(ctx->device, 3, writes, 0, nullptr);
	}
    }

    void setup_sampler() {
	VkPhysicalDeviceProperties pdev_prop{};
    	vkGetPhysicalDeviceProperties(ctx->physical_device, &pdev_prop);
	VkSamplerCreateInfo sampler_info = {
	    .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
	    .magFilter = VK_FILTER_LINEAR,
	    .minFilter = VK_FILTER_LINEAR,
	    .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
	    .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
	    .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
	    .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
	    .mipLodBias = 0.0f,
	    .anisotropyEnable = VK_TRUE,
	    .maxAnisotropy = pdev_prop.limits.maxSamplerAnisotropy,
	    .compareEnable = VK_FALSE,
	    .compareOp = VK_COMPARE_OP_ALWAYS,
	    .minLod = 0.0f,
	    .maxLod = 0.0f,
	    .borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE,
	    .unnormalizedCoordinates = VK_FALSE,
	};
	assert(vkCreateSampler(ctx->device, &sampler_info, nullptr, &sampler) == VK_SUCCESS);
    }
};
