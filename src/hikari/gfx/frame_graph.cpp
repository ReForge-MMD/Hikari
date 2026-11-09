#include "hikari/gfx/frame_graph.hpp"

#include <utility>

namespace hikari::gfx {

void FrameGraph::add_external(std::string name) {
    externals_.insert(std::move(name));
}

void FrameGraph::add_pass(std::string name, std::vector<std::string> reads,
                          std::vector<std::string> writes, std::function<void()> execute) {
    passes_.push_back({std::move(name), std::move(reads), std::move(writes), std::move(execute)});
}

bool FrameGraph::validate(std::string* error) const {
    std::unordered_set<std::string> produced = externals_;
    std::unordered_set<std::string> names;
    for (const Pass& pass : passes_) {
        if (pass.name.empty()) {
            if (error != nullptr) {
                *error = "pass has an empty name";
            }
            return false;
        }
        if (!names.insert(pass.name).second) {
            if (error != nullptr) {
                *error = "duplicate pass name: " + pass.name;
            }
            return false;
        }
        for (const std::string& read : pass.reads) {
            if (produced.find(read) == produced.end()) {
                if (error != nullptr) {
                    *error = "pass '" + pass.name + "' reads '" + read +
                             "' before any producer";
                }
                return false;
            }
        }
        for (const std::string& write : pass.writes) {
            if (!produced.insert(write).second) {
                if (error != nullptr) {
                    *error = "resource '" + write + "' has more than one producer";
                }
                return false;
            }
        }
    }
    return true;
}

void FrameGraph::execute() const {
    for (const Pass& pass : passes_) {
        if (pass.execute) {
            pass.execute();
        }
    }
}

void FrameGraph::clear() {
    passes_.clear();
    externals_.clear();
}

} // namespace hikari::gfx
