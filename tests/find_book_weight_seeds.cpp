/**
 * @file find_book_weight_seeds.cpp
 *
 * Finds one loot-table seed for every distinct raw enchant-weight integer w
 * (0, 1, 2, … TW-1) that is rolled when the first item from a
 * stronghold_library chest is an Enchanted Book.
 *
 * Because ELYTRA and AQUATIC have a different active enchant set (aquatic adds
 * impaling/riptide/loyalty/channeling), their per-level totalWeight values
 * differ.  We therefore run two completely independent searches:
 *
 *   Group A – ELYTRA  : reference = XBOX360_ELYTRA
 *                        columns   = XBOX360_ELYTRA, WIIU_ELYTRA
 *   Group B – AQUATIC : reference = XBOX360_AQUATIC
 *                        columns   = XBOX360_AQUATIC, WIIU_AQUATIC
 *
 * ── stronghold_library table (TableWrapper<2,10,...>) ────────────────────────
 *   Item            Weight  Cumulative
 *   BOOK              100     100
 *   PAPER             100     200
 *   MAP                 5     205
 *   COMPASS             5     210
 *   ENCHANTED_BOOK     60     270   <- seeds landing here are kept
 *
 * ── RNG sequence (MOD_NO_SHUF, first roll is the book) ──────────────────────
 *   rng.setSeed(seed)
 *   [1] nextInt(9)           roll-count draw (RETURNS_R, nextInt<false>(2,10))
 *   [2] nextInt(270)         item-selection weight  [must be 210..269]
 *   [3] Roll<1,1,false>      book count             → no advance
 *   [4] nextInt<30,30,false> base enchant level     → no advance  (= 30)
 *   [5] nextInt(1)           level += 1+nextInt(1)  → advance, always 0
 *   [6] nextInt(1)           level +=  +nextInt(1)  → advance, always 0
 *                            → xpLevel_pre = 31
 *   [7] nextFloat()          jitter f1
 *   [8] nextFloat()          jitter f2
 *                            → xpLevel = clamp(round(31 + 31*f), 1, INT_MAX)
 *                            → array   = getLevelArray(xpLevel)
 *   [9] nextInt(array->totalWeight)   ← THIS IS w
 */

#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "common/MathHelper.hpp"
#include "common/rng.hpp"
#include "lce/items/itemsInit.hpp"
#include "lce/registry/itemRegistry.hpp"
#include "loot/classes/Container.hpp"
#include "loot/classes/LootGenMode.hpp"
#include "loot/enchants/enchantController.hpp"
#include "loot/enchants/fastBookEnchantArray.hpp"
#include "loot/Tables.hpp"

// ─────────────────────────────────────────────────────────────────────────────
//  Table geometry (stronghold_library)
// ─────────────────────────────────────────────────────────────────────────────

static constexpr int LOOT_TOTAL_WEIGHT = 270;
static constexpr int BOOK_WEIGHT_LO    = 210;
static constexpr int BOOK_WEIGHT_HI    = 269;
static constexpr int ROLL_COUNT_RANGE  = 9;    // nextInt(9) for getRollCount<false>(2,10)
static constexpr int ENCHANT_LEVEL_COUNT = 48; // VECTOR_COUNT in EnchantLookupTable

// ─────────────────────────────────────────────────────────────────────────────
//  Console/version descriptor
// ─────────────────────────────────────────────────────────────────────────────

struct CV {
    lce::CONSOLE console;
    LCEVERSION   version;
    const char*  label;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Per-version-group search configuration
// ─────────────────────────────────────────────────────────────────────────────

struct Group {
    const char*      name;      // human-readable group name
    CV               reference; // defines the enchant weight space for the search
    std::vector<CV>  columns;   // consoles printed in the output table
};

static const std::vector<Group> GROUPS = {
    {
        "ELYTRA",
        { lce::CONSOLE::XBOX360, LCEVERSION::ELYTRA, "XBOX360_ELYTRA" },
        {
            { lce::CONSOLE::XBOX360, LCEVERSION::ELYTRA, "XBOX360_ELYTRA" },
            { lce::CONSOLE::WIIU,    LCEVERSION::ELYTRA, "WIIU_ELYTRA"    },
        }
    },
    {
        "AQUATIC",
        { lce::CONSOLE::XBOX360, LCEVERSION::AQUATIC, "XBOX360_AQUATIC" },
        {
            { lce::CONSOLE::XBOX360, LCEVERSION::AQUATIC, "XBOX360_AQUATIC" },
            { lce::CONSOLE::WIIU,    LCEVERSION::AQUATIC, "WIIU_AQUATIC"    },
        }
    },
};

// ─────────────────────────────────────────────────────────────────────────────
//  Fast RNG replay — extracts the raw enchant-weight value w for a seed
// ─────────────────────────────────────────────────────────────────────────────

static int extractEnchantWeight(int64_t seed,
                                const std::array<int, ENCHANT_LEVEL_COUNT>& weightForLevel) {
    RNG rng;
    rng.setSeed(static_cast<uint64_t>(seed));

    // [1] roll-count
    rng.nextInt(ROLL_COUNT_RANGE);

    // [2] item-selection weight
    const int itemW = rng.nextInt(LOOT_TOTAL_WEIGHT);
    if (itemW < BOOK_WEIGHT_LO || itemW > BOOK_WEIGHT_HI) return -1;

    // [3] Roll<1,1,false> → no-op
    // [4] nextInt<30,30,false> → no-op

    // [5][6] nextInt(1) ×2 — advance seed, always return 0
    rng.nextInt(1);
    rng.nextInt(1);

    // [7][8] float jitter
    const float f1 = rng.nextFloat();
    const float f2 = rng.nextFloat();
    const float f  = (f1 + f2 - 1.0f) * 0.15f;

    // xpLevel = clamp(round(31 + 31*f), 1, INT_MAX)
    const int xpLevel = MathHelper::clamp(
        static_cast<int>(std::round(31.0f + 31.0f * f)),
        1, 0x7fffffff);

    // Clamp to valid bucket index
    const int arrayIdx = std::min(xpLevel, ENCHANT_LEVEL_COUNT - 1);
    if (arrayIdx < 25 || arrayIdx > 27) return -1;

    // [9] nextInt(array->totalWeight)
    const int tw = weightForLevel[arrayIdx];
    if (tw <= 0) return -1;
    return rng.nextInt(tw);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Full loot-engine generation for display
// ─────────────────────────────────────────────────────────────────────────────

static std::string generateEnchantString(int64_t seed, lce::CONSOLE console, LCEVERSION version) {
    enchants::EnchantController::setup(console, version);

    loot::Container<27> container;
    loot::stronghold_library.getLootFromLootTableSeed<loot::GenMode::MOD_NO_SHUF>(
            container, seed, nullptr);

    if (container.slotCount() == 0) return "(empty)";
    const lce::ItemState& first = container.getSlotAt(0);
    if (first.getID() != lce::items::ids::ENCHANTED_BOOK_ID) return "(no book)";

    std::string out;
    bool any = false;
    for (size_t i = 0; i < container.enchantCount(); ++i) {
        const enchants::EnchantState& es = container.getEnchantAt(i);
        if (es.getExtra() != 0) continue;
        const enchants::Enchant* ep =
                enchants::EnchantController::getEnchantFromID(es.getID());
        if (!any) any = true; else out += ", ";
        out += ep ? ep->getName() : "?";
        out += ' ';
        out += std::to_string(es.getLevel());
    }
    return any ? out : "(no enchants)";
}

// ─────────────────────────────────────────────────────────────────────────────
//  Formatting helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::string colR(const std::string& s, int w) {
    if ((int)s.size() >= w) return s.substr(0, w);
    return std::string(w - (int)s.size(), ' ') + s;
}
static std::string colL(const std::string& s, int w) {
    if ((int)s.size() >= w) return s.substr(0, w);
    return s + std::string(w - (int)s.size(), ' ');
}
static std::string trunc(const std::string& s, int maxLen) {
    return (int)s.size() <= maxLen ? s : s.substr(0, maxLen - 3) + "...";
}

// ─────────────────────────────────────────────────────────────────────────────
//  Per-group search + output
// ─────────────────────────────────────────────────────────────────────────────

static void runGroup(const Group& g, std::ofstream& outFile) {

    // stdout gets everything; file gets only the compact weight+seed lines.
    auto emit = [&](const std::string& line) {
        std::cout << line << '\n';
    };
    auto emitFile = [&](const std::string& line) {
        if (outFile) outFile << line << '\n';
    };

    emit("\n════════════════════════════════════════════════════════════");
    emit(std::string("  GROUP: ") + g.name + "  (reference: " + g.reference.label + ")");
    emit("════════════════════════════════════════════════════════════\n");

    // File heading: just "Aquatic:" / "Elytra:"
    emitFile(std::string(g.name) + ":");

    // Setup reference console so the lookup table reflects this version's enchants
    enchants::EnchantController::setup(g.reference.console, g.reference.version);

    // Precompute per-level totalWeight for this version group.
    // getLevelArray(i)->clear() only resets index arrays, not totalWeight — safe.
    std::array<int, ENCHANT_LEVEL_COUNT> weightForLevel{};
    int globalMaxW = 0;
    for (int i = 0; i < ENCHANT_LEVEL_COUNT; ++i) {
        const enchants::EnchantLevelArray* arr =
                enchants::EnchantController::getLevelArray(i);
        // totalWeight is i8; cast through uint8_t to avoid sign-extension
        weightForLevel[i] = static_cast<int>(static_cast<uint8_t>(arr->totalWeight));
        globalMaxW = std::max(globalMaxW, weightForLevel[i]);
    }
    const int TW = globalMaxW;

    emit("Max enchant TW  : " + std::to_string(TW));
    emit("Searching w in  : [0 .. " + std::to_string(TW - 1) + "]\n");

    // ── Search + inline validation ────────────────────────────────────────────
    // A seed is only accepted when:
    //   1. extractEnchantWeight returns the target w
    //   2. Full chest generation yields exactly 1 enchanted book
    //   3. That book carries exactly 1 enchant
    std::vector<int64_t> seedForW(TW, -1LL);
    int found = 0;

    auto isValidSeed = [&](int64_t seed, int /*expectedW*/) -> bool {
        enchants::EnchantController::setup(g.reference.console, g.reference.version);

        // ── Pass 1: MOD_NO_SHUF ──────────────────────────────────────────────
        // Verifies item/enchant counts before shuffling.
        {
            loot::Container<27> container;
            loot::stronghold_library.getLootFromLootTableSeed<loot::GenMode::MOD_NO_SHUF>(
                    container, seed, nullptr);

            // exactly 1 enchanted book
            int bookCount = 0;
            for (size_t s = 0; s < container.slotCount(); ++s) {
                if (container.getSlotAt(static_cast<int>(s)).getID() == lce::items::ids::ENCHANTED_BOOK_ID)
                    ++bookCount;
            }
            if (bookCount != 1) return false;

            // exactly 1 enchant on that book (extra == 0 → added before first addItem)
            int enchCount = 0;
            for (size_t e = 0; e < container.enchantCount(); ++e) {
                if (container.getEnchantAt(static_cast<int>(e)).getExtra() == 0)
                    ++enchCount;
            }
            if (enchCount != 1) return false;
        }

        // ── Pass 2: MODERN (with shuffle) ────────────────────────────────────
        // After shuffling the book must land in slot index 0.
        {
            enchants::EnchantController::setup(g.reference.console, g.reference.version);
            loot::Container<27> container;
            loot::stronghold_library.getLootFromLootTableSeed<loot::GenMode::MODERN>(
                    container, seed, nullptr);

            if (container.getSlotAt(0).getID() != lce::items::ids::ENCHANTED_BOOK_ID)
                return false;
        }

        return true;
    };

    constexpr int64_t SEARCH_LIMIT = 500'000'000LL;
    for (int64_t seed = 1; seed < SEARCH_LIMIT && found < TW; ++seed) {
        const int w = extractEnchantWeight(seed, weightForLevel);
        if (w < 0 || w >= TW) continue;
        if (seedForW[w] != -1LL) continue;
        if (!isValidSeed(seed, w)) continue;
        seedForW[w] = seed;
        ++found;
    }
    emit("Found " + std::to_string(found) + " / " + std::to_string(TW) + " weight values.\n");

    // ── Stdout table ─────────────────────────────────────────────────────────
    constexpr int CW_W    =  4;
    constexpr int CW_SEED = 14;
    constexpr int CW_ENCH = 44;
    const std::string SEP = " | ";

    const int ruleW = CW_W + (int)SEP.size() + CW_SEED
                    + (int)g.columns.size() * ((int)SEP.size() + CW_ENCH);
    const std::string rule(ruleW, '-');

    emit(rule);
    {
        std::string hdr = colR("w", CW_W) + SEP + colL("lootTableSeed", CW_SEED);
        for (const auto& cv : g.columns) {
            hdr += SEP;
            hdr += colL(cv.label, CW_ENCH);
        }
        emit(hdr);
    }
    emit(rule);

    for (int w = 0; w < TW; ++w) {
        const int64_t seed = seedForW[w];
        const std::string seedStr = seed >= 0 ? std::to_string(seed) : "NOT_FOUND";

        // stdout: full row with enchant columns
        std::string row = colR(std::to_string(w), CW_W)
                        + SEP
                        + colL(seedStr, CW_SEED);
        for (const auto& cv : g.columns) {
            const std::string ench = seed >= 0
                ? generateEnchantString(seed, cv.console, cv.version)
                : "-";
            row += SEP;
            row += colL(trunc(ench, CW_ENCH), CW_ENCH);
        }
        emit(row);

        // file: just "w seed"
        emitFile("{" + std::to_string(w) + ", " + seedStr + "},");
    }
    emit(rule);
    emitFile("");   // blank line between groups
}

// ─────────────────────────────────────────────────────────────────────────────
//  Main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    const std::string outFilename = "stronghold_library_book_enchant_seeds.txt";
    std::ofstream outFile(outFilename);
    if (!outFile)
        std::cerr << "Warning: could not open " << outFilename << '\n';

    for (const auto& g : GROUPS) {
        runGroup(g, outFile);
    }

    if (outFile) {
        outFile.close();
        std::cout << "\nResults written to: " << outFilename << '\n';
    }
    return 0;
}

