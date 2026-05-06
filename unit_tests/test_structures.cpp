#include "include/doctest.h"

// placement headers
#include "structures/placement/StaticStructures.hpp"
#include "structures/placement/DynamicStructures.hpp"
#include "structures/placement/stronghold.hpp"
#include "structures/placement/netherfortress.hpp"
#include "structures/placement/mineshaft.hpp"

// generator / biome support
#include "terrain/generator.hpp"
#include "terrain/biomes/biome.hpp"
#include "common/enums.hpp"
#include "lce/enums.hpp"

// ─── helpers ─────────────────────────────────────────────────────────────────

// Build a fully-initialised small-world Generator (CLASSIC, WIIU, AQUATIC).
static Generator makeGen(i64 seed,
                         lce::CONSOLE console    = lce::CONSOLE::WIIU,
                         LCEVERSION  version     = LCEVERSION::AQUATIC,
                         lce::WORLDSIZE  ws      = lce::WORLDSIZE::CLASSIC,
                         lce::BIOMESCALE bs      = lce::BIOMESCALE::SMALL)
{
    return Generator(console, version, seed, ws, bs);
}

// ─────────────────────────────────────────────────────────────────────────────
//  StaticStructure  –  getRegionChunkPosition / getRegionBlockPosition
// ─────────────────────────────────────────────────────────────────────────────
TEST_SUITE("StaticStructure_basics") {

    TEST_CASE("getRegionChunkPosition is deterministic for same inputs") {
        Placement::Feature::setWorldSize(lce::WORLDSIZE::CLASSIC);

        const Pos2D p1 = Placement::Feature::getRegionChunkPosition(42LL, 0, 0);
        const Pos2D p2 = Placement::Feature::getRegionChunkPosition(42LL, 0, 0);
        CHECK(p1 == p2);
    }

    TEST_CASE("getRegionChunkPosition differs for different world seeds") {
        Placement::Feature::setWorldSize(lce::WORLDSIZE::CLASSIC);

        const Pos2D a = Placement::Feature::getRegionChunkPosition(1LL, 0, 0);
        const Pos2D b = Placement::Feature::getRegionChunkPosition(2LL, 0, 0);
        // Different seeds should (with overwhelming probability) give different positions
        CHECK(!(a == b));
    }

    TEST_CASE("getRegionChunkPosition differs for different regions") {
        Placement::Feature::setWorldSize(lce::WORLDSIZE::CLASSIC);

        const Pos2D p00 = Placement::Feature::getRegionChunkPosition(42LL, 0, 0);
        const Pos2D p10 = Placement::Feature::getRegionChunkPosition(42LL, 1, 0);
        const Pos2D p01 = Placement::Feature::getRegionChunkPosition(42LL, 0, 1);
        CHECK(!(p00 == p10));
        CHECK(!(p00 == p01));
    }

    TEST_CASE("getRegionBlockPosition is chunk*16 + 8 offset") {
        Placement::Feature::setWorldSize(lce::WORLDSIZE::CLASSIC);
        const i64 seed = 99LL;
        const Pos2D chunk = Placement::Feature::getRegionChunkPosition(seed, 0, 0);
        const Pos2D block = Placement::Feature::getRegionBlockPosition(seed, 0, 0);
        // block = (chunk << 4) + 8
        CHECK(block.x == (chunk.x << 4) + 8);
        CHECK(block.z == (chunk.z << 4) + 8);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Village placement (StaticStructure<Village>)
// ─────────────────────────────────────────────────────────────────────────────
TEST_SUITE("Village_placement") {

    TEST_CASE("setWorldSize does not crash") {
        CHECK_NOTHROW(Placement::Village<false>::setWorldSize(lce::WORLDSIZE::CLASSIC));
        CHECK_NOTHROW(Placement::Village<false>::setWorldSize(lce::WORLDSIZE::MEDIUM));
        CHECK_NOTHROW(Placement::Village<false>::setWorldSize(lce::WORLDSIZE::LARGE));
    }

    TEST_CASE("getRegionChunkPosition is stable after setWorldSize") {
        Placement::Village<false>::setWorldSize(lce::WORLDSIZE::CLASSIC);
        const Pos2D a = Placement::Village<false>::getRegionChunkPosition(7LL, 0, 0);
        const Pos2D b = Placement::Village<false>::getRegionChunkPosition(7LL, 0, 0);
        CHECK(a == b);
    }

    TEST_CASE("getAllPositions returns vector (may be empty for no-biome seed)") {
        Placement::Village<false>::setWorldSize(lce::WORLDSIZE::CLASSIC);
        const Generator g = makeGen(0LL);
        CHECK_NOTHROW(Placement::Village<false>::getAllPositions(&g));
    }

    TEST_CASE("getAllPositionsBounded subset of getAllPositions") {
        Placement::Village<false>::setWorldSize(lce::WORLDSIZE::CLASSIC);
        const Generator g = makeGen(12345LL);

        constexpr int HALF = 512;
        const auto all     = Placement::Village<false>::getAllPositions(&g);
        const auto bounded = Placement::Village<false>::getAllPositionsBounded(&g, -HALF, -HALF, HALF, HALF);

        // Every bounded position must exist in the full list
        for (const Pos2D &bp : bounded) {
            bool found = false;
            for (const Pos2D &ap : all) { if (ap == bp) { found = true; break; } }
            CHECK(found);
        }
    }

    TEST_CASE("PS4 village salt is same as non-PS4 village salt") {
        // Both templates share the same SALT (10387312)
        CHECK(Placement::StaticStructure<Placement::Village<false>>::SALT ==
              Placement::StaticStructure<Placement::Village<true>>::SALT);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Feature placement (desert pyramid, jungle temple, swamp hut, igloo)
// ─────────────────────────────────────────────────────────────────────────────
TEST_SUITE("Feature_placement") {

    TEST_CASE("setWorldSize CLASSIC vs MEDIUM changes REGION_SIZE") {
        Placement::Feature::setWorldSize(lce::WORLDSIZE::CLASSIC);
        const int smallRegion = Placement::Feature::REGION_SIZE;

        Placement::Feature::setWorldSize(lce::WORLDSIZE::MEDIUM);
        const int largeRegion = Placement::Feature::REGION_SIZE;

        CHECK(smallRegion != largeRegion);
        // restore
        Placement::Feature::setWorldSize(lce::WORLDSIZE::CLASSIC);
    }

    TEST_CASE("getFeatureType returns DesertPyramid in desert biome") {
        const StructureType type = Placement::Feature::getFeatureType(biome_t::desert);
        CHECK(type == StructureType::DesertPyramid);
    }

    TEST_CASE("getFeatureType returns JunglePyramid in jungle biome") {
        const StructureType type = Placement::Feature::getFeatureType(biome_t::jungle);
        CHECK(type == StructureType::JunglePyramid);
    }

    TEST_CASE("getFeatureType returns SwampHut in swamp biome") {
        const StructureType type = Placement::Feature::getFeatureType(biome_t::swamp);
        CHECK(type == StructureType::SwampHut);
    }

    TEST_CASE("getFeatureType returns Igloo in snowy_tundra biome") {
        const StructureType type = Placement::Feature::getFeatureType(biome_t::snowy_tundra);
        CHECK(type == StructureType::Igloo);
    }

    TEST_CASE("getAllFeaturePositions returns vector without crash") {
        Placement::Feature::setWorldSize(lce::WORLDSIZE::CLASSIC);
        const Generator g = makeGen(42LL);
        CHECK_NOTHROW(Placement::Feature::getAllFeaturePositions(&g));
    }

    TEST_CASE("getAllFeaturePositionsSeparated size equals FEATURE_NUM") {
        Placement::Feature::setWorldSize(lce::WORLDSIZE::CLASSIC);
        const Generator g = makeGen(42LL);
        const auto separated = Placement::Feature::getAllFeaturePositionsSeparated(&g);
        CHECK(separated.size() == static_cast<size_t>(StructureType::FEATURE_NUM));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  OceanRuin (StaticStructure)
// ─────────────────────────────────────────────────────────────────────────────
TEST_SUITE("OceanRuin_placement") {

    TEST_CASE("setWorldSize does not crash") {
        CHECK_NOTHROW(Placement::OceanRuin::setWorldSize(lce::WORLDSIZE::CLASSIC));
        CHECK_NOTHROW(Placement::OceanRuin::setWorldSize(lce::WORLDSIZE::LARGE));
    }

    TEST_CASE("REGION_SIZE is 8 (smaller than feature region)") {
        Placement::OceanRuin::setWorldSize(lce::WORLDSIZE::CLASSIC);
        CHECK(Placement::StaticStructure<Placement::OceanRuin>::REGION_SIZE == 8);
    }

    TEST_CASE("CHUNK_RANGE is 6") {
        CHECK(Placement::StaticStructure<Placement::OceanRuin>::CHUNK_RANGE == 6);
    }

    TEST_CASE("getRegionChunkPosition is deterministic") {
        const Pos2D a = Placement::OceanRuin::getRegionChunkPosition(55LL, 0, 0);
        const Pos2D b = Placement::OceanRuin::getRegionChunkPosition(55LL, 0, 0);
        CHECK(a == b);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DynamicStructure – Monument, Mansion, BuriedTreasure, Shipwreck, Outpost
// ─────────────────────────────────────────────────────────────────────────────
TEST_SUITE("DynamicStructure_salts") {

    TEST_CASE("Monument SALT is 10387313") {
        CHECK(Placement::Monument::SALT == 10387313);
    }

    TEST_CASE("Mansion SALT is 10387319") {
        CHECK(Placement::Mansion::SALT == 10387319);
    }

    TEST_CASE("BuriedTreasure SALT is 16842397") {
        CHECK(Placement::BuriedTreasure::SALT == 16842397);
    }

    TEST_CASE("Shipwreck SALT is 14357617") {
        CHECK(Placement::Shipwreck::SALT == 14357617);
    }

    TEST_CASE("Outpost SALT is 165745296") {
        CHECK(Placement::Outpost::SALT == 165745296);
    }
}

TEST_SUITE("DynamicStructure_Monument") {

    TEST_CASE("setWorldSize CLASSIC then MEDIUM doesn't crash") {
        CHECK_NOTHROW(Placement::Monument::setWorldSize(lce::WORLDSIZE::CLASSIC));
        CHECK_NOTHROW(Placement::Monument::setWorldSize(lce::WORLDSIZE::MEDIUM));
    }

    TEST_CASE("getAllPositions returns vector without crash") {
        Placement::Monument::setWorldSize(lce::WORLDSIZE::CLASSIC);
        const Generator g = makeGen(1LL);
        CHECK_NOTHROW(Placement::Monument::getAllPositions(&g));
    }

    TEST_CASE("getAllPossibleChunks is deterministic") {
        const auto a = Placement::Monument::getAllPossibleChunks(42LL, 0, 0);
        const auto b = Placement::Monument::getAllPossibleChunks(42LL, 0, 0);
        REQUIRE(a.size() == b.size());
        for (size_t i = 0; i < a.size(); ++i) CHECK(a[i] == b[i]);
    }

    TEST_CASE("getAllPossibleChunks differs for different seeds") {
        const auto a = Placement::Monument::getAllPossibleChunks(1LL, 0, 0);
        const auto b = Placement::Monument::getAllPossibleChunks(2LL, 0, 0);
        // Not necessarily same set
        bool diff = (a.size() != b.size());
        if (!diff) {
            for (size_t i = 0; i < a.size(); ++i) {
                if (!(a[i] == b[i])) { diff = true; break; }
            }
        }
        CHECK(diff);
    }

    TEST_CASE("isPossibleChunkPos: every chunk from getAllPossibleChunks passes") {
        const auto chunks = Placement::Monument::getAllPossibleChunks(99LL, 0, 0);
        for (const Pos2D &pos : chunks) {
            CHECK(Placement::Monument::isPossibleChunkPos(99LL, 0, 0, pos));
        }
    }

    TEST_CASE("canSpawnAtChunk: chunk in getAllPossibleChunks returns true") {
        const auto chunks = Placement::Monument::getAllPossibleChunks(77LL, 0, 0);
        for (const Pos2D &pos : chunks) {
            CHECK(Placement::Monument::canSpawnAtChunk(77LL, pos.x, pos.z, 0, 0));
        }
    }
}

TEST_SUITE("DynamicStructure_BuriedTreasure") {

    TEST_CASE("setWorldSize does not crash") {
        CHECK_NOTHROW(Placement::BuriedTreasure::setWorldSize(lce::WORLDSIZE::CLASSIC));
    }

    TEST_CASE("getAllPositions z offset is applied (+1)") {
        Placement::BuriedTreasure::setWorldSize(lce::WORLDSIZE::CLASSIC);
        const Generator g = makeGen(5LL);
        // base positions (without override) via base class
        auto base = Placement::DynamicStructure<Placement::BuriedTreasure>::getAllPositions(&g);
        auto derived = Placement::BuriedTreasure::getAllPositions(&g);
        REQUIRE(base.size() == derived.size());
        for (size_t i = 0; i < base.size(); ++i) {
            CHECK(derived[i].z == base[i].z + 1);
            CHECK(derived[i].x == base[i].x);
        }
    }

    TEST_CASE("getAllPositionsBounded z offset is applied (+1)") {
        Placement::BuriedTreasure::setWorldSize(lce::WORLDSIZE::CLASSIC);
        const Generator g = makeGen(5LL);
        constexpr int B = 512;
        auto base    = Placement::DynamicStructure<Placement::BuriedTreasure>::getAllPositionsBounded(&g, -B,-B, B,B);
        auto derived = Placement::BuriedTreasure::getAllPositionsBounded(&g, -B,-B, B,B);
        REQUIRE(base.size() == derived.size());
        for (size_t i = 0; i < base.size(); ++i) {
            CHECK(derived[i].z == base[i].z + 1);
        }
    }

    TEST_CASE("MAIN_VALID_BIOMES includes beach biome") {
        constexpr u64 mask = Placement::BuriedTreasure::MAIN_VALID_BIOMES;
        constexpr u64 beachBit = makeBiomeBitmask<biome_t::beach>();
        CHECK((mask & beachBit) != 0u);
    }
}

TEST_SUITE("DynamicStructure_Mansion") {

    TEST_CASE("setWorldSize MEDIUM changes REGION_SIZE") {
        Placement::Mansion::setWorldSize(lce::WORLDSIZE::CLASSIC);
        const int small = Placement::Mansion::REGION_SIZE;
        Placement::Mansion::setWorldSize(lce::WORLDSIZE::MEDIUM);
        const int large = Placement::Mansion::REGION_SIZE;
        CHECK(small != large);
        Placement::Mansion::setWorldSize(lce::WORLDSIZE::CLASSIC);
    }

    TEST_CASE("MAIN_VALID_BIOMES includes dark_forest") {
        constexpr u64 mask    = Placement::Mansion::MAIN_VALID_BIOMES;
        constexpr u64 dfBit   = makeBiomeBitmask<biome_t::dark_forest>();
        CHECK((mask & dfBit) != 0u);
    }
}

TEST_SUITE("DynamicStructure_Shipwreck") {

    TEST_CASE("setWorldSize does not crash") {
        CHECK_NOTHROW(Placement::Shipwreck::setWorldSize(lce::WORLDSIZE::CLASSIC));
    }

    TEST_CASE("HAS_SECOND_BIOME_CHECK is false") {
        CHECK_FALSE(Placement::Shipwreck::HAS_SECOND_BIOME_CHECK);
    }

    TEST_CASE("MAIN_VALID_BIOMES includes ocean") {
        constexpr u64 mask   = Placement::Shipwreck::MAIN_VALID_BIOMES;
        constexpr u64 ocnBit = makeBiomeBitmask<biome_t::ocean>();
        CHECK((mask & ocnBit) != 0u);
    }

    TEST_CASE("MAIN_VALID_BIOMES includes beach") {
        constexpr u64 mask   = Placement::Shipwreck::MAIN_VALID_BIOMES;
        constexpr u64 bchBit = makeBiomeBitmask<biome_t::beach>();
        CHECK((mask & bchBit) != 0u);
    }
}

TEST_SUITE("DynamicStructure_Outpost") {

    TEST_CASE("setWorldSize does not crash") {
        CHECK_NOTHROW(Placement::Outpost::setWorldSize(lce::WORLDSIZE::CLASSIC));
    }

    TEST_CASE("MAIN_VALID_BIOMES includes plains, desert, taiga") {
        constexpr u64 mask    = Placement::Outpost::MAIN_VALID_BIOMES;
        constexpr u64 plains  = makeBiomeBitmask<biome_t::plains>();
        constexpr u64 desert  = makeBiomeBitmask<biome_t::desert>();
        constexpr u64 taiga   = makeBiomeBitmask<biome_t::taiga>();
        CHECK((mask & plains) != 0u);
        CHECK((mask & desert) != 0u);
        CHECK((mask & taiga)  != 0u);
    }

    TEST_CASE("Correct placement for given seed on classic world size") {
        const Generator g = makeGen(-3885444882906729567LL, lce::CONSOLE::PS4, LCEVERSION::AQUATIC, lce::WORLDSIZE::CLASSIC, lce::BIOMESCALE::LARGE);
        const auto outposts = Placement::Outpost::getAllPositions(&g);
        // Just verify it returns the same result across runs
        REQUIRE(outposts.size() == 3);
        CHECK(outposts[0] == Pos2D(-200, -184));
        CHECK(outposts[1] == Pos2D(-168, 296));
        CHECK(outposts[2] == Pos2D(376, 40));
    }

    TEST_CASE("Correct placement for given seed classic (2)") {
        const Generator g = makeGen(-1653154253098079, lce::CONSOLE::PS4, LCEVERSION::AQUATIC, lce::WORLDSIZE::CLASSIC, lce::BIOMESCALE::LARGE);
        const auto outposts = Placement::Outpost::getAllPositions(&g);
        // Just verify it returns the same result across runs
        REQUIRE(outposts.size() == 4);
        CHECK(outposts[0] == Pos2D(-200, -184));
        CHECK(outposts[1] == Pos2D(-120, 216));
        CHECK(outposts[2] == Pos2D(200, -120));
        CHECK(outposts[3] == Pos2D(312, 216));
    }

    TEST_CASE("Correct placement for given seed on large world size") {
        const Generator g = makeGen(1, lce::CONSOLE::PS4, LCEVERSION::AQUATIC, lce::WORLDSIZE::LARGE, lce::BIOMESCALE::LARGE);
        Placement::Outpost::setWorldSize(lce::WORLDSIZE::LARGE);
        const auto outposts = Placement::Outpost::getAllPositions(&g);
        // Just verify it returns the same result across runs
        REQUIRE(outposts.size() > 0);
        bool found = false;
        for (size_t i = 0; i < outposts.size(); ++i) {
            if (outposts[i] == Pos2D(280, -680)) {
                found = true;
                break;
            }
        }
        CHECK(found);
    }

    TEST_CASE("Correct placement for given seed large world size (2)") {
        const Generator g = makeGen(39, lce::CONSOLE::PS4, LCEVERSION::AQUATIC, lce::WORLDSIZE::LARGE, lce::BIOMESCALE::LARGE);
        Placement::Outpost::setWorldSize(lce::WORLDSIZE::LARGE);
        const auto outposts = Placement::Outpost::getAllPositions(&g);
        REQUIRE(outposts.size() > 0);
        bool found = false;
        for (size_t i = 0; i < outposts.size(); ++i) {
            if (outposts[i] == Pos2D(-1032, -488)) {
                found = true;
                break;
            }
        }
        CHECK(found);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  NetherFortress placement
// ─────────────────────────────────────────────────────────────────────────────
TEST_SUITE("NetherFortress_placement") {

    TEST_CASE("getWorldPosition is deterministic") {
        const Pos2DVec_t a = Placement::NetherFortress::getWorldPositions(12345LL);
        const Pos2DVec_t b = Placement::NetherFortress::getWorldPositions(12345LL);
        CHECK(a[0] == b[0]);
    }

    TEST_CASE("getWorldPosition differs for different seeds") {
        const Pos2DVec_t a = Placement::NetherFortress::getWorldPositions(1LL);
        const Pos2DVec_t b = Placement::NetherFortress::getWorldPositions(2LL);
        CHECK(!(a[0] == b[0]));
    }

    TEST_CASE("getWorldPosition x,z is in [0,6]") {
        for (i64 seed = 0; seed < 100; ++seed) {
            const Pos2DVec_t p = Placement::NetherFortress::getWorldPositions(seed);
            REQUIRE(p.size() == 1); // only one fortress in classic world size
            CHECK(p[0].x >= 0);
            CHECK(p[0].x <= 6);
            CHECK(p[0].z >= 0);
            CHECK(p[0].z <= 6);
        }
    }

    TEST_CASE("large world size can have multiple fortresses") {
        const Pos2DVec_t p = Placement::NetherFortress::getWorldPositions(1, lce::WORLDSIZE::LARGE);
        CHECK(p.size() > 1);
    }

    TEST_CASE("known seed produces known position") {
        // seed=1: rng.setSeed(0) -> next<32>() -> nextInt(49) -> deterministic
        const Pos2DVec_t p = Placement::NetherFortress::getWorldPositions(1LL);
        // just verify it's stable across runs by checking it equals itself
        CHECK(p[0] == Pos2D(4, 6));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Mineshaft placement
// ─────────────────────────────────────────────────────────────────────────────
TEST_SUITE("Mineshaft_placement") {

    TEST_CASE("getPositions is deterministic") {
        const auto a = Placement::Mineshaft::getPositions(42LL, -256, -256, 256, 256);
        const auto b = Placement::Mineshaft::getPositions(42LL, -256, -256, 256, 256);
        REQUIRE(a.size() == b.size());
        for (size_t i = 0; i < a.size(); ++i) CHECK(a[i] == b[i]);
    }

    TEST_CASE("getPositions differs for different seeds") {
        const auto a = Placement::Mineshaft::getPositions(1LL, -256, -256, 256, 256);
        const auto b = Placement::Mineshaft::getPositions(2LL, -256, -256, 256, 256);
        bool diff = (a.size() != b.size());
        if (!diff) {
            for (size_t i = 0; i < a.size(); ++i) {
                if (!(a[i] == b[i])) { diff = true; break; }
            }
        }
        CHECK(diff);
    }

    TEST_CASE("radius overload matches rectangular overload") {
        const auto rect   = Placement::Mineshaft::getPositions(77LL, -128, -128, 128, 128);
        const auto radial = Placement::Mineshaft::getPositions(77LL, 0, 0, 128);
        // They cover the same bounding box, so results must match
        REQUIRE(rect.size() == radial.size());
        for (size_t i = 0; i < rect.size(); ++i) CHECK(rect[i] == radial[i]);
    }

    TEST_CASE("all returned chunk positions are block-space (offset +8)") {
        const auto shafts = Placement::Mineshaft::getPositions(55LL, -512, -512, 512, 512);
        for (const Pos2D &p : shafts) {
            // Block positions are (chunk << 4) + 8, so (pos - 8) % 16 == 0
            CHECK(((p.x - 8) & 0xF) == 0);
            CHECK(((p.z - 8) & 0xF) == 0);
        }
    }

    TEST_CASE("getAllPositions uses generator bounds") {
        const Generator g = makeGen(5LL);
        const auto all = Placement::Mineshaft::getAllPositions(g);
        // Verify all positions are within world bounds
        const int b = g.getWorldCoordinateBounds();
        for (const Pos2D &p : all) {
            CHECK(p.x >= -b);
            CHECK(p.x <=  b);
            CHECK(p.z >= -b);
            CHECK(p.z <=  b);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Stronghold placement
// ─────────────────────────────────────────────────────────────────────────────
TEST_SUITE("Stronghold_placement") {

    TEST_CASE("setWorldSize SMALL sets useFarStronghold = false") {
        Placement::Stronghold::setWorldSize(lce::WORLDSIZE::SMALL);
        CHECK_FALSE(Placement::Stronghold::useFarStronghold);
    }

    TEST_CASE("setWorldSize MEDIUM sets useFarStronghold = true") {
        Placement::Stronghold::setWorldSize(lce::WORLDSIZE::MEDIUM);
        CHECK(Placement::Stronghold::useFarStronghold);
        Placement::Stronghold::setWorldSize(lce::WORLDSIZE::SMALL); // restore
    }

    TEST_CASE("setWorldSize LARGE sets useFarStronghold = true") {
        Placement::Stronghold::setWorldSize(lce::WORLDSIZE::LARGE);
        CHECK(Placement::Stronghold::useFarStronghold);
        Placement::Stronghold::setWorldSize(lce::WORLDSIZE::SMALL); // restore
    }

    TEST_CASE("getWorldPositions returns strongholdCount positions") {
        Placement::Stronghold::setWorldSize(lce::WORLDSIZE::CLASSIC);
        Generator g = makeGen(100LL);
        g.setStrongholdCount(3);
        const auto positions = Placement::Stronghold::getWorldPositions(g);
        CHECK(static_cast<int>(positions.size()) == g.getStrongholdCount());
    }

    TEST_CASE("getWorldPositions is deterministic for same seed") {
        Placement::Stronghold::setWorldSize(lce::WORLDSIZE::CLASSIC);
        const Generator g = makeGen(999LL);
        const auto a = Placement::Stronghold::getWorldPositions(g);
        const auto b = Placement::Stronghold::getWorldPositions(g);
        REQUIRE(a.size() == b.size());
        for (size_t i = 0; i < a.size(); ++i) CHECK(a[i] == b[i]);
    }

    TEST_CASE("getWorldPositions differs for different seeds") {
        Placement::Stronghold::setWorldSize(lce::WORLDSIZE::CLASSIC);
        const Generator g1 = makeGen(1LL);
        const Generator g2 = makeGen(2LL);
        const auto a = Placement::Stronghold::getWorldPositions(g1);
        const auto b = Placement::Stronghold::getWorldPositions(g2);
        REQUIRE(a.size() == b.size());
        bool diff = false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (!(a[i] == b[i])) { diff = true; break; }
        }
        CHECK(diff);
    }

    TEST_CASE("NON-XBOX correct first iteration position on given seed") {
        Placement::Stronghold::setWorldSize(lce::WORLDSIZE::CLASSIC);
        Generator g = makeGen(1LL);
        const auto positions = Placement::Stronghold::getWorldPositions(g);
        REQUIRE(positions.size() == 1u);
        CHECK(positions[0] == Pos2D{ 84, 4 });
    }

    TEST_CASE("XBOX correct first iteration position on given seed") {
        Placement::Stronghold::setWorldSize(lce::WORLDSIZE::CLASSIC);
        Generator g = makeGen(1LL, lce::CONSOLE::XBOX1, LCEVERSION::ELYTRA, lce::WORLDSIZE::CLASSIC, lce::BIOMESCALE::SMALL);
        const auto positions = Placement::Stronghold::getWorldPositions(g);
        REQUIRE(positions.size() == 1u);
        CHECK(positions[0] == Pos2D{ 20, -28 });
    }

    TEST_CASE("XB1 correct last iteration position on given seed") {
        Placement::Stronghold::setWorldSize(lce::WORLDSIZE::CLASSIC);
        Generator g = makeGen(10557LL, lce::CONSOLE::XBOX1, LCEVERSION::ELYTRA, lce::WORLDSIZE::CLASSIC, lce::BIOMESCALE::LARGE);
        const auto positions = Placement::Stronghold::getWorldPositions(g);
        REQUIRE(positions.size() == 1u);
        CHECK(positions[0] == Pos2D{ -44, 100 });
    }

    TEST_CASE("single stronghold position is in valid biome (CLASSIC world)") {
        Placement::Stronghold::setWorldSize(lce::WORLDSIZE::CLASSIC);
        Generator g = makeGen(42LL);
        g.setStrongholdCount(1);
        const auto positions = Placement::Stronghold::getWorldPositions(g);
        REQUIRE(positions.size() == 1u);
        // The stronghold must land in a stronghold-valid biome
        const biome_t biome = g.getBiomeIdAt(1, positions[0].x, positions[0].z);
        CHECK(Generator::id_matches(biome, Placement::Stronghold::stronghold_biomes));
    }

    TEST_CASE("multiple strongholds each land in a valid biome") {
        Placement::Stronghold::setWorldSize(lce::WORLDSIZE::CLASSIC);
        Generator g = makeGen(54321LL);
        g.setStrongholdCount(3);
        const auto positions = Placement::Stronghold::getWorldPositions(g);
        REQUIRE(positions.size() == 3u);
        for (const Pos2D &pos : positions) {
            const biome_t biome = g.getBiomeIdAt(1, pos.x, pos.z);
            CHECK(Generator::id_matches(biome, Placement::Stronghold::stronghold_biomes));
        }
    }

    TEST_CASE("XBOX360 stronghold count is 1") {
        Placement::Stronghold::setWorldSize(lce::WORLDSIZE::CLASSIC);
        Generator g(lce::CONSOLE::XBOX360, LCEVERSION::AQUATIC, 7LL,
                    lce::WORLDSIZE::CLASSIC, lce::BIOMESCALE::SMALL);
        const auto positions = Placement::Stronghold::getWorldPositions(g);
        CHECK(positions.size() == 1u);
    }

    TEST_CASE("stronghold_biomes bitmask includes plains") {
        constexpr u64 plainsBit = makeBiomeBitmask<biome_t::plains>();
        CHECK((Placement::Stronghold::stronghold_biomes & plainsBit) != 0u);
    }

    TEST_CASE("stronghold_biomes bitmask includes desert") {
        constexpr u64 desertBit = makeBiomeBitmask<biome_t::desert>();
        CHECK((Placement::Stronghold::stronghold_biomes & desertBit) != 0u);
    }

    TEST_CASE("stronghold_biomes bitmask does NOT include ocean") {
        constexpr u64 oceanBit = makeBiomeBitmask<biome_t::ocean>();
        CHECK((Placement::Stronghold::stronghold_biomes & oceanBit) == 0u);
    }

    TEST_CASE("verifyBlockPosition: stronghold block position passes biome check") {
        Placement::Stronghold::setWorldSize(lce::WORLDSIZE::CLASSIC);
        Generator g = makeGen(1LL);
        const auto positions = Placement::Stronghold::getWorldPositions(g);
        REQUIRE(!positions.empty());
        // The returned block position should be in a valid biome
        const biome_t b = g.getBiomeIdAt(4, positions[0].x, positions[0].z);
        CHECK(Generator::id_matches(b, Placement::Stronghold::stronghold_biomes));
    }

    TEST_CASE("far stronghold has larger distance from origin than small world") {
        Generator g_small = makeGen(999LL, lce::CONSOLE::PS4, LCEVERSION::AQUATIC,
                                    lce::WORLDSIZE::CLASSIC, lce::BIOMESCALE::SMALL);

        Generator g_large = makeGen(999LL, lce::CONSOLE::PS4, LCEVERSION::AQUATIC,
                                    lce::WORLDSIZE::LARGE, lce::BIOMESCALE::SMALL);

        Placement::Stronghold::setWorldSize(lce::WORLDSIZE::CLASSIC);
        const Pos2D small_pos = Placement::Stronghold::getWorldPositions(g_small)[0];

        Placement::Stronghold::setWorldSize(lce::WORLDSIZE::LARGE);
        const Pos2D large_pos = Placement::Stronghold::getWorldPositions(g_large)[0];

        const double small_dist = std::sqrt(static_cast<double>(small_pos.x) * small_pos.x +
                                            static_cast<double>(small_pos.z) * small_pos.z);
        const double large_dist = std::sqrt(static_cast<double>(large_pos.x) * large_pos.x +
                                            static_cast<double>(large_pos.z) * large_pos.z);
        CHECK(large_dist >= small_dist);

        // restore
        Placement::Stronghold::setWorldSize(lce::WORLDSIZE::CLASSIC);
    }
}

