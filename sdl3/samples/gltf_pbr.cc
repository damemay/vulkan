#define GLM_FORCE_RADIANS
#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <vb.h>
#include "gltf_pbr.h"
#include "app.h"

struct PushConstants {
    glm::mat4 model;
};

struct View {
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec4 position;
};

struct Lights {
    glm::vec4 position;
};

struct GltfTextures : public App {
    vb::GraphicsPipeline gfx_pipeline {&vbc};
    GLTF mesh {&vbc};

    vb::DescriptorPool ubo_pool {&vbc};
    VkDescriptorSet ubo_set;
    VkDescriptorSetLayout ubo_set_layout;
    vb::Buffer ubo {&vbc};
    vb::Buffer ubo2 {&vbc};

    struct {
	View view;
	Lights lights;
    } scene_data;

    GltfTextures(): App{} {
	vb::ContextInstanceWindowInfo windowinfo = {
	    .title = "vbc",
	    .width = 1280,
	    .height = 720,
	    .window_flags = SDL_WINDOW_HIDDEN,
	    .vulkan_api = VK_API_VERSION_1_3,
	};
	//windowinfo.require_debug();
	vb::ContextDeviceInfo deviceinfo = {
    	    .vk10features = {.samplerAnisotropy = VK_TRUE},
    	    .vk12features = {
    	    },
    	    .vk13features = {
    	        .dynamicRendering = VK_TRUE,
    	    },
    	};
	vb::ContextSwapchainInfo swapchaininfo = {
	    .present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR,
	};
	create(windowinfo, deviceinfo, swapchaininfo,0);
	load_mesh();
	setup_ubo();
	init_pipelines();
	interactive_camera.move_speed = 0.01f;
	scene_data.lights.position = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
	memcpy(ubo2.info.pMappedData, &scene_data.lights, sizeof(Lights));
    }

    ~GltfTextures() {
	mesh.clean();
	gfx_pipeline.clean();
	gfx_pipeline.clean_shaders();
	ubo_pool.clean_layout(ubo_set_layout);
	ubo_pool.clean();
	ubo.clean();
	ubo2.clean();
    }

    void setup_ubo() {
	VkDescriptorPoolSize sizes[1] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2}};
	ubo_pool.create(sizes, 2);
	assert(ubo_pool.all_valid());
	ubo_pool.add_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0);
	ubo_pool.add_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 1);
	ubo_set_layout = ubo_pool.create_layout();
	assert(ubo_set_layout);
	ubo_set = ubo_pool.create_set(ubo_set_layout);
	assert(ubo_set);
	ubo.create(sizeof(View), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	assert(ubo.all_valid());
	ubo2.create(sizeof(Lights), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	assert(ubo2.all_valid());
	VkDescriptorBufferInfo info[2] = {
	    {
		.buffer = ubo.buffer,
		.offset = 0,
		.range = sizeof(View),
	    },
	    {
		.buffer = ubo2.buffer,
		.offset = 0,
		.range = sizeof(Lights),
	    }
	};
	VkWriteDescriptorSet write[2] = {
	    {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = ubo_set,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = &info[0],
	    },
	    {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = ubo_set,
		.dstBinding = 1,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = &info[1],
	    }
	};
	vkUpdateDescriptorSets(vbc.device, 2, write, 0, nullptr);
    }

    void init_pipelines() {
	VkVertexInputBindingDescription bind_desc = {
	    .binding = 0,
	    .stride = sizeof(GLTF::Vertex),
	    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};

	std::vector<VkVertexInputAttributeDescription> attr_desc = {
	    {
		.location = 0,
		.binding = 0,
		.format = VK_FORMAT_R32G32B32_SFLOAT,
		.offset = offsetof(GLTF::Vertex, position),
	    },
	    {
		.location = 1,
		.binding = 0,
		.format = VK_FORMAT_R32_SFLOAT,
		.offset = offsetof(GLTF::Vertex, uv_x),
	    },
	    {
		.location = 2,
		.binding = 0,
		.format = VK_FORMAT_R32G32B32_SFLOAT,
		.offset = offsetof(GLTF::Vertex, normal),
	    },
	    {
		.location = 3,
		.binding = 0,
		.format = VK_FORMAT_R32_SFLOAT,
		.offset = offsetof(GLTF::Vertex, uv_y),
	    },
	    {
		.location = 4,
		.binding = 0,
		.format = VK_FORMAT_R32G32B32A32_SFLOAT,
		.offset = offsetof(GLTF::Vertex, tangent),
	    },
	};
	gfx_pipeline.vertex_input.vertexBindingDescriptionCount = 1;
	gfx_pipeline.vertex_input.pVertexBindingDescriptions = &bind_desc;
	gfx_pipeline.vertex_input.vertexAttributeDescriptionCount = 5;
	gfx_pipeline.vertex_input.pVertexAttributeDescriptions = attr_desc.data();
	gfx_pipeline.set_front_face(VK_FRONT_FACE_COUNTER_CLOCKWISE);
	gfx_pipeline.enable_blend();
	gfx_pipeline.enable_depth_test();
	gfx_pipeline.set_depth_comparison(VK_COMPARE_OP_GREATER_OR_EQUAL);

	gfx_pipeline.add_shader("../samples/shaders/pbr.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
	gfx_pipeline.add_shader("../samples/shaders/pbr.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
	gfx_pipeline.add_push_constant(sizeof(PushConstants), VK_SHADER_STAGE_VERTEX_BIT);
	gfx_pipeline.add_descriptor_set_layout(mesh.descriptor_layout);
	gfx_pipeline.add_descriptor_set_layout(ubo_set_layout);
	VkPipelineRenderingCreateInfo info = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
	    .colorAttachmentCount = 1,
	    .pColorAttachmentFormats = &vbc.swapchain_format,
	    .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT,
	};
	gfx_pipeline.create(&info, 0);
	assert(gfx_pipeline.all_valid());
    }

    void load_mesh() {
	mesh.load("../samples/sponza/glTF/Sponza.gltf");
	assert(mesh.vertices.all_valid()&&mesh.indices.all_valid());
    }

    void render_node(VkCommandBuffer cmd, GLTF::Node* node) {
	if(node->mesh->primitives.size() == 0) return;
	auto node_matrix = node->matrix;
	auto parent = node->parent;
	while(parent) {
	    node_matrix = parent->matrix * node_matrix;
	    parent = parent->parent;
	}
	auto push_constants = PushConstants{node_matrix};
	vkCmdPushConstants(cmd, gfx_pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT,
		0, sizeof(PushConstants), &push_constants);
	for(auto& primitive: node->mesh->primitives) {
	    if(primitive.index_count == 0) continue;
	    if(primitive.material_index.has_value()) {
		auto descriptor = mesh.materials[primitive.material_index.value()].descriptor;
		VkDescriptorSet descriptors[2] = {descriptor, ubo_set};
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
		    gfx_pipeline.layout, 0, 2, descriptors, 0, nullptr);
	    }
	    vkCmdDrawIndexed(cmd, primitive.index_count, 1, primitive.first_index, 0, 0);
    	    stats.drawcalls++;
	    stats.triangles += primitive.index_count/3;
	}
	for(auto& child: node->children) render_node(cmd, child.get());
    }

    VkImageLayout render(VkCommandBuffer cmd, VkImageLayout input_layout, uint32_t index) {
	stats.drawcalls = 0;
	stats.triangles = 0;
	auto start = std::chrono::high_resolution_clock::now();

	if(!interactive_camera.lock && interactive_camera.use)
	    interactive_camera.update(stats.frametime);
	if(interactive_camera.use) {
	    scene_data.view.view = interactive_camera.view();
	    scene_data.view.projection = interactive_camera.projection();
	    scene_data.view.position = glm::vec4(interactive_camera.position, 1.0f);
	    memcpy(ubo.info.pMappedData, &scene_data.view, sizeof(View));
	}

	vb::transition_image(cmd, render_target.image, VK_IMAGE_LAYOUT_UNDEFINED, 
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	vb::transition_image(cmd, depth_target.image, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
	VkClearValue color[2] = {
	    {.color = {0.0f, 0.0f, 0.0f, 1.0f}},
	    {.depthStencil = {0.0f, 0}},
	};
	VkRenderingAttachmentInfo color_attach = {
	    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
	    .imageView = render_target.image_view,
	    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
	    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
	    .clearValue = color[0],
	};
	VkRenderingAttachmentInfo depth_attach = {
	    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
	    .imageView = depth_target.image_view,
	    .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
	    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
	    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
	    .clearValue = color[1],
	};
	VkRenderingInfo rendering = {
	    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
	    .renderArea = {{0,0}, render_extent},
	    .layerCount = 1,
	    .colorAttachmentCount = 1,
	    .pColorAttachments = &color_attach,
	    .pDepthAttachment = &depth_attach,
	};
	vkCmdBeginRendering(cmd, &rendering);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gfx_pipeline.pipeline);
	VkViewport viewport = {0.0f, 0.0f, (float)render_extent.width,
	    (float)render_extent.height, 0.0f, 1.0f};
	vkCmdSetViewport(cmd, 0, 1, &viewport);
	VkRect2D scissor {{0,0}, render_extent};
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	vkCmdBindIndexBuffer(cmd, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
	VkDeviceSize offsets[1] = {0};
	vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertices.buffer, offsets);
	for(const auto& node: mesh.nodes) render_node(cmd, node.get());

	vkCmdEndRendering(cmd);
	auto end = std::chrono::high_resolution_clock::now();
    	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	stats.draw_time = elapsed.count() / 1000.0f;

	vb::transition_image(cmd, render_target.image,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	vb::transition_image(cmd, vbc.swapchain_images[index], input_layout,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	vb::blit_image(cmd, render_target.image, vbc.swapchain_images[index],
		{render_extent.width, render_extent.height, 1},
		{vbc.swapchain_extent.width, vbc.swapchain_extent.height, 1});

	return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }
};

int main() {
    GltfTextures app {};
    app.run();
}
