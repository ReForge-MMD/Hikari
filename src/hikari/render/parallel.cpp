#include "hikari/render/parallel.hpp"

#include <thread>

namespace hikari::render {

Microsoft::WRL::ComPtr<ID3D11CommandList> ParallelFrameRenderer::record_single_pass(
    core::D3D11Context& context, SceneRenderer& renderer, ID3D11DeviceContext* deferred,
    const ScenePassItem& pass) {
    if (pass.kind == ScenePassItem::Kind::Grid) {
        return renderer.record_grid(context, deferred);
    }
    return renderer.record_mesh_subsets(context, deferred, *pass.mesh, *pass.materials,
                                        *pass.textures, *pass.skinning, pass.first_subset,
                                        pass.last_subset);
}

std::vector<Microsoft::WRL::ComPtr<ID3D11CommandList>>
ParallelFrameRenderer::record_passes_parallel(core::D3D11Context& context, SceneRenderer& renderer,
                                              const std::vector<ScenePassItem>& passes) {
    if (passes.empty()) {
        return {};
    }
    if (passes.size() == 1) {
        auto deferred = context.acquire_deferred_context();
        auto list = record_single_pass(context, renderer, deferred.Get(), passes[0]);
        context.recycle_deferred_context(std::move(deferred));
        return {std::move(list)};
    }

    std::vector<Microsoft::WRL::ComPtr<ID3D11DeviceContext>> deferred_contexts;
    deferred_contexts.reserve(passes.size());
    for (std::size_t i = 0; i < passes.size(); ++i) {
        deferred_contexts.push_back(context.acquire_deferred_context());
    }

    std::vector<Microsoft::WRL::ComPtr<ID3D11CommandList>> results(passes.size());
    std::vector<std::thread> workers;
    for (std::size_t i = 0; i < passes.size(); ++i) {
        workers.emplace_back([&, i] {
            results[i] = record_single_pass(context, renderer, deferred_contexts[i].Get(),
                                            passes[i]);
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    for (auto& deferred : deferred_contexts) {
        context.recycle_deferred_context(std::move(deferred));
    }
    return results;
}

} // namespace hikari::render
