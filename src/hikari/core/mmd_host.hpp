#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hikari::core {

struct HostObject {
    std::size_t id = 0;
    bool is_pmd = true;
    std::int32_t index = 0;
    std::int32_t order = 0;
    std::string filename;
    std::int32_t mat_count = 0;
    bool disp = true;
};

float get_frame_time();
std::int32_t get_pmd_num();
std::string get_pmd_filename(std::int32_t index);
std::int32_t get_pmd_order(std::int32_t index);
std::int32_t get_pmd_mat_num(std::int32_t index);
bool get_pmd_disp(std::int32_t index);
std::size_t get_pmd_id(std::int32_t index);

std::int32_t get_acs_num();
std::string get_acs_filename(std::int32_t index);
std::int32_t get_acs_order(std::int32_t index);
std::int32_t get_acs_mat_num(std::int32_t index);
bool get_acs_disp(std::int32_t index);
std::size_t get_acs_id(std::int32_t index);

std::int32_t get_current_object();
std::int32_t get_current_material();
std::int32_t get_current_technic();
bool is_english_mode();

std::vector<HostObject> enumerate_host_objects();
bool is_mmd_bound();
std::uint64_t get_host_objects_fingerprint();

} // namespace hikari::core
