#pragma once

#include <contract/mission/Mission.hpp>
#include <contract/runtime/RuntimeEventJournal.hpp>
#include <contract/runtime/RuntimeWorld.hpp>
#include <contract/scene/Scene.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace contract::runtime {

struct RuntimeEntityObservation {
    scene::EntityId id;
    scene::Transform transform;
    std::vector<scene::ComponentReference> components;
    bool enabled{true};
};

struct RuntimeObjectiveObservation {
    mission::ObjectiveId id;
    std::vector<scene::EntityId> target_references;
    std::vector<scene::EntityId> spawn_references;
    ObjectiveProgress progress{ObjectiveProgress::pending};
};

struct RuntimeObservation {
    mission::MissionId mission;
    scene::MapId map;
    std::optional<navigation::NavigationGraph> navigation_graph;
    std::uint64_t completed_ticks{0};
    std::chrono::nanoseconds simulation_step{0};
    std::chrono::nanoseconds clock_remainder{0};
    bool all_objectives_complete{false};
    std::vector<RuntimeEntityObservation> entities;
    std::vector<RuntimeObjectiveObservation> objectives;
    std::uint64_t next_event_sequence{0};
    std::vector<SequencedRuntimeEvent> retained_events;
};

}
