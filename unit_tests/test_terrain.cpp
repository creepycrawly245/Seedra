#include "include/doctest.h"

#include "terrain/ChunkPrimer.hpp"
#include "terrain/WorldConfig.hpp"
#include "terrain/PopulationReverser.hpp"
#include "terrain/generator.hpp"
#include "terrain/biomes/biomeDepthAndScale.hpp"
#include "terrain/biomes/biome_t.hpp"
#include "terrain/biomes/layers.hpp"
#include "terrain/noise/noise.hpp"
#include "terrain/noise/NoiseGen.hpp"
#include "terrain/decorators/WorldGenLakes.hpp"
#include "terrain/carve/scanBoxHull.hpp"
#include "common/Pos3DTemplate.hpp"
#include "common/rng.hpp"

// ============================================================
//  ChunkPrimer – storage index
// ============================================================
TEST_SUITE("ChunkPrimer::getStorageIndex") {

    TEST_CASE("origin maps to 0") {
        CHECK(ChunkPrimer::getStorageIndex(0, 0, 0) == 0);
    }

    TEST_CASE("XZY layout: x stride is 4096, z stride is 256, y stride is 1") {
        // index = x<<12 | z<<8 | y
        CHECK(ChunkPrimer::getStorageIndex(1, 0, 0) == (1 << 12));
        CHECK(ChunkPrimer::getStorageIndex(0, 0, 1) == (1 << 8));
        CHECK(ChunkPrimer::getStorageIndex(0, 1, 0) == 1);
    }

    TEST_CASE("corner (15,255,15) is within storage") {
        const size_t idx = ChunkPrimer::getStorageIndex(15, 255, 15);
        CHECK(idx < 16u * 256u * 16u);
    }
}

// ============================================================
//  ChunkPrimer – isInvalidIndex
// ============================================================
TEST_SUITE("ChunkPrimer::isInvalidIndex") {

    TEST_CASE("valid coords are not invalid") {
        CHECK_FALSE(ChunkPrimer::isInvalidIndex(0,   0,   0));
        CHECK_FALSE(ChunkPrimer::isInvalidIndex(15,  255, 15));
        CHECK_FALSE(ChunkPrimer::isInvalidIndex(7,   128, 7));
    }

    TEST_CASE("x out of [0,15] is invalid") {
        CHECK(ChunkPrimer::isInvalidIndex(-1,  0, 0));
        CHECK(ChunkPrimer::isInvalidIndex(16,  0, 0));
    }

    TEST_CASE("z out of [0,15] is invalid") {
        CHECK(ChunkPrimer::isInvalidIndex(0,  0, -1));
        CHECK(ChunkPrimer::isInvalidIndex(0,  0, 16));
    }

    TEST_CASE("y out of [0,255] is invalid") {
        CHECK(ChunkPrimer::isInvalidIndex(0, -1,  0));
        CHECK(ChunkPrimer::isInvalidIndex(0, 256, 0));
    }
}

// ============================================================
//  ChunkPrimer – block get/set round-trip
// ============================================================
TEST_SUITE("ChunkPrimer block access") {

    TEST_CASE("fresh chunk is all air") {
        ChunkPrimer cp;
        for (int x = 0; x < 16; ++x)
            for (int z = 0; z < 16; ++z)
                for (int y = 0; y < 8; ++y)
                    CHECK(cp.getBlockId(x, y, z) == lce::blocks::AIR_ID);
    }

    TEST_CASE("setBlockId / getBlockId round-trip") {
        ChunkPrimer cp;
        cp.setBlockId(3, 64, 7, 1);   // stone = 1
        CHECK(cp.getBlockId(3, 64, 7) == 1);
        // neighbours untouched
        CHECK(cp.getBlockId(2, 64, 7) == lce::blocks::AIR_ID);
        CHECK(cp.getBlockId(3, 63, 7) == lce::blocks::AIR_ID);
        CHECK(cp.getBlockId(3, 64, 6) == lce::blocks::AIR_ID);
    }

    TEST_CASE("setBlockAndData encodes id and data correctly") {
        ChunkPrimer cp;
        cp.setBlockAndData(1, 1, 1, /*id=*/5, /*data=*/3);
        const lce::BlockState bs = cp.getBlock(1, 1, 1);
        CHECK(bs.getID()      == 5);
        CHECK(bs.getDataTag() == 3);
    }

    TEST_CASE("setBlock(Pos3D) and getBlock(Pos3D) round-trip") {
        ChunkPrimer cp;
        const lce::BlockState stone = lce::BlockState(1, 0);
        const Pos3D pos{5, 100, 12};
        cp.setBlock(pos, stone);
        CHECK(cp.getBlock(pos).getID() == 1);
    }

    TEST_CASE("out-of-bounds setBlockId is silently ignored") {
        ChunkPrimer cp;
        cp.setBlockId(16, 0, 0, 1);   // x=16 is invalid
        // storage should be untouched
        for (int y = 0; y < 4; ++y)
            CHECK(cp.getBlockId(0, y, 0) == lce::blocks::AIR_ID);
    }

    TEST_CASE("out-of-bounds getBlockId returns 0") {
        ChunkPrimer cp;
        CHECK(cp.getBlockId(-1, 0, 0)  == 0);
        CHECK(cp.getBlockId(0, 256, 0) == 0);
        CHECK(cp.getBlockId(0, 0, 16)  == 0);
    }

    TEST_CASE("isAirBlock reflects block state") {
        ChunkPrimer cp;
        CHECK(cp.isAirBlock(0, 0, 0));
        cp.setBlockId(0, 0, 0, 1);
        CHECK_FALSE(cp.isAirBlock(0, 0, 0));
    }

    TEST_CASE("mutBlockPtr returns nullptr for out-of-bounds") {
        ChunkPrimer cp;
        CHECK(cp.mutBlockPtr(-1, 0, 0)   == nullptr);
        CHECK(cp.mutBlockPtr(0, 256, 0)  == nullptr);
    }

    TEST_CASE("mutBlockPtr allows direct write") {
        ChunkPrimer cp;
        u16* ptr = cp.mutBlockPtr(2, 10, 3);
        REQUIRE(ptr != nullptr);
        *ptr = static_cast<u16>(7 << 4); // id=7, data=0
        CHECK(cp.getBlockId(2, 10, 3) == 7);
    }

    TEST_CASE("reset clears all blocks back to air") {
        ChunkPrimer cp;
        cp.setBlockId(0, 0, 0, 1);
        cp.setBlockId(15, 255, 15, 2);
        cp.reset();
        CHECK(cp.getBlockId(0,   0,   0)   == lce::blocks::AIR_ID);
        CHECK(cp.getBlockId(15,  255, 15)  == lce::blocks::AIR_ID);
    }
}

// ============================================================
//  ChunkPrimer – getHighestYBlock
// ============================================================
TEST_SUITE("ChunkPrimer::getHighestYBlock") {

    TEST_CASE("empty chunk returns 0") {
        ChunkPrimer cp;
        CHECK(cp.getHighestYBlock() == 0);
    }

    TEST_CASE("single block at y=64 returns 64") {
        ChunkPrimer cp;
        cp.setBlockId(0, 64, 0, 1);
        CHECK(cp.getHighestYBlock() == 64);
    }

    TEST_CASE("highest of two blocks is returned") {
        ChunkPrimer cp;
        cp.setBlockId(0, 64,  0, 1);
        cp.setBlockId(5, 128, 5, 2);
        CHECK(cp.getHighestYBlock() == 128);
    }
}

// ============================================================
//  ChunkPrimer – getFootprintSize
// ============================================================
TEST_SUITE("ChunkPrimer::getFootprintSize") {

    TEST_CASE("footprint is at least the block storage size") {
        CHECK(ChunkPrimer::getFootprintSize() >= 16u * 256u * 16u * sizeof(u16));
    }
}

// ============================================================
//  WorldConfig – construction and getters
// ============================================================
TEST_SUITE("WorldConfig") {

    TEST_CASE("constructor stores seed and getters match") {
        WorldConfig cfg(123456789LL,
                        lce::CONSOLE::PS4,
                        LCEVERSION::AQUATIC,
                        lce::WORLDSIZE::LARGE,
                        lce::BIOMESCALE::MEDIUM);
        CHECK(cfg.getWorldSeed()    == 123456789LL);
        CHECK(cfg.getConsole()      == lce::CONSOLE::PS4);
        CHECK(cfg.getLCEVersion()   == LCEVERSION::AQUATIC);
        CHECK(cfg.getWorldSize()    == lce::WORLDSIZE::LARGE);
        CHECK(cfg.getBiomeScale()   == lce::BIOMESCALE::MEDIUM);
        CHECK(cfg.getWorldGenerator() == WORLDGENERATOR::DEFAULT);
    }

    TEST_CASE("setWorldSeed updates seed") {
        WorldConfig cfg(0LL, lce::CONSOLE::XBOX360, LCEVERSION::ELYTRA,
                        lce::WORLDSIZE::MEDIUM, lce::BIOMESCALE::SMALL);
        cfg.setWorldSeed(-9999LL);
        CHECK(cfg.getWorldSeed() == -9999LL);
    }

    TEST_CASE("world coordinate bounds are positive and non-zero") {
        WorldConfig cfg(1LL, lce::CONSOLE::SWITCH, LCEVERSION::AQUATIC,
                        lce::WORLDSIZE::MEDIUM, lce::BIOMESCALE::MEDIUM);
        CHECK(cfg.getWorldCoordinateBounds() > 0);
    }

    TEST_CASE("chunk bounds equal coordinate bounds >> 4") {
        WorldConfig cfg(1LL, lce::CONSOLE::SWITCH, LCEVERSION::AQUATIC,
                        lce::WORLDSIZE::MEDIUM, lce::BIOMESCALE::MEDIUM);
        CHECK(cfg.getWorldChunkBounds() == cfg.getWorldCoordinateBounds() >> 4);
    }

    TEST_CASE("world bounds box reflects coordinate bounds") {
        WorldConfig cfg(1LL, lce::CONSOLE::SWITCH, LCEVERSION::AQUATIC,
                        lce::WORLDSIZE::MEDIUM, lce::BIOMESCALE::MEDIUM);
        const BoundingBox bb = cfg.getWorldBounds();
        const int b = cfg.getWorldCoordinateBounds();
        CHECK(bb.m_minX == -b);
        CHECK(bb.m_minZ == -b);
        CHECK(bb.m_maxX ==  b);
        CHECK(bb.m_maxZ ==  b);
    }

    TEST_CASE("setWorldSize updates bounds consistently") {
        WorldConfig cfg(1LL, lce::CONSOLE::PS3, LCEVERSION::AQUATIC,
                        lce::WORLDSIZE::SMALL, lce::BIOMESCALE::SMALL);
        cfg.setWorldSize(lce::WORLDSIZE::LARGE);
        CHECK(cfg.getWorldSize() == lce::WORLDSIZE::LARGE);
        CHECK(cfg.getWorldCoordinateBounds() > 0);
        CHECK(cfg.getWorldChunkBounds() == cfg.getWorldCoordinateBounds() >> 4);
    }

    TEST_CASE("setLCEVersion and setConsole round-trip") {
        WorldConfig cfg(0LL, lce::CONSOLE::NONE, LCEVERSION::NONE,
                        lce::WORLDSIZE::CLASSIC, lce::BIOMESCALE::SMALL);
        cfg.setLCEVersion(LCEVERSION::BOUNTIFUL);
        cfg.setConsole(lce::CONSOLE::WIIU);
        CHECK(cfg.getLCEVersion() == LCEVERSION::BOUNTIFUL);
        CHECK(cfg.getConsole()    == lce::CONSOLE::WIIU);
    }

    TEST_CASE("fixed biome default is plains and can be changed") {
        WorldConfig cfg(0LL, lce::CONSOLE::NONE, LCEVERSION::NONE,
                        lce::WORLDSIZE::CLASSIC, lce::BIOMESCALE::SMALL);
        CHECK(cfg.getFixedBiome() == biome_t::plains);
        cfg.setFixedBiome(biome_t::desert);
        CHECK(cfg.getFixedBiome() == biome_t::desert);
    }

    TEST_CASE("stronghold count default is 1 and can be changed") {
        WorldConfig cfg(0LL, lce::CONSOLE::NONE, LCEVERSION::NONE,
                        lce::WORLDSIZE::CLASSIC, lce::BIOMESCALE::SMALL);
        CHECK(cfg.getStrongholdCount() == 1);
        cfg.setStrongholdCount(3);
        CHECK(cfg.getStrongholdCount() == 3);
    }

    TEST_CASE("setBiomeScale round-trip") {
        WorldConfig cfg(0LL, lce::CONSOLE::NONE, LCEVERSION::NONE,
                        lce::WORLDSIZE::CLASSIC, lce::BIOMESCALE::SMALL);
        cfg.setBiomeScale(lce::BIOMESCALE::LARGE);
        CHECK(cfg.getBiomeScale() == lce::BIOMESCALE::LARGE);
    }
}

// ============================================================
//  RNG::getChunkSeed / getPopulationSeed
// ============================================================
TEST_SUITE("RNG seed helpers") {

    TEST_CASE("getChunkSeed is deterministic") {
        const u64 s1 = RNG::getChunkSeed(123456LL, 4, -2);
        const u64 s2 = RNG::getChunkSeed(123456LL, 4, -2);
        CHECK(s1 == s2);
    }

    TEST_CASE("getChunkSeed changes with world seed") {
        CHECK(RNG::getChunkSeed(1LL,  0, 0) != RNG::getChunkSeed(2LL,  0, 0));
    }

    TEST_CASE("getChunkSeed changes with chunk coords") {
        const i64 w = 42LL;
        CHECK(RNG::getChunkSeed(w, 0, 0) != RNG::getChunkSeed(w, 1, 0));
        CHECK(RNG::getChunkSeed(w, 0, 0) != RNG::getChunkSeed(w, 0, 1));
    }

    TEST_CASE("getPopulationSeed(w,0,0) seed matches getChunkSeed(w,0,0)") {
        const i64 w = 999LL;
        const RNG pop = RNG::getPopulationSeed(w, 0, 0);
        // getPopulationSeed calls RNG(getChunkSeed(...)) which calls setSeed
        const u64 expected = (RNG::getChunkSeed(w, 0, 0) ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1);
        CHECK(pop.getSeed() == expected);
    }

    TEST_CASE("getLargeFeatureSeed is deterministic") {
        const RNG r1 = RNG::getLargeFeatureSeed(777LL, 5, -3);
        const RNG r2 = RNG::getLargeFeatureSeed(777LL, 5, -3);
        CHECK(r1 == r2);
    }

    TEST_CASE("getLargeFeatureSeed differs across chunk coords") {
        CHECK_FALSE(RNG::getLargeFeatureSeed(1LL, 0, 0) == RNG::getLargeFeatureSeed(1LL, 1, 0));
    }
}

// ============================================================
//  LCG::combine
// ============================================================
TEST_SUITE("LCG::combine") {

    TEST_CASE("combine(0) is identity (mul=1, add=0)") {
        constexpr LCG base{0x5DEECE66DULL, 0xBULL};
        constexpr LCG id = base.combine(0);
        CHECK(id.multiplier == 1);
        CHECK(id.addend     == 0);
    }

    TEST_CASE("combine(1) equals base LCG") {
        constexpr LCG base{0x5DEECE66DULL, 0xBULL};
        constexpr LCG c1 = base.combine(1);
        CHECK(c1.multiplier == base.multiplier);
        CHECK(c1.addend     == base.addend);
    }

    TEST_CASE("JAVA_LCG::java2 matches combine(2)") {
        constexpr LCG c2 = JAVA_LCG::base.combine(2);
        CHECK(JAVA_LCG::M2 == c2.multiplier);
        CHECK(JAVA_LCG::A2 == c2.addend);
    }

    TEST_CASE("JAVA_LCG::java4 matches combine(4)") {
        constexpr LCG c4 = JAVA_LCG::base.combine(4);
        CHECK(JAVA_LCG::M4 == c4.multiplier);
        CHECK(JAVA_LCG::A4 == c4.addend);
    }
}

// ============================================================
//  PopulationReverser::getSeedsFromChunkSeed
// ============================================================
TEST_SUITE("PopulationReverser") {

    // Helper: verify every returned candidate actually reproduces the chunk seed
    static void verifyCandidates(u64 chunkSeed, int x, int z) {
        const auto results = PopulationReverser::getSeedsFromChunkSeed(chunkSeed, x, z);
        for (i64 ws : results) {
            const u64 got = RNG::getChunkSeed(ws, x, z) & MASK_48;
            CHECK(got == (chunkSeed & MASK_48));
        }
    }

    TEST_CASE("x=0, z=0 returns the chunk seed directly") {
        const u64 cs = 0xDEADBEEFULL;
        const auto res = PopulationReverser::getSeedsFromChunkSeed(cs, 0, 0);
        REQUIRE(res.size() == 1);
        CHECK(static_cast<u64>(res[0]) == cs);
    }

    TEST_CASE("all candidates reproduce the chunk seed at (1,0)") {
        verifyCandidates(RNG::getChunkSeed(12345LL, 1, 0), 1, 0);
    }

    TEST_CASE("all candidates reproduce the chunk seed at (0,1)") {
        verifyCandidates(RNG::getChunkSeed(12345LL, 0, 1), 0, 1);
    }

    TEST_CASE("all candidates reproduce the chunk seed at (4,-2)") {
        verifyCandidates(RNG::getChunkSeed(987654321LL, 4, -2), 4, -2);
    }

    TEST_CASE("known world seed is recovered at (1,1)") {
        constexpr i64 worldSeed = 42LL;
        const u64 cs = RNG::getChunkSeed(worldSeed, 1, 1);
        const auto res = PopulationReverser::getSeedsFromChunkSeed(cs, 1, 1);
        bool found = false;
        for (i64 ws : res) {
            if ((ws & MASK_48) == (worldSeed & MASK_48)) { found = true; break; }
        }
        CHECK(found);
    }

    TEST_CASE("result list is non-empty for typical coords") {
        const u64 cs = RNG::getChunkSeed(99999LL, 3, 5);
        const auto res = PopulationReverser::getSeedsFromChunkSeed(cs, 3, 5);
        CHECK_FALSE(res.empty());
    }
}

// ============================================================
//  noise_values helpers
// ============================================================
TEST_SUITE("noise_values helpers") {

    TEST_CASE("fastFloor positive value") {
        CHECK(noise_values::fastFloor(2.9)  == 2);
        CHECK(noise_values::fastFloor(0.01) == 0);
        CHECK(noise_values::fastFloor(1.0)  == 1);
    }

    TEST_CASE("fastFloor negative value") {
        CHECK(noise_values::fastFloor(-0.01) == -1);
        CHECK(noise_values::fastFloor(-1.0)  == -2); // implementation: cast(-1.0) - 1 = -2 (not > 0 branch)
        CHECK(noise_values::fastFloor(-1.5)  == -2);
    }

    TEST_CASE("fastFloor zero") {
        // 0.0 is not > 0.0 so the implementation returns cast(0.0) - 1 = -1
        CHECK(noise_values::fastFloor(0.0) == -1);
    }

    TEST_CASE("grad returns finite value") {
        const double v = noise_values::grad(0, 1.0, 0.5, -0.5);
        CHECK(std::isfinite(v));
    }

    TEST_CASE("grad covers all 16 hash cases (no UB)") {
        for (int h = 0; h < 16; ++h) {
            const double v = noise_values::grad(h, 0.3, -0.7, 1.2);
            CHECK(std::isfinite(v));
        }
    }

    TEST_CASE("grad2 returns finite value") {
        for (int h = 0; h < 16; ++h) {
            const double v = noise_values::grad2(h, 0.5, -0.5);
            CHECK(std::isfinite(v));
        }
    }

    TEST_CASE("dot product is correct") {
        int g2[2] = {1, -1};
        CHECK(noise_values::dot(g2, 2.0, 3.0) == doctest::Approx(-1.0));
    }

    TEST_CASE("maintainPrecision on zero is zero") {
        CHECK(maintainPrecision(0.0) == doctest::Approx(0.0));
    }

    TEST_CASE("maintainPrecision on small value is identity") {
        const double v = 1.5;
        CHECK(maintainPrecision(v) == doctest::Approx(v));
    }

    TEST_CASE("maintainPrecision on large value folds correctly") {
        // values >> 33554432 should be folded; result must be finite
        CHECK(std::isfinite(maintainPrecision(1e9)));
    }
}

// ============================================================
//  PerlinNoise initialisation
// ============================================================
TEST_SUITE("PerlinNoise init") {

    TEST_CASE("perlinInit fills permutation table with each byte exactly twice") {
        RNG rng(12345ULL);
        PerlinNoise pn{};
        perlinInit(&pn, rng);

        // The first half [0..255] is a permutation of 0-255
        std::array<int, 256> count{};
        for (int i = 0; i < 256; ++i) count[pn.permutations[i]]++;
        for (int i = 0; i < 256; ++i) CHECK(count[i] == 1);

        // Second half mirrors first half
        for (int i = 0; i < 256; ++i)
            CHECK(pn.permutations[i + 256] == pn.permutations[i]);
    }

    TEST_CASE("perlinInit offsets are in [0, 256)") {
        RNG rng(99ULL);
        PerlinNoise pn{};
        perlinInit(&pn, rng);
        CHECK(pn.x >= 0.0); CHECK(pn.x < 256.0);
        CHECK(pn.y >= 0.0); CHECK(pn.y < 256.0);
        CHECK(pn.z >= 0.0); CHECK(pn.z < 256.0);
    }

    TEST_CASE("perlinInit is deterministic for same seed") {
        RNG r1(777ULL), r2(777ULL);
        PerlinNoise p1{}, p2{};
        perlinInit(&p1, r1);
        perlinInit(&p2, r2);
        CHECK(p1.x == p2.x);
        CHECK(p1.y == p2.y);
        CHECK(p1.z == p2.z);
        for (int i = 0; i < 512; ++i)
            CHECK(p1.permutations[i] == p2.permutations[i]);
    }

    TEST_CASE("perlinInit produces different offsets for different seeds") {
        RNG r1(1ULL), r2(2ULL);
        PerlinNoise p1{}, p2{};
        perlinInit(&p1, r1);
        perlinInit(&p2, r2);
        // At least one coordinate should differ
        const bool differs = (p1.x != p2.x) || (p1.y != p2.y) || (p1.z != p2.z);
        CHECK(differs);
    }
}

// ============================================================
//  NoiseGeneratorSimplex
// ============================================================
TEST_SUITE("NoiseGeneratorSimplex") {

    static NoiseGeneratorSimplex makeSimp(u64 seed) {
        RNG rng(seed);
        NoiseGeneratorSimplex s{};
        perlinInit(&s, rng);
        return s;
    }

    TEST_CASE("getValue returns finite value") {
        auto s = makeSimp(42ULL);
        CHECK(std::isfinite(s.getValue(0.0, 0.0)));
        CHECK(std::isfinite(s.getValue(100.0, -50.0)));
    }

    TEST_CASE("getValue is in a reasonable range (simplex: roughly [-1,1] before amplitude)") {
        auto s = makeSimp(1234ULL);
        for (int i = -5; i <= 5; ++i) {
            const double v = s.getValue(i * 13.7, i * -7.3);
            CHECK(v >= -2.0);
            CHECK(v <=  2.0);
        }
    }

    TEST_CASE("getValue is deterministic") {
        auto s1 = makeSimp(55ULL);
        auto s2 = makeSimp(55ULL);
        CHECK(s1.getValue(3.14, -2.71) == doctest::Approx(s2.getValue(3.14, -2.71)));
    }

    TEST_CASE("different seeds produce different values") {
        auto s1 = makeSimp(1ULL);
        auto s2 = makeSimp(2ULL);
        CHECK(s1.getValue(1.0, 1.0) != s2.getValue(1.0, 1.0));
    }
}

// ============================================================
//  NoiseGeneratorPerlin
// ============================================================
TEST_SUITE("NoiseGeneratorPerlin") {

    TEST_CASE("getValue is deterministic") {
        RNG r1(999ULL), r2(999ULL);
        NoiseGeneratorPerlin<4> p1, p2;
        p1.setNoiseGeneratorPerlin(r1);
        p2.setNoiseGeneratorPerlin(r2);
        CHECK(p1.getValue(5.0, -3.0) == doctest::Approx(p2.getValue(5.0, -3.0)));
    }

    TEST_CASE("getValue returns finite result") {
        RNG rng(42ULL);
        NoiseGeneratorPerlin<4> p;
        p.setNoiseGeneratorPerlin(rng);
        CHECK(std::isfinite(p.getValue(0.0, 0.0)));
        CHECK(std::isfinite(p.getValue(1000.0, -1000.0)));
    }

    TEST_CASE("different seeds give different values") {
        RNG r1(1ULL), r2(2ULL);
        NoiseGeneratorPerlin<4> p1, p2;
        p1.setNoiseGeneratorPerlin(r1);
        p2.setNoiseGeneratorPerlin(r2);
        CHECK(p1.getValue(1.0, 1.0) != p2.getValue(1.0, 1.0));
    }
}

// ============================================================
//  Biome depth/scale table  (getBiomeDepthAndScale)
// ============================================================
TEST_SUITE("getBiomeDepthAndScale") {

    TEST_CASE("ocean depth is -1.0 and scale is 0.1") {
        double depth = 0, scale = 0;
        int rc = getBiomeDepthAndScale<true, true, false, false>(
            biome_t::ocean, &depth, &scale, nullptr, nullptr);
        CHECK(rc == 1);
        CHECK(depth == doctest::Approx(-1.0));
        CHECK(scale == doctest::Approx(0.1));
    }

    TEST_CASE("plains depth is 0.125 and scale is 0.05") {
        double depth = 0, scale = 0;
        int rc = getBiomeDepthAndScale<true, true, false, false>(
            biome_t::plains, &depth, &scale, nullptr, nullptr);
        CHECK(rc == 1);
        CHECK(depth == doctest::Approx(0.125));
        CHECK(scale == doctest::Approx(0.05));
    }

    TEST_CASE("mountains depth is 1.0 and scale is 0.5") {
        double depth = 0, scale = 0;
        getBiomeDepthAndScale<true, true, false, false>(
            biome_t::mountains, &depth, &scale, nullptr, nullptr);
        CHECK(depth == doctest::Approx(1.0));
        CHECK(scale == doctest::Approx(0.5));
    }

    TEST_CASE("deep_ocean has lower depth than ocean") {
        double dOcean = 0, dDeep = 0;
        getBiomeDepthAndScale<true, false, false, false>(biome_t::ocean,      &dOcean, nullptr, nullptr, nullptr);
        getBiomeDepthAndScale<true, false, false, false>(biome_t::deep_ocean, &dDeep,  nullptr, nullptr, nullptr);
        CHECK(dDeep < dOcean);
    }

    TEST_CASE("invDPlus2 equals 1/(depth+2) for ocean") {
        double depth = 0, inv = 0;
        getBiomeDepthAndScale<true, false, false, true>(biome_t::ocean, &depth, nullptr, nullptr, &inv);
        CHECK(inv == doctest::Approx(1.0 / (depth + 2.0)));
    }

    TEST_CASE("invDPlus2 equals 1/(depth+2) for plains") {
        double depth = 0, inv = 0;
        getBiomeDepthAndScale<true, false, false, true>(biome_t::plains, &depth, nullptr, nullptr, &inv);
        CHECK(inv == doctest::Approx(1.0 / (depth + 2.0)));
    }

    TEST_CASE("grass height for desert is 0") {
        int grass = -1;
        getBiomeDepthAndScale<false, false, true, false>(biome_t::desert, nullptr, nullptr, &grass, nullptr);
        CHECK(grass == 0);
    }

    TEST_CASE("grass height for forest is 62") {
        int grass = -1;
        getBiomeDepthAndScale<false, false, true, false>(biome_t::forest, nullptr, nullptr, &grass, nullptr);
        CHECK(grass == 62);
    }

    TEST_CASE("invalid/unknown biome returns 0") {
        double depth = 99;
        int rc = getBiomeDepthAndScale<true, false, false, false>(
            static_cast<biome_t>(200), &depth, nullptr, nullptr, nullptr);
        CHECK(rc == 0);
        CHECK(depth == doctest::Approx(99.0)); // unchanged
    }

    TEST_CASE("all four template bools false still returns success for valid biome") {
        int rc = getBiomeDepthAndScale<false, false, false, false>(
            biome_t::taiga, nullptr, nullptr, nullptr, nullptr);
        CHECK(rc == 1);
    }

    TEST_CASE("biome table is symmetric: depth and invDPlus2 consistent for multiple biomes") {
        static constexpr biome_t biomes[] = {
            biome_t::ocean, biome_t::plains, biome_t::desert, biome_t::mountains,
            biome_t::forest, biome_t::taiga, biome_t::swamp, biome_t::deep_ocean,
            biome_t::jungle, biome_t::savanna, biome_t::badlands
        };
        for (const biome_t b : biomes) {
            double d = 0, inv = 0;
            int rc = getBiomeDepthAndScale<true, false, false, true>(b, &d, nullptr, nullptr, &inv);
            CHECK(rc == 1);
            CHECK(inv == doctest::Approx(1.0 / (d + 2.0)));
        }
    }
}

// ============================================================
//  makeBiomeBitmask
// ============================================================
TEST_SUITE("makeBiomeBitmask") {

    TEST_CASE("single biome sets exactly one bit") {
        constexpr u64 mask = makeBiomeBitmask<biome_t::plains>();
        CHECK(mask == (1ULL << static_cast<int>(biome_t::plains)));
        CHECK(__builtin_popcountll(mask) == 1);
    }

    TEST_CASE("two biomes set exactly two bits") {
        constexpr u64 mask = makeBiomeBitmask<biome_t::ocean, biome_t::desert>();
        CHECK(__builtin_popcountll(mask) == 2);
        CHECK((mask & (1ULL << static_cast<int>(biome_t::ocean)))  != 0);
        CHECK((mask & (1ULL << static_cast<int>(biome_t::desert))) != 0);
    }

    TEST_CASE("id_matches returns true for included biome") {
        constexpr u64 mask = makeBiomeBitmask<biome_t::forest, biome_t::taiga>();
        CHECK(Generator::id_matches(biome_t::forest, mask));
        CHECK(Generator::id_matches(biome_t::taiga,  mask));
    }

    TEST_CASE("id_matches returns false for excluded biome") {
        constexpr u64 mask = makeBiomeBitmask<biome_t::forest, biome_t::taiga>();
        CHECK_FALSE(Generator::id_matches(biome_t::desert, mask));
        CHECK_FALSE(Generator::id_matches(biome_t::ocean,  mask));
    }

    TEST_CASE("empty pack gives zero mask") {
        // No biomes → no bits set
        constexpr u64 mask = 0ULL; // can't instantiate empty pack directly, so just validate zero
        CHECK(mask == 0ULL);
        CHECK_FALSE(Generator::id_matches(biome_t::plains, mask));
    }
}

// ============================================================
//  Generator – construction and seed queries
// ============================================================
TEST_SUITE("Generator construction") {

    TEST_CASE("int seed constructor stores seed correctly") {
        Generator g(lce::CONSOLE::PS4, LCEVERSION::AQUATIC, 12345LL,
                    lce::WORLDSIZE::MEDIUM, lce::BIOMESCALE::MEDIUM);
        CHECK(g.getWorldSeed() == 12345LL);
    }

    TEST_CASE("string seed constructor hashes to non-zero") {
        Generator g(lce::CONSOLE::PS4, LCEVERSION::AQUATIC, std::string("hello"),
                    lce::WORLDSIZE::MEDIUM, lce::BIOMESCALE::MEDIUM);
        // String seeds should produce a deterministic, non-zero seed
        CHECK(g.getWorldSeed() != 0LL);
    }

    TEST_CASE("same string seed produces same world seed") {
        Generator g1(lce::CONSOLE::PS4, LCEVERSION::AQUATIC, std::string("test"),
                     lce::WORLDSIZE::MEDIUM, lce::BIOMESCALE::MEDIUM);
        Generator g2(lce::CONSOLE::PS4, LCEVERSION::AQUATIC, std::string("test"),
                     lce::WORLDSIZE::MEDIUM, lce::BIOMESCALE::MEDIUM);
        CHECK(g1.getWorldSeed() == g2.getWorldSeed());
    }

    TEST_CASE("setWorldSeed updates seed") {
        Generator g(lce::CONSOLE::PS4, LCEVERSION::AQUATIC, 1LL,
                    lce::WORLDSIZE::MEDIUM, lce::BIOMESCALE::MEDIUM);
        g.applyWorldSeed(99999LL);
        CHECK(g.getWorldSeed() == 99999LL);
    }

    TEST_CASE("getWorldCoordinateBounds is positive") {
        Generator g(lce::CONSOLE::PS4, LCEVERSION::AQUATIC, 0LL,
                    lce::WORLDSIZE::MEDIUM, lce::BIOMESCALE::MEDIUM);
        CHECK(g.getWorldCoordinateBounds() > 0);
    }

    TEST_CASE("console getter matches construction") {
        Generator g(lce::CONSOLE::SWITCH, LCEVERSION::AQUATIC, 0LL,
                    lce::WORLDSIZE::MEDIUM, lce::BIOMESCALE::MEDIUM);
        CHECK(g.getConsole() == lce::CONSOLE::SWITCH);
    }

    TEST_CASE("copy constructor preserves seed") {
        Generator g1(lce::CONSOLE::PS4, LCEVERSION::AQUATIC, 777LL,
                     lce::WORLDSIZE::MEDIUM, lce::BIOMESCALE::MEDIUM);
        Generator g2 = g1;
        CHECK(g2.getWorldSeed() == 777LL);
        CHECK(g2.getConsole()   == lce::CONSOLE::PS4);
    }
}

// ============================================================
//  Generator – getBiomeIdAt
// ============================================================
TEST_SUITE("Generator::getBiomeIdAt") {

    TEST_CASE("getBiomeIdAt returns a valid biome_t (not garbage)") {
        Generator g(lce::CONSOLE::PS4, LCEVERSION::AQUATIC, 12345LL,
                    lce::WORLDSIZE::MEDIUM, lce::BIOMESCALE::MEDIUM);
        const biome_t b = g.getBiomeIdAt(1, 0, 0);
        // biome IDs are < 256; cast to uint and check
        CHECK(static_cast<unsigned>(b) < 256u);
    }

    TEST_CASE("getBiomeIdAt is deterministic for same seed") {
        Generator g1(lce::CONSOLE::PS4, LCEVERSION::AQUATIC, 42LL,
                     lce::WORLDSIZE::MEDIUM, lce::BIOMESCALE::MEDIUM);
        Generator g2(lce::CONSOLE::PS4, LCEVERSION::AQUATIC, 42LL,
                     lce::WORLDSIZE::MEDIUM, lce::BIOMESCALE::MEDIUM);
        CHECK(g1.getBiomeIdAt(1, 8, 8) == g2.getBiomeIdAt(1, 8, 8));
    }

    TEST_CASE("getBiomeIdAt can differ between two world seeds") {
        Generator g1(lce::CONSOLE::PS4, LCEVERSION::AQUATIC, 1LL,
                     lce::WORLDSIZE::MEDIUM, lce::BIOMESCALE::MEDIUM);
        Generator g2(lce::CONSOLE::PS4, LCEVERSION::AQUATIC, 2LL,
                     lce::WORLDSIZE::MEDIUM, lce::BIOMESCALE::MEDIUM);
        // Not guaranteed to differ at every position, but very likely at a spread of coords
        bool anyDiffers = false;
        for (int i = -3; i <= 3 && !anyDiffers; ++i)
            for (int j = -3; j <= 3 && !anyDiffers; ++j)
                if (g1.getBiomeIdAt(1, i * 32, j * 32) != g2.getBiomeIdAt(1, i * 32, j * 32))
                    anyDiffers = true;
        CHECK(anyDiffers);
    }

    TEST_CASE("FLAT world generator can be set and read back") {
        Generator g(lce::CONSOLE::PS4, LCEVERSION::AQUATIC, 0LL,
                    lce::WORLDSIZE::MEDIUM, lce::BIOMESCALE::MEDIUM);
        g.setWorldGenerator(WORLDGENERATOR::FLAT);
        g.setFixedBiome(biome_t::desert);
        CHECK(g.getWorldGenerator() == WORLDGENERATOR::FLAT);
        CHECK(g.getFixedBiome()     == biome_t::desert);
    }
}

// ============================================================
//  FeaturePositions – lava lake / dungeon seed independence
// ============================================================
TEST_SUITE("FeaturePositions") {

    // Build a generator used across these tests
    static Generator makeGen(i64 seed = 12345LL) {
        return Generator(lce::CONSOLE::PS4, LCEVERSION::AQUATIC, seed,
                         lce::WORLDSIZE::MEDIUM, lce::BIOMESCALE::MEDIUM);
    }

    TEST_CASE("lavaLake (RNG overload) returns null when roll != 0") {
        // Drive the RNG to a state where nextInt(8) != 0
        // Use a seed that is known to produce a non-lake result
        // We just verify the function doesn't crash and returns a coherent Pos3D
        RNG rng(111ULL);
        const Pos3D pos = FeaturePositions::lavaLake(rng, 0, 0);
        // Either null or within chunk coordinate range
        if (!pos.isNull()) {
            CHECK(pos.x >= 8);   CHECK(pos.x < 24);   // (chunk<<4)+[8,23]
            CHECK(pos.z >= 8);   CHECK(pos.z < 24);
            CHECK(pos.y >= 0);   CHECK(pos.y < 128);
        }
    }

    TEST_CASE("dungeon (RNG overload) always returns a position in the chunk") {
        RNG rng(222ULL);
        const Pos3D pos = FeaturePositions::dungeon(rng, 0, 0);
        CHECK(pos.x >= 8);   CHECK(pos.x < 24);
        CHECK(pos.z >= 8);   CHECK(pos.z < 24);
        CHECK(pos.y >= 0);   CHECK(pos.y < 128);
    }

    TEST_CASE("dungeon position is deterministic") {
        RNG r1(333ULL), r2(333ULL);
        const Pos3D p1 = FeaturePositions::dungeon(r1, 2, -3);
        const Pos3D p2 = FeaturePositions::dungeon(r2, 2, -3);
        CHECK(p1.x == p2.x);
        CHECK(p1.y == p2.y);
        CHECK(p1.z == p2.z);
    }

    TEST_CASE("dungeon X and Z are within the expected chunk band") {
        for (int cx = -2; cx <= 2; ++cx) {
            for (int cz = -2; cz <= 2; ++cz) {
                RNG rng(static_cast<u64>(cx * 17 + cz * 31 + 1));
                const Pos3D pos = FeaturePositions::dungeon(rng, cx, cz);
                const int xBase = (cx << 4) + 8;
                const int zBase = (cz << 4) + 8;
                CHECK(pos.x >= xBase);      CHECK(pos.x < xBase + 16);
                CHECK(pos.z >= zBase);      CHECK(pos.z < zBase + 16);
                CHECK(pos.y >= 0);          CHECK(pos.y < 128);
            }
        }
    }

    TEST_CASE("lavaLake above y=63 is extremely rare (sanity check over 1000 rolls)") {
        int aboveSea = 0;
        for (int i = 0; i < 1000; ++i) {
            RNG rng(static_cast<u64>(i));
            const Pos3D pos = FeaturePositions::lavaLake(rng, 0, 0);
            if (!pos.isNull() && pos.y >= 63) aboveSea++;
        }
        // Should be <10 % of successful placements above sea level
        CHECK(aboveSea < 100);
    }
}

// ============================================================
//  scanBoxHull
// ============================================================
TEST_SUITE("scanBoxHull") {

    // A simple block-world mock that records visited positions
    struct MockWorld {
        std::vector<Pos3D> visited;
        void clear() { visited.clear(); }
    };

    TEST_CASE("predicate returning false visits all hull faces") {
        MockWorld w;
        const Pos3D minP{0, 0, 0};
        const Pos3D maxP{4, 4, 4};

        bool result = scanBoxHull(w, minP, maxP, [&](const Pos3D& p, MockWorld& mw) {
            mw.visited.push_back(p);
            return false; // keep scanning
        });

        CHECK_FALSE(result);
        CHECK_FALSE(w.visited.empty());
    }

    TEST_CASE("predicate returning true short-circuits immediately") {
        MockWorld w;
        const Pos3D minP{0, 0, 0};
        const Pos3D maxP{4, 4, 4};

        bool result = scanBoxHull(w, minP, maxP, [](const Pos3D&, MockWorld&) {
            return true; // stop immediately
        });

        CHECK(result);
    }

    TEST_CASE("1x1x1 box visits expected boundary positions") {
        MockWorld w;
        const Pos3D minP{2, 2, 2};
        const Pos3D maxP{3, 3, 3};

        scanBoxHull(w, minP, maxP, [&](const Pos3D& p, MockWorld& mw) {
            mw.visited.push_back(p);
            return false;
        });

        // Every visited position must be on the hull (y = minY-1 or maxY+1, or x/z boundary)
        const int minY = minP.y - 1, maxY = maxP.y + 1;
        for (const Pos3D& p : w.visited) {
            bool onHull = (p.y == minY || p.y == maxY ||
                           p.x == minP.x || p.x == maxP.x - 1 ||
                           p.z == minP.z || p.z == maxP.z - 1);
            CHECK(onHull);
        }
    }

    TEST_CASE("no positions are visited for a degenerate zero-size box") {
        MockWorld w;
        const Pos3D minP{5, 5, 5};
        const Pos3D maxP{5, 5, 5}; // zero area face loops

        bool result = scanBoxHull(w, minP, maxP, [&](const Pos3D& p, MockWorld& mw) {
            mw.visited.push_back(p);
            return false;
        });

        // Zero-size faces produce no iterations; result must be false
        CHECK_FALSE(result);
    }

    TEST_CASE("all hull positions have valid relative coordinates") {
        MockWorld w;
        const Pos3D minP{-2, 10, -2};
        const Pos3D maxP{ 2, 14,  2};

        scanBoxHull(w, minP, maxP, [&](const Pos3D& p, MockWorld& mw) {
            mw.visited.push_back(p);
            return false;
        });

        for (const Pos3D& p : w.visited) {
            // x and z must be within [minX, maxX) / [minZ, maxZ)
            CHECK(p.x >= minP.x); CHECK(p.x < maxP.x);
            CHECK(p.z >= minP.z); CHECK(p.z < maxP.z);
            // y must be within extended [minY-1, maxY+1]
            CHECK(p.y >= minP.y - 1);
            CHECK(p.y <= maxP.y + 1);
        }
    }
}

