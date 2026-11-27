// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/world.hpp"
#include <iostream>
#include <stdexcept>
using namespace opensim;

int main() {
    try {
        for (const bool silent : {false, true}) {
            scene::World world(framework::Uuid::random());
            auto root = scene::SceneObject::primitive(framework::Uuid::random(), {12, 13, 14});
            root = world.add(root).value();
            auto child = scene::SceneObject::primitive(root.owner, {1, 2, 3});
            child.parent_id = root.local_id;
            child = world.add(child).value();
            world.set_selected(child.id, true);
            world.drain_events();
            if (!world.remove_crossing_group(root.id, silent) || world.object(root.id) || world.object(child.id))
                throw std::runtime_error("Crossing did not remove its full linkset");
            const auto events = world.drain_events();
            if (events.size() != 2)
                throw std::runtime_error("Silent crossing lost internal part-removal events");
            for (const auto &event : events)
                if (event.kind != scene::WorldEventKind::Removed || event.suppress_viewer_kill != silent)
                    throw std::runtime_error("Crossing mixed cleanup and viewer deletion flags");
            auto replacement = scene::SceneObject::primitive(root.owner, {20, 20, 20});
            replacement.id = root.id;
            replacement = world.add(replacement).value();
            world.drain_events();
            if (world.teleport_group(replacement.id)->selected)
                throw std::runtime_error("Removed selection leaked to replacement");
            world.remove(replacement.id);
            const auto normal = world.drain_events();
            if (normal.size() != 1 || normal[0].suppress_viewer_kill)
                throw std::runtime_error("Silent crossing suppressed a later ordinary deletion");
            auto unpublished = world.add(scene::SceneObject::primitive(root.owner, {30, 30, 30})).value();
            world.remove(unpublished.id, false);
            if (!world.drain_events().empty())
                throw std::runtime_error("Legacy unpublished rollback behavior changed");
        }
        std::cout
            << "PASS: silent crossing preserves internal removal, ordinary kills and unpublished rollback boundaries\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
