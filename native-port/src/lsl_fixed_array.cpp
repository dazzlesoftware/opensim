// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_script_class.hpp"
#include <mutex>
#include <unordered_map>

namespace opensim::script {
namespace {
struct Registry {
    std::recursive_mutex mutex;
    std::vector<std::weak_ptr<const LslFixedArray>> arrays;
    std::vector<std::weak_ptr<const LslScriptClass>> classes;
};

Registry &registry() {
    static Registry value;
    return value;
}
} // namespace

std::recursive_mutex &lsl_reference_graph_mutex() {
    return registry().mutex;
}

void register_lsl_script_class(const std::shared_ptr<const LslScriptClass> &value) {
    std::lock_guard lock(registry().mutex);
    registry().classes.emplace_back(value);
}

LslValue make_lsl_fixed_array(LslFixedArrayKind kind, std::size_t length) {
    auto array = std::make_shared<LslFixedArray>();
    array->kind = kind;
    switch (kind) {
    case LslFixedArrayKind::Character:
        array->cells = std::vector<char16_t>(length);
        break;
    case LslFixedArrayKind::Float:
        array->cells = std::vector<double>(length);
        break;
    case LslFixedArrayKind::Integer:
        array->cells = std::vector<std::int32_t>(length);
        break;
    case LslFixedArrayKind::Object:
        array->cells = std::vector<LslValue>(length, LslNullObject{});
        break;
    default:
        throw std::invalid_argument("Invalid fixed array kind");
    }
    auto &r = registry();
    std::lock_guard lock(r.mutex);
    r.arrays.emplace_back(array);
    return std::shared_ptr<const LslFixedArray>(array);
}

std::size_t lsl_fixed_array_size(const std::shared_ptr<const LslFixedArray> &array) {
    if (!array)
        throw std::invalid_argument("Null fixed array carrier");
    if (static_cast<std::size_t>(array->kind) != array->cells.index())
        throw std::invalid_argument("Fixed array kind/storage mismatch");
    return std::visit([](const auto &cells) { return cells.size(); }, array->cells);
}

LslValue lsl_fixed_array_get(const std::shared_ptr<const LslFixedArray> &array, std::size_t index) {
    auto &r = registry();
    std::lock_guard lock(r.mutex);
    if (!array)
        throw std::invalid_argument("Null fixed array carrier");
    return std::visit(
        [&](const auto &cells) -> LslValue {
            using Cell = typename std::decay_t<decltype(cells)>::value_type;
            if constexpr (std::is_same_v<Cell, char16_t>)
                return LslCharacter{cells.at(index)};
            else
                return cells.at(index);
        },
        array->cells);
}

void lsl_fixed_array_set(const std::shared_ptr<const LslFixedArray> &array, std::size_t index, const LslValue &value) {
    auto &r = registry();
    std::lock_guard lock(r.mutex);
    if (!array)
        throw std::invalid_argument("Null fixed array carrier");
    switch (array->kind) {
    case LslFixedArrayKind::Character:
        std::get<std::vector<char16_t>>(array->cells).at(index) =
            static_cast<char16_t>(std::get<LslCharacter>(value).value);
        break;
    case LslFixedArrayKind::Float:
        std::get<std::vector<double>>(array->cells).at(index) = std::get<double>(value);
        break;
    case LslFixedArrayKind::Integer:
        std::get<std::vector<std::int32_t>>(array->cells).at(index) = std::get<std::int32_t>(value);
        break;
    case LslFixedArrayKind::Object:
        std::get<std::vector<LslValue>>(array->cells).at(index) = value;
        break;
    }
}

void collect_lsl_fixed_arrays() noexcept {
    try {
        auto &r = registry();
        std::lock_guard lock(r.mutex);

        struct Node {
            std::shared_ptr<const LslFixedArray> array;
            std::shared_ptr<const LslList> list;
            std::shared_ptr<const LslScriptClass> script_class;
            std::vector<std::size_t> edges;
            std::size_t incoming = 0;
            bool live = false;
        };

        std::vector<Node> nodes;
        std::unordered_map<const void *, std::size_t> indices;
        auto add_array = [&](const auto &ptr) {
            auto [entry, inserted] = indices.emplace(ptr.get(), nodes.size());
            if (inserted)
                nodes.push_back(Node{ptr, {}, {}, {}, 0, false});
            return entry->second;
        };
        for (auto it = r.arrays.begin(); it != r.arrays.end();) {
            if (auto ptr = it->lock()) {
                add_array(ptr);
                ++it;
            } else
                it = r.arrays.erase(it);
        }
        auto add_class = [&](const auto &ptr) {
            auto [entry, inserted] = indices.emplace(ptr.get(), nodes.size());
            if (inserted)
                nodes.push_back(Node{{}, {}, ptr, {}, 0, false});
            return entry->second;
        };
        for (auto it = r.classes.begin(); it != r.classes.end();) {
            if (auto ptr = it->lock()) {
                add_class(ptr);
                ++it;
            } else
                it = r.classes.erase(it);
        }
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            auto trace = [&](const std::vector<LslValue> &cells) {
                for (const auto &cell : cells) {
                    std::optional<std::size_t> target;
                    if (auto a = std::get_if<std::shared_ptr<const LslFixedArray>>(&cell); a && *a)
                        target = add_array(*a);
                    else if (auto c = std::get_if<std::shared_ptr<const LslScriptClass>>(&cell); c && *c)
                        target = add_class(*c);
                    else if (auto list = std::get_if<std::shared_ptr<const LslList>>(&cell); list && *list) {
                        auto [entry, inserted] = indices.emplace(list->get(), nodes.size());
                        const auto index = entry->second;
                        if (inserted)
                            nodes.push_back(Node{{}, *list, {}, {}, 0, false});
                        target = index;
                    }
                    if (target) {
                        nodes[i].edges.push_back(*target);
                        ++nodes[*target].incoming;
                    }
                }
            };
            // Do not retain references into nodes while tracing: discovering a child can reallocate it.
            if (nodes[i].script_class) {
                const auto *instance = nodes[i].script_class.get();
                for (const auto &lane : instance->instance_arrays)
                    trace(lane);
            } else if (nodes[i].list)
                trace(nodes[i].list->items);
            else if (auto cells = std::get_if<std::vector<LslValue>>(&nodes[i].array->cells))
                trace(*cells);
        }
        std::vector<std::size_t> pending;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            const auto count = nodes[i].array  ? nodes[i].array.use_count()
                               : nodes[i].list ? nodes[i].list.use_count()
                                               : nodes[i].script_class.use_count();
            if (count > static_cast<long>(nodes[i].incoming + 1)) {
                nodes[i].live = true;
                pending.push_back(i);
            }
        }
        while (!pending.empty()) {
            const auto i = pending.back();
            pending.pop_back();
            for (auto target : nodes[i].edges)
                if (!nodes[target].live) {
                    nodes[target].live = true;
                    pending.push_back(target);
                }
        }
        for (auto &node : nodes)
            if (!node.live) {
                if (node.array) {
                    if (auto cells = std::get_if<std::vector<LslValue>>(&node.array->cells))
                        cells->clear();
                } else if (node.script_class)
                    for (auto &lane : node.script_class->instance_arrays)
                        lane.clear();
            }
    } catch (...) { /* Allocation failure conservatively retains graphs for a later collection. */
    }
}
} // namespace opensim::script
