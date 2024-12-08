#include <filesystem>
#include <format>
#include <fstream>
#include <slang-com-ptr.h>
#include <slang.h>
#include <spirv_reflect.h>
#include <stdio.h>
#include <vector>
#include <vulkan/vulkan.h>

std::filesystem::path abs_exe_directory() {
#if defined(_MSC_VER)
    wchar_t path[FILENAME_MAX] = {0};
    GetModuleFileNameW(nullptr, path, FILENAME_MAX);
    return std::filesystem::path(path).parent_path();
#else
    char path[FILENAME_MAX];
    ssize_t count = readlink("/proc/self/exe", path, FILENAME_MAX);
    return std::filesystem::path(std::string(path, (count > 0) ? count : 0)).parent_path();
#endif
}

struct descriptor_info {
    uint32_t set;
    VkDescriptorSetLayoutCreateInfo ci;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
};

struct reflected_spirv {
    std::vector<descriptor_info> reflected_sets;
    Slang::ComPtr<slang::IBlob> spirv;
};

struct slang_session_info {
    SlangCompileTarget target_format = SLANG_SPIRV;
    const char *target_profile_name = "spirv_1_5";
    std::vector<const char *> paths = {};
    std::vector<const char *> modules = {};
};

struct slang_compiler {
    Slang::ComPtr<slang::IGlobalSession> global_session;
    Slang::ComPtr<slang::ISession> session;
    Slang::ComPtr<slang::IBlob> diagnostics;
    std::vector<reflected_spirv> output;

    slang_compiler() { assert(createGlobalSession(global_session.writeRef()) == SLANG_OK); }

    bool compile(slang_session_info info) {
        slang::TargetDesc target = {
            .format = info.target_format,
            .profile = global_session->findProfile(info.target_profile_name),
        };

        slang::SessionDesc sess_desc = {
            .targets = &target,
            .targetCount = 1,
            .searchPaths = info.paths.data(),
            .searchPathCount = (SlangInt)info.paths.size(),
        };

        if (global_session->createSession(sess_desc, session.writeRef()) != SLANG_OK)
            return false;

        for (auto name : info.modules)
            if (!handle_module(name))
                return false;

        diagnostics.setNull();
        session.setNull();

        return true;
    }

    ~slang_compiler() = default;

  private:
    bool handle_module(const char *name) {
        auto module = session->loadModule(name, diagnostics.writeRef());

        if (diagnostics) {
            fprintf(stderr, "%s\n", (const char *)diagnostics->getBufferPointer());
            diagnostics.setNull();
        }

        if (!module)
            return false;

        Slang::ComPtr<slang::IEntryPoint> entry_point;
        if (module->findEntryPointByName("main", entry_point.writeRef()) != SLANG_OK)
            return false;

        slang::IComponentType *components[] = {module, entry_point};
        Slang::ComPtr<slang::IComponentType> program;
        if (session->createCompositeComponentType(components, 2, program.writeRef()) != SLANG_OK)
            return false;

        Slang::ComPtr<slang::IComponentType> linked_program;
        auto res = program->link(linked_program.writeRef(), diagnostics.writeRef());

        if (diagnostics) {
            fprintf(stderr, "%s\n", (const char *)diagnostics->getBufferPointer());
            diagnostics.setNull();
        }
        if (res != SLANG_OK)
            return false;

        Slang::ComPtr<slang::IBlob> spirv;
        res = linked_program->getEntryPointCode(0, 0, spirv.writeRef(), diagnostics.writeRef());
        if (diagnostics) {
            fprintf(stderr, "%s\n", (const char *)diagnostics->getBufferPointer());
            diagnostics.setNull();
        }
        if (res != SLANG_OK)
            return false;

        auto reflection = reflect_spirv(spirv->getBufferPointer(), spirv->getBufferSize());

        output.push_back({reflection, spirv});

        return true;
    }

    std::vector<descriptor_info> reflect_spirv(const void *buffer, const size_t size) {
        SpvReflectShaderModule module = {};
        SpvReflectResult res = spvReflectCreateShaderModule(size, buffer, &module);
        assert(res == SPV_REFLECT_RESULT_SUCCESS);

        uint32_t count = 0;
        res = spvReflectEnumerateDescriptorSets(&module, &count, nullptr);
        assert(res == SPV_REFLECT_RESULT_SUCCESS);

        std::vector<SpvReflectDescriptorSet *> sets(count);
        res = spvReflectEnumerateDescriptorSets(&module, &count, sets.data());
        assert(res == SPV_REFLECT_RESULT_SUCCESS);

        std::vector<descriptor_info> set_layouts(sets.size(), descriptor_info{});

        for (size_t i = 0; i < sets.size(); i++) {
            const SpvReflectDescriptorSet &r_set = *(sets[i]);
            descriptor_info &layout = set_layouts[i];

            layout.set = r_set.set;
            layout.ci.bindingCount = r_set.binding_count;
            layout.bindings.resize(r_set.binding_count);

            for (size_t b = 0; b < r_set.binding_count; b++) {
                const SpvReflectDescriptorBinding &r_bind = *(r_set.bindings[b]);
                VkDescriptorSetLayoutBinding l_binding = layout.bindings[b];

                l_binding.binding = r_bind.binding;
                l_binding.descriptorType = (VkDescriptorType)r_bind.descriptor_type;
                l_binding.descriptorCount = 1;

                for (size_t d = 0; d < r_bind.array.dims_count; ++d)
                    l_binding.descriptorCount *= r_bind.array.dims[d];

                auto stage = (VkShaderStageFlagBits)module.shader_stage;
            }

            layout.ci.pBindings = layout.bindings.data();
        }

        spvReflectDestroyShaderModule(&module);

        return set_layouts;
    }
};

int main() {
    auto compiler = slang_compiler();

    std::vector<const char *> modules = {"hello"};
    slang_session_info info = {
        .paths = {"../"},
        .modules = modules,
    };

    assert(compiler.compile(info));

    for (auto &set : compiler.output[0].reflected_sets) {
        printf("set layout:\n");
        printf("\tset: %d, ", set.set);
        printf("\tbinding:\n");
        for (auto &binding : set.bindings) {
            printf("\t\tbinding: %d, ", binding.binding);
            printf("descriptors: %d\n", binding.descriptorCount);
        }
        puts("");
    }

    // save
    // for (size_t i = 0; i < compiler.output.size(); i++) {
    //     std::ofstream out{std::format("../{}.spv", modules[i]), std::ios::binary};
    //     out.write((char *)compiler.output[i].spirv->getBufferPointer(),
    //               compiler.output[i].spirv->getBufferSize());
    //     out.close();
    // }
}
