#include <contract/runtime/RuntimeObservation.hpp>

#include <contract/runtime/RuntimeSession.hpp>

namespace contract::runtime {

RuntimeObservation RuntimeSession::observe() const {
    RuntimeObservation observation;
    observation.mission = world_.mission_id();
    observation.map = world_.map_id();
    observation.navigation_graph = world_.navigation_graph();
    observation.completed_ticks = clock_.completed_ticks();
    observation.simulation_step = clock_.step();
    observation.clock_remainder = clock_.remainder();
    observation.all_objectives_complete = world_.all_objectives_complete();

    const auto entities = world_.entities();
    observation.entities.reserve(entities.size());
    for (const auto& entity : entities) {
        observation.entities.push_back(
            {
                entity.definition.id,
                entity.definition.transform,
                entity.definition.components,
                entity.enabled
            });
    }

    const auto objectives = world_.objectives();
    observation.objectives.reserve(objectives.size());
    for (const auto& objective : objectives) {
        observation.objectives.push_back(
            {
                objective.definition.id,
                objective.definition.target_references,
                objective.definition.spawn_references,
                objective.progress
            });
    }

    observation.next_event_sequence = event_journal_.next_sequence();
    const auto retained_events = event_journal_.events();
    observation.retained_events.assign(
        retained_events.begin(),
        retained_events.end());
    return observation;
}

}
