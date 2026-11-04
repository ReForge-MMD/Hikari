#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

#include "hikari/formats/pmx.hpp"
#include "hikari/math.hpp"

namespace hikari::animation {

struct Spring {
    float frequency = 4.2f;
    float damping = 0.22f;
    std::array<float, 3> stiffness{1.6f, 1.0f, 2.2f};
    float max_offset = 0.012f;
};

struct JiggleSettings {
    bool enabled = true;
    float strength = 1.0f;
    Spring chest{4.2f, 0.22f, {1.6f, 1.0f, 2.2f}, 0.012f};
    Spring hips{3.4f, 0.30f, {1.4f, 1.0f, 2.0f}, 0.009f};
};

enum class Region { Chest, Hips };

class Jiggle {
public:
    Jiggle() = default;
    explicit Jiggle(const formats::PmxModel& pmx);

    void reset();
    std::size_t body_count() const { return bodies_.size(); }

    // Returns per-bone position offsets, or nullptr when disabled / inactive.
    const std::vector<std::optional<Vec3>>* step(const std::vector<Vec3>& world_positions,
                                                 const std::vector<Quat>& world_rotations,
                                                 float dt,
                                                 const JiggleSettings& settings);

private:
    struct Body {
        std::size_t bone = 0;
        Region region = Region::Chest;
        std::optional<std::size_t> frame;
        Vec3 position;
        Vec3 velocity;
        Vec3 offset;
    };

    std::vector<Body> bodies_;
    float height_ = 1.0f;
    bool primed_ = false;
    std::vector<std::optional<Vec3>> overrides_;
};

} // namespace hikari::animation
