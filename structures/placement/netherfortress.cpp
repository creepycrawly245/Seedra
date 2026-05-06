#include "netherfortress.hpp"

namespace Placement {
    Pos2DVec_t NetherFortress::getWorldPositions(c_i64 worldSeed, lce::WORLDSIZE worldSize) noexcept {
        RNG rng;
        rng.setSeed(static_cast<u64>(worldSeed));
        rng.next<32>();

        Pos2DVec_t pos2DVec;

        // Present in all world sizes
        c_int pos = rng.nextInt<49>();
        pos2DVec.emplace_back(pos % 7, pos / 7);

        if (worldSize >= lce::WORLDSIZE::MEDIUM) {
            int netherSize = lce::getChunkNetherBounds(worldSize);
            int regionSize = netherSize >> 4;
            for (int cx = -regionSize; cx <= regionSize; ++cx) {
                for (int cz = -regionSize; cz <= regionSize; ++cz) {
                    rng.setSeed(cx ^ (cz << 4) ^ static_cast<u64>(worldSeed));
                    rng.next<32>();

                    if (rng.nextInt(3) != 0) {
                        continue;
                    }

                    int x = (cx << 4) + 4 + rng.nextInt(8);
                    int z = (cz << 4) + 4 + rng.nextInt(8);
                    pos2DVec.emplace_back(x, z);
                }
            }

        }
        return pos2DVec;
    }

}
