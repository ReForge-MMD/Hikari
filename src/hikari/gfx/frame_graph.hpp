#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace hikari::gfx {

// A small deterministic pass graph. Passes declare resource names; validation
// rejects reads before a producer, duplicate writers, and duplicate pass names.
class FrameGraph {
public:
    struct Pass {
        std::string name;
        std::vector<std::string> reads;
        std::vector<std::string> writes;
        std::function<void()> execute;
    };

    void add_external(std::string name);
    void add_pass(std::string name, std::vector<std::string> reads,
                  std::vector<std::string> writes, std::function<void()> execute);

    bool validate(std::string* error = nullptr) const;
    void execute() const;

    const std::vector<Pass>& passes() const { return passes_; }
    void clear();

private:
    std::vector<Pass> passes_;
    std::unordered_set<std::string> externals_;
};

} // namespace hikari::gfx
