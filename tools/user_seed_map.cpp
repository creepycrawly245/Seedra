#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "common/worldPicture.hpp"
#include "structures/__include.hpp"
#include "terrain/biomes/biome.hpp"
#include "terrain/generator.hpp"

namespace {
constexpr int64_t kSeed = -5155870644958840792LL;
constexpr auto kConsole = lce::CONSOLE::XBOX1;
constexpr auto kVersion = LCEVERSION::ELYTRA;
constexpr auto kWorldSize = lce::WORLDSIZE::LARGE;
constexpr auto kBiomeScale = lce::BIOMESCALE::LARGE;

void writePositions(std::ofstream& csv, const std::string& name,
                    const std::vector<Pos2D>& positions) {
    for (const auto& pos : positions) {
        csv << name << ',' << pos.x << ',' << pos.z << ",block\n";
    }
}
}

int main() {
    const std::filesystem::path outDir = "seed_output";
    std::filesystem::create_directories(outDir);

    // Raw seed only. No BalancedSeed function or seed replacement is used anywhere here.
    Biome::registerBiomes();
    Generator generator(kConsole, kVersion, kSeed, kWorldSize, kBiomeScale);
    Placement::setWorldSize(kWorldSize);

    std::ofstream info(outDir / "world_info.txt");
    info << "Seed: " << kSeed << '\n';
    info << "Console: Xbox One (XBOX1)\n";
    info << "Legacy version: ELYTRA\n";
    info << "World size: LARGE\n";
    info << "Biome size: LARGE\n";
    info << "Find Balanced Seed: OFF (raw seed passed directly to Generator)\n";
    info << "World coordinate bounds: +/-" << generator.getWorldCoordinateBounds() << '\n';

    const Pos2D spawn = generator.getSpawnBlock();
    info << "Spawn: " << spawn.x << ", " << spawn.z << '\n';

    std::cout << "Generating full biome image..." << std::endl;
    WorldPicture picture(&generator);
    picture.drawBiomes();
    picture.save((outDir.string() + "/"));

    std::ofstream csv(outDir / "structures.csv");
    csv << "structure,x,z,coordinate_kind\n";
    csv << "spawn," << spawn.x << ',' << spawn.z << ",block\n";

    std::cout << "Finding villages..." << std::endl;
    writePositions(csv, "village", Placement::Village<false>::getAllPositions(&generator));

    std::cout << "Finding scattered features (desert/jungle temples, witch huts, igloos)..." << std::endl;
    const auto features = Placement::Feature::getAllFeaturePositions(&generator);
    for (const auto& feature : features) {
        csv << getStructureName(feature.m_type) << ','
            << feature.m_pos.x << ',' << feature.m_pos.z << ",block\n";
    }

    std::cout << "Finding woodland mansions..." << std::endl;
    writePositions(csv, "woodland_mansion", Placement::Mansion::getAllPositions(&generator));

    std::cout << "Finding ocean monuments..." << std::endl;
    writePositions(csv, "ocean_monument", Placement::Monument::getAllPositions(&generator));

    std::cout << "Finding strongholds..." << std::endl;
    writePositions(csv, "stronghold", Placement::Stronghold::getWorldPositions(generator));

    std::cout << "Finding mineshafts..." << std::endl;
    const auto mineshafts = Placement::Mineshaft::getAllPositions(generator);
    for (const auto& chunk : mineshafts) {
        // Seedra's Mineshaft placement API returns chunk coordinates. Include both the
        // chunk coordinate and its block-center equivalent so the output is unambiguous.
        csv << "mineshaft_chunk," << chunk.x << ',' << chunk.z << ",chunk\n";
        csv << "mineshaft_center," << ((chunk.x << 4) + 8) << ','
            << ((chunk.z << 4) + 8) << ",block_center\n";
    }

    info << "Biome PNG: "
         << getBiomeImageFileNameFromGenerator(&generator, "") << '\n';
    info << "Structure coordinates: structures.csv\n";

    std::cout << "Done. Output is in seed_output/." << std::endl;
    return 0;
}
