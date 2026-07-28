#include "TestSupport.hpp"

#include <contract/rendering/ProceduralCharacter.hpp>

#include <algorithm>
#include <cstdint>

int main() {
    const auto mesh =
        contract::rendering::create_procedural_character();

    CONTRACT_EXPECT_EQ(mesh.vertices.size(), std::size_t{32});
    CONTRACT_EXPECT_EQ(mesh.indices.size(), std::size_t{144});
    CONTRACT_EXPECT(
        std::all_of(
            mesh.indices.begin(),
            mesh.indices.end(),
            [&mesh](std::uint32_t index) {
                return index < mesh.vertices.size();
            }));

    float minimum_y = mesh.vertices.front().y;
    float maximum_y = minimum_y;
    for (const auto& vertex : mesh.vertices) {
        minimum_y = std::min(minimum_y, vertex.y);
        maximum_y = std::max(maximum_y, vertex.y);
    }
    CONTRACT_EXPECT_EQ(minimum_y, 0.0F);
    CONTRACT_EXPECT_EQ(maximum_y, 190.0F);

    return contract::test::finish();
}
