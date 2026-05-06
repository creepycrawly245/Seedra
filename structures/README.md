# Structures

This module handles both **placement** (where does the game decide to put a structure?) and **generation** (what blocks make up that structure?).

---

## Directory Layout

```
structures/
├── placement/          # Placement logic for all structures
│   ├── StaticStructures.hpp/.cpp   # Feature, Village, OceanRuin
│   ├── DynamicStructures.hpp/.cpp  # Monument, Mansion, BuriedTreasure, Shipwreck, Outpost
│   ├── stronghold.hpp/.cpp         # Stronghold (special-case placement)
│   ├── netherfortress.hpp/.cpp     # Nether Fortress
│   └── mineshaft.hpp/.cpp          # Mineshaft
├── gen/                # Block-level generation (piece-based, "gen1")
│   ├── Structure.hpp               # Base template for all gen1 structures
│   ├── FeaturePiece.hpp            # Macro helpers for piece functions
│   ├── ScatteredFeature.hpp        # Shared scattered-feature base
│   ├── scatteredFeature.cpp
│   ├── Structure.cpp
│   ├── stronghold/                 # Stronghold pieces + rolls
│   │   ├── stronghold.hpp/.cpp     # Stronghold structure + generation
│   │   ├── rolls.cpp               # Per-chunk RNG rolls (chest/portal)
│   │   └── StrongholdStones.hpp    # Block palette
│   ├── village/                    # Village pieces + rolls
│   │   ├── village.hpp
│   │   ├── build.cpp
│   │   └── gen.cpp
│   ├── desert_temple/
│   │   └── desert_pyramid.hpp/.cpp
│   ├── jungle_temple/
│   │   ├── jungle_temple.hpp/.cpp
│   │   └── JungleStones.hpp        # Block palette
│   ├── igloo/
│   │   └── igloo.hpp/.cpp
│   ├── mineshaft/
│   │   ├── mineshaft.hpp
│   │   ├── build.cpp
│   │   ├── gen.cpp
│   │   └── rolls.cpp
│   ├── netherfortress/
│   │   └── netherfortress.hpp/.cpp
│   └── witch_hut/
│       └── witch_hut.hpp/.cpp
└── gen2/               # NBT-template-based generation ("gen2")
    ├── Template.hpp/.cpp               # Block/entity info, NBT loading
    ├── TemplateManager.hpp/.cpp        # Manages loaded templates
    ├── StructureComponentTemplate.hpp/.cpp  # Component wrapper
    ├── PlacementSettings.hpp           # Rotation + mirror settings
    ├── ResourceLocation.hpp            # Resource path helper
    ├── ocean_monument/
    │   ├── OceanMonument.hpp
    │   ├── OceanMonumentPieces.hpp
    │   └── OceanMonumentPieces.cpp
    └── woodland_mansion/
        ├── WoodlandMansion.hpp
        └── WoodlandMansionPieces.hpp
```

---

## Placement

All placement classes live in `namespace Placement`.  
Call `setWorldSize()` once whenever the world size changes — it adjusts region sizes, chunk ranges, and spacing for that structure type.

### Quick reference

```cpp
using namespace Placement;

// ── Static (one roll per region, biome check at that position) ──
Feature::setWorldSize(worldSize);
Village<false>::setWorldSize(worldSize);  // LCE ≤ 1.13
Village<true>::setWorldSize(worldSize);   // LCE ≥ 1.14 (PS4 variant)
OceanRuin::setWorldSize(worldSize);

// ── Dynamic (up to 60 candidate rolls per region, biome check on each) ──
Monument::setWorldSize(worldSize);
Mansion::setWorldSize(worldSize);
BuriedTreasure::setWorldSize(worldSize);
Shipwreck::setWorldSize(worldSize);
Outpost::setWorldSize(worldSize);

// ── Special-case ──
Stronghold::setWorldSize(worldSize);      // toggles useFarStronghold
// Mineshaft and NetherFortress need no setup
```

---

### Static Structures

`StaticStructure<Derived>` places exactly one candidate per grid region. The candidate chunk is computed from the world seed + region coordinates + a per-structure salt; it only actually spawns if the biome at that position matches `VALID_BIOMES`.

**Region size** is the number of chunks on each axis of a placement grid cell.  
**Chunk range** is the `nextInt()` bound used when picking the candidate chunk offset within a region (`[0, CHUNK_RANGE)`).  
**Reduced spacing** applies to small worlds (< `MEDIUM`); those worlds use a halved region size.

| Class | Salt | Region Size (normal / small) | Chunk Range (normal / small) | Valid Biomes |
|---|---|---|---|---|
| `Feature` | 14357617 | 32 / 16 | 24 / 8 | desert/desert_hills → Desert Pyramid; jungle/jungle_hills/bamboo_jungle → Jungle Pyramid; swamp → Swamp Hut; snowy_tundra/snowy_taiga → Igloo |
| `Village<false>` *(LCE ≤ 1.13)* | 10387312 | 32 / 16 | 24 / 8 | plains, desert, taiga, ice_plains, cold_taiga, savanna |
| `Village<true>` *(LCE ≥ 1.14 / PS4)* | 10387312 | 32 / 16 | 23 / 7 | plains, desert, taiga, ice_plains, cold_taiga, savanna |
| `OceanRuin` | 14357617 | 8 (fixed) | 6 (fixed) | ocean, deep_ocean, warm_ocean, deep_warm_ocean, lukewarm_ocean, deep_lukewarm_ocean, cold_ocean, deep_cold_ocean, frozen_ocean, deep_frozen_ocean |

> **Feature** does not use a `VALID_BIOMES` bitmask for placement verification; instead the spawned structure *type* is determined by a per-biome switch after the position is computed, so any of the four feature biome groups will pass.

#### `Feature`  *(desert pyramid, jungle temple, swamp hut, igloo)*

```cpp
// Seed-only position query (no biome check)
Pos2D Feature::getRegionChunkPosition(i64 worldSeed, int regionX, int regionZ);
Pos2D Feature::getRegionBlockPosition(i64 worldSeed, int regionX, int regionZ);

// What feature spawns at a block position?
StructureType Feature::getFeatureType(biome_t biome);
StructureType Feature::getFeatureTypeAt(const Generator* g, int blockX, int blockZ);
StructureType Feature::getFeatureTypeAt(const Generator* g, Pos2D block);

// All spawned features in the world / within bounds
std::vector<FeatureStructurePair> Feature::getAllFeaturePositions(const Generator* g);
std::vector<FeatureStructurePair> Feature::getAllFeaturePositionsBounded(
    const Generator* g, int lowerX, int lowerZ, int upperX, int upperZ);

// All features split into per-type vectors (index = StructureType)
std::vector<std::vector<Pos2D>> Feature::getAllFeaturePositionsSeparated(const Generator* g);
```

`FeatureStructurePair` holds `Pos2D m_pos` and `StructureType m_type` and is printable via `<<`.

#### `Village<bool isPS4>`

```cpp
// Inherits the full StaticStructure<> interface below.
// Village<false>  →  LCE ≤ 1.13 (CHUNK_RANGE = 8 small / 24 normal)
// Village<true>   →  LCE ≥ 1.14 / PS4 (CHUNK_RANGE = 7 small / 23 normal)
Village<false>::setWorldSize(worldSize);
std::vector<Pos2D> Village<false>::getAllPositions(const Generator* g);
```

#### `OceanRuin`

```cpp
OceanRuin::setWorldSize(worldSize);
std::vector<Pos2D> OceanRuin::getAllPositions(const Generator* g);
```

#### Shared `StaticStructure<Derived>` API

```cpp
// Single-region queries (no biome check)
Pos2D Derived::getRegionChunkPosition(i64 worldSeed, int regionX, int regionZ);
Pos2D Derived::getRegionBlockPosition(i64 worldSeed, int regionX, int regionZ);

// Biome-verified queries
bool Derived::verifyChunkPosition(const Generator* g, int chunkX, int chunkZ);
bool Derived::verifyChunkPosition(const Generator* g, Pos2D chunkPos);
bool Derived::verifyBlockPosition(const Generator* g, int blockX, int blockZ);
bool Derived::verifyBlockPosition(const Generator* g, Pos2D blockPos);

// All positions in world / within block-coordinate bounds
std::vector<Pos2D> Derived::getAllPositions(const Generator* g);
std::vector<Pos2D> Derived::getAllPositionsBounded(
    const Generator* g, int lowerX, int lowerZ, int upperX, int upperZ);
```

---

### Dynamic Structures

`DynamicStructure<Derived>` rolls up to `ATTEMPTS` unique chunk candidates per region, stopping at the first one that passes both the primary and (optionally) secondary biome checks.

**Main radius** — the square half-width (in blocks) passed to `areBiomesViable` for the primary biome check.  
**Second radius** — the square half-width used for the secondary biome check (only when `HAS_SECOND_BIOME_CHECK = true`).  
**Attempts** — maximum unique candidate chunks tried per region before giving up.  
**Chunk range** — `nextInt()` bound for picking a candidate chunk offset within the region (`[0, CHUNK_RANGE)`).

| Class            | Salt      | Region Size (normal / small) | Chunk Range (normal / small) | Attempts (normal / small) | Main Radius | Second Radius | Secondary Check      | Main Biomes                                                                                                                                                                 | Secondary Biomes                                                                     |
|------------------|-----------|------------------------------|------------------------------|---------------------------|-------------|---------------|----------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------|
| `Monument`       | 10387313  | 80 / 32                      | 75 / 27                      | 40 / 60                   | 8           | 29            | ✔ deep ocean only    | ocean, river, frozen_river, deep_ocean, warm_ocean, deep_warm_ocean, lukewarm_ocean, deep_lukewarm_ocean, cold_ocean, deep_cold_ocean, frozen_ocean, deep_frozen_ocean      | deep_ocean, deep_warm_ocean, deep_lukewarm_ocean, deep_cold_ocean, deep_frozen_ocean |
| `Mansion`        | 10387319  | 80 / 32                      | 74 / 26                      | 40 / 60                   | 4           | 32            | ✔ non-special biome  | dark_forest                                                                                                                                                                 | All common non-special biomes *(see `DEFAULT_SECONDARY_VALID_BIOMES`)*               |
| `BuriedTreasure` | 16842397  | 4 / 32                       | 2 / 30                       | 4 / 60                    | 0           | 16            | ✔ non-special biome  | mushroom_island_shore, beach, stone_beach, cold_beach                                                                                                                       | All common non-special biomes *(see `DEFAULT_SECONDARY_VALID_BIOMES`)*               |
| `Shipwreck`      | 14357617  | 10 / 32                      | 5 / 27                       | 20 / 60                   | 10          | —             | ✗                    | ocean, mushroom_island_shore, beach, snowy_beach, deep_ocean, warm_ocean, lukewarm_ocean, deep_lukewarm_ocean, cold_ocean, deep_cold_ocean, frozen_ocean, deep_frozen_ocean | —                                                                                    |
| `Outpost`        | 165745296 | 48 / 32                      | 42 / 26                      | ? / 64                    | 32          | —             | ✗                    | plains, desert, taiga, ice_plains, cold_taiga, savanna                                                                                                                      | —                                                                                    |

> **BuriedTreasure** `getAllPositions` / `getAllPositionsBounded` add `+1` to every returned Z coordinate (vanilla offset).  
> **"normal"** = world size ≥ `MEDIUM`; **"small"** = world size < `MEDIUM` (reduced spacing mode).

#### Shared `DynamicStructure<Derived>` API

```cpp
// Set once per world-size change
Derived::setWorldSize(lce::WORLDSIZE worldSize);

// Biome-verified position for a region (returns {0,0} if nothing spawns)
Pos2D Derived::getPosition(const Generator* g, int regionX, int regionZ);

// All candidate chunks the RNG can roll in a region (no biome check)
Pos2DVec_t Derived::getAllPossibleChunks(i64 worldSeed, int regionX, int regionZ);

// Is a given chunk one of those candidates?
bool Derived::isPossibleChunkPos(i64 worldSeed, int regionX, int regionZ, const Pos2D& pos);

// Does the structure actually spawn at this chunk (candidate check only, no biome)?
bool Derived::canSpawnAtChunk(i64 worldSeed, int chunkX, int chunkZ, int regionX, int regionZ);

// Biome check helpers
bool Derived::verifyBlockPosition(const Generator* g, int blockX, int blockZ);
bool Derived::verifyChunkPosition(const Generator* g, int chunkX, int chunkZ);

// All spawned positions in world / within block-coordinate bounds
// terminateFlag: set to true from another thread to abort early
std::vector<Pos2D> Derived::getAllPositions(
    const Generator* g, std::atomic_bool* terminateFlag = nullptr);
std::vector<Pos2D> Derived::getAllPositionsBounded(
    const Generator* g, int lowerX, int lowerZ, int upperX, int upperZ,
    const std::atomic_bool* terminateFlag = nullptr);
```

---

### Stronghold

Stronghold placement is seed-driven (angle sweep + `locateBiome`), not region-based.

```cpp
// Must be called when the world size changes
Stronghold::setWorldSize(lce::WORLDSIZE worldSize);
// worldSize >= MEDIUM  →  useFarStronghold = true  (multiplier fixed at 32 chunks)
// worldSize <  MEDIUM  →  useFarStronghold = false (multiplier 3–6 chunks, Xbox random)

// Returns one Pos2D per stronghold (count set via g.setStrongholdCount())
// Every position is guaranteed to be in a valid biome (stronghold_biomes)
std::vector<Pos2D> Stronghold::getWorldPositions(const Generator& g);

// Biome bitmask used for placement
static constexpr uint64_t Stronghold::stronghold_biomes;
// plains, desert, extreme_hills, forest, taiga, hell, the_end, ice_plains, ice_mountains,
// mushroom_island, desert_hills, forest_hills, taiga_hills, extreme_hills_edge, jungle,
// jungle_hills, jungle_edge, stone_beach, birch_forest, birch_forest_hills, roofed_forest,
// cold_taiga, cold_taiga_hills, mega_taiga, mega_taiga_hills, extreme_hills_plus_trees,
// savanna, savanna_plateau, mesa, mesa_plateau_stone, mesa_plateau
```

---

### Nether Fortress

```cpp
// Returns a chunk-grid position {x ∈ [0,6], z ∈ [0,6]}
Pos2D NetherFortress::getWorldPosition(i64 worldSeed);
```

---

### Mineshaft

Mineshafts are not region-based; every chunk independently has a ~0.4 % chance of spawning one (biased away from origin).

```cpp
// Block-coordinate bounds
Pos2DVec_t Mineshaft::getPositions(i64 worldSeed,
                                    int xLower, int zLower, int xUpper, int zUpper);

// Square radius around a centre point (block coordinates)
Pos2DVec_t Mineshaft::getPositions(i64 worldSeed, int x, int z, int radius);

// Full world using generator bounds
Pos2DVec_t Mineshaft::getAllPositions(const Generator& g);
```

All returned positions are block-space with the standard `(chunk << 4) + 8` offset.

---

## Generation

### gen1 – Piece-based (`structures/gen/`)

Structures built from `StructureComponent` pieces stored in a fixed-size array inside `Structure<N>`.

```cpp
// Base interface (Structure<N>)
StructureComponent& getPiece(int index);
const StructureComponent& getPieceConst(int index) const;
int getPieceCount() const;
const BoundingBox& getStructureBB() const;
const Pos2D& getStartPos() const;
```

#### `gen::Stronghold`

```cpp
gen::Stronghold sh;
sh.generate(i64 worldSeed, int chunkX, int chunkZ);
sh.generate(i64 worldSeed, Pos2D chunkPos);

// After generate():
sh.getPieceCount();           // number of placed pieces
sh.m_portalRoomPiece;         // pointer to the End Portal room piece (nullptr if not yet rolled)
sh.m_eyesCount;               // number of active Eyes of Ender in the portal (populated by rolls)
sh.m_altarChestsArray[0..3];  // pointers to altar chest pieces
```

##### Rolls

```cpp
// In namespace rolls (stronghold/rolls.cpp):
// Drives the RNG forward for every chunk that intersects the stronghold
// Returns true when the target piece (chest / portal) has been hit
template<bool stopStrongholdChest, bool stopPortal>
bool Stronghold::additionalStrongholdRolls(
    ChunkPrimer* chunk, const gen::Stronghold* sg, RNG& rng,
    int xChunk, int zChunk, const StructureComponent& pieceStop);
```

---

### gen2 – NBT-template-based (`structures/gen2/`)

Used for Ocean Monument and Woodland Mansion. Templates are loaded from NBT data and placed using `PlacementSettings` (rotation + mirror).

```cpp
// Load and query a structure template
Template tmpl;
Pos3D size = tmpl.getSize();

// Apply to world
PlacementSettings settings;
settings.setRotation(Rotation::NONE);
settings.setMirror(Mirror::NONE);
tmpl.addBlocksToWorld(world, origin, settings);

// Template manager (caches loaded templates by ResourceLocation)
TemplateManager mgr;
Template* t = mgr.getTemplate(ResourceLocation("minecraft", "ocean_monument/base"));
```
