#include "hikari/animation/jiggle.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace hikari::animation {
namespace {

const std::vector<std::string> kChestKeywords = {"胸", "乳", "おっぱい", "bust", "breast"};
const std::vector<std::string> kHipsKeywords = {"尻", "ヒップ", "臀", "hip", "butt"};
const std::vector<std::string> kNotSoftKeywords = {
    "上半身", "下半身", "胸元", "ネクタイ", "リボン", "腰", "花", "飾", "IK", "ＩＫ"};

bool contains_any(const std::string& text, const std::vector<std::string>& needles) {
    for (const auto& needle : needles) {
        if (text.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> words(const std::string& name) {
    std::vector<std::string> out;
    std::string current;
    for (unsigned char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            current.push_back(static_cast<char>(std::tolower(c)));
        } else if (!current.empty()) {
            out.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) {
        out.push_back(current);
    }
    return out;
}

bool keyword_match(const std::string& name, const std::vector<std::string>& latin,
                   const std::vector<std::string>& keywords) {
    for (const auto& keyword : keywords) {
        bool ascii = true;
        for (unsigned char c : keyword) {
            if (c >= 0x80) {
                ascii = false;
                break;
            }
        }
        if (ascii) {
            if (std::find(latin.begin(), latin.end(), keyword) != latin.end()) {
                return true;
            }
        } else if (name.find(keyword) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::optional<Region> region_of(const std::string& name) {
    if (contains_any(name, kNotSoftKeywords)) {
        return std::nullopt;
    }
    const auto latin = words(name);
    if (keyword_match(name, latin, kChestKeywords)) {
        return Region::Chest;
    }
    if (keyword_match(name, latin, kHipsKeywords)) {
        return Region::Hips;
    }
    return std::nullopt;
}

int side_of(const std::string& name) {
    const auto latin = words(name);
    if (name.find("左") != std::string::npos ||
        std::find(latin.begin(), latin.end(), "l") != latin.end() ||
        std::find(latin.begin(), latin.end(), "left") != latin.end()) {
        return -1;
    }
    if (name.find("右") != std::string::npos ||
        std::find(latin.begin(), latin.end(), "r") != latin.end() ||
        std::find(latin.begin(), latin.end(), "right") != latin.end()) {
        return 1;
    }
    return 0;
}

} // namespace

Jiggle::Jiggle(const formats::PmxModel& pmx) {
    struct Best {
        Region region;
        int side;
        std::size_t bone;
    };
    std::vector<Best> best;
    for (std::size_t i = 0; i < pmx.bones.size(); ++i) {
        const auto region = region_of(pmx.bones[i].name_local);
        if (!region) {
            continue;
        }
        const int side = side_of(pmx.bones[i].name_local);
        if (side == 0) {
            continue;
        }
        const bool seen = std::any_of(best.begin(), best.end(), [&](const Best& entry) {
            return entry.region == *region && entry.side == side;
        });
        if (!seen) {
            best.push_back({*region, side, i});
        }
    }

    for (const auto& entry : best) {
        Body body;
        body.bone = entry.bone;
        body.region = entry.region;
        const auto& source = pmx.bones[entry.bone];
        if (source.parent_index >= 0) {
            body.frame = static_cast<std::size_t>(source.parent_index);
        }
        bodies_.push_back(body);
    }

    float low = std::numeric_limits<float>::max();
    float high = std::numeric_limits<float>::lowest();
    for (const auto& bone : pmx.bones) {
        low = std::min(low, bone.position[1]);
        high = std::max(high, bone.position[1]);
    }
    height_ = std::max(high - low, 1.0f);
    overrides_.assign(pmx.bones.size(), std::nullopt);
}

void Jiggle::reset() {
    primed_ = false;
}

const std::vector<std::optional<Vec3>>* Jiggle::step(const std::vector<Vec3>& world_positions,
                                                     const std::vector<Quat>& world_rotations,
                                                     float dt,
                                                     const JiggleSettings& settings) {
    if (!settings.enabled || settings.strength <= 0.0f || bodies_.empty()) {
        return nullptr;
    }

    const bool restart = !primed_ || !(dt > 0.0f) || dt > 0.25f;
    primed_ = true;

    const int substeps = std::clamp(static_cast<int>(std::ceil(dt / (1.0f / 240.0f))), 1, 16);
    const float h = dt / static_cast<float>(substeps);
    std::fill(overrides_.begin(), overrides_.end(), std::nullopt);

    for (auto& body : bodies_) {
        const Vec3 target =
            body.bone < world_positions.size() ? world_positions[body.bone] : Vec3::zero();
        const Quat frame = body.frame && *body.frame < world_rotations.size()
                               ? world_rotations[*body.frame]
                               : Quat::identity();
        const Spring& spring = body.region == Region::Chest ? settings.chest : settings.hips;

        if (restart) {
            body.position = target;
            body.velocity = Vec3::zero();
            body.offset = Vec3::zero();
        } else {
            const float omega = 6.28318530717958647692f * spring.frequency;
            const Vec3 k(spring.stiffness[0] * omega * omega,
                         spring.stiffness[1] * omega * omega,
                         spring.stiffness[2] * omega * omega);
            const Vec3 c(std::sqrt(spring.stiffness[0]) * 2.0f * spring.damping * omega,
                         std::sqrt(spring.stiffness[1]) * 2.0f * spring.damping * omega,
                         std::sqrt(spring.stiffness[2]) * 2.0f * spring.damping * omega);
            const Vec3 start = body.position - body.offset;
            const Quat inv_frame = frame.inverse();

            for (int s = 1; s <= substeps; ++s) {
                const float a = static_cast<float>(s) / static_cast<float>(substeps);
                const Vec3 anchor = start + (target - start) * a;
                const Vec3 d = inv_frame.rotate_vec3(body.position - anchor);
                const Vec3 v = inv_frame.rotate_vec3(body.velocity);
                const Vec3 acc(-k.x * d.x - c.x * v.x,
                               -k.y * d.y - c.y * v.y,
                               -k.z * d.z - c.z * v.z);
                body.velocity = body.velocity + frame.rotate_vec3(acc) * h;
                body.position = body.position + body.velocity * h;
            }

            const float max_d = spring.max_offset * height_;
            Vec3 offset = body.position - target;
            const float len = offset.length();
            if (len > max_d && len > 1.0e-6f) {
                offset = offset * (max_d / len);
                body.position = target + offset;
                const Vec3 normal = offset.normalize();
                const float dot = body.velocity.dot(normal);
                if (dot > 0.0f) {
                    body.velocity = body.velocity - normal * dot;
                }
            }
            body.offset = offset;
        }

        if (body.bone < overrides_.size()) {
            overrides_[body.bone] = body.offset * settings.strength;
        }
    }

    return &overrides_;
}

} // namespace hikari::animation
