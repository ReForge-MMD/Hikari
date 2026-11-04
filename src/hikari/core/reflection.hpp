#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace hikari::core {

struct ReflectedVariable {
    std::string name;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

struct ReflectedCBuffer {
    std::string name;
    std::uint32_t slot = 0;
    std::uint32_t size = 0;
    std::unordered_map<std::string, ReflectedVariable> variables;
};

enum class ReflectedResourceType {
    CBuffer,
    Texture,
    Sampler,
    Uav,
    StructuredBuffer,
    ByteAddressBuffer,
    Other,
};

struct ReflectedResource {
    std::string name;
    ReflectedResourceType type = ReflectedResourceType::Texture;
    std::uint32_t bind_point = 0;
    std::uint32_t bind_count = 0;
    std::uint32_t return_type = 0;
    std::uint32_t dimension = 0;
};

struct ReflectedInputParam {
    std::string semantic_name;
    std::uint32_t semantic_index = 0;
    std::uint32_t register_index = 0;
    std::uint32_t component_type = 0;
    std::uint8_t mask = 0;
};

struct ShaderReflectionData {
    std::vector<ReflectedCBuffer> cbuffers;
    std::vector<ReflectedResource> resources;
    std::vector<ReflectedInputParam> input_parameters;
    std::array<std::uint32_t, 3> thread_group_size{1, 1, 1};

    static ShaderReflectionData reflect(const std::vector<std::uint8_t>& bytecode);

    std::optional<std::uint32_t> find_texture_slot(const std::string& name) const;
    std::optional<std::uint32_t> find_sampler_slot(const std::string& name) const;
    std::optional<std::uint32_t> find_uav_slot(const std::string& name) const;
};

} // namespace hikari::core
