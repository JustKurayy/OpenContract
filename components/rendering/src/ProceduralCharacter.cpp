#include <contract/rendering/ProceduralCharacter.hpp>

#include <array>

namespace contract::rendering {
namespace {

void append_box(
    ProceduralCharacterMesh& mesh,
    std::array<float, 3> minimum,
    std::array<float, 3> maximum) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.insert(
        mesh.vertices.end(),
        {
            {minimum[0], minimum[1], minimum[2]},
            {maximum[0], minimum[1], minimum[2]},
            {maximum[0], maximum[1], minimum[2]},
            {minimum[0], maximum[1], minimum[2]},
            {minimum[0], minimum[1], maximum[2]},
            {maximum[0], minimum[1], maximum[2]},
            {maximum[0], maximum[1], maximum[2]},
            {minimum[0], maximum[1], maximum[2]}
        });
    constexpr std::array<std::uint32_t, 36> box_indices{
        0, 2, 1, 0, 3, 2,
        4, 5, 6, 4, 6, 7,
        0, 1, 5, 0, 5, 4,
        3, 7, 6, 3, 6, 2,
        1, 2, 6, 1, 6, 5,
        0, 4, 7, 0, 7, 3
    };
    for (const auto index : box_indices) {
        mesh.indices.push_back(base + index);
    }
}

}

ProceduralCharacterMesh create_procedural_character() {
    ProceduralCharacterMesh mesh;
    mesh.vertices.reserve(32);
    mesh.indices.reserve(144);
    append_box(
        mesh,
        {-24.0F, 0.0F, -12.0F},
        {-4.0F, 65.0F, 12.0F});
    append_box(
        mesh,
        {4.0F, 0.0F, -12.0F},
        {24.0F, 65.0F, 12.0F});
    append_box(
        mesh,
        {-35.0F, 65.0F, -18.0F},
        {35.0F, 145.0F, 18.0F});
    append_box(
        mesh,
        {-23.0F, 145.0F, -23.0F},
        {23.0F, 190.0F, 23.0F});
    return mesh;
}

}
