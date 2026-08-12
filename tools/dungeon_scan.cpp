#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "common/AreaRange.hpp"
#include "structures/__include.hpp"
#include "terrain/World.hpp"
#include "terrain/Chunk.hpp"
#include "terrain/decorators/WorldGenLakes.hpp"
#include "terrain/generator.hpp"
#include "lce/blocks/__include.hpp"

struct DungeonHit { Pos3D pos; std::string mob; double dist; };

static bool generateDungeon(World* world, RNG& rng, const Pos3D& position, std::string& mobOut) {
    using namespace lce::blocks;
    const int halfSizeX = rng.nextInt(2) + 2;
    const int minX = -halfSizeX - 1, maxX = halfSizeX + 1;
    constexpr int minY = -1, maxY = 4;
    const int halfSizeZ = rng.nextInt(2) + 2;
    const int minZ = -halfSizeZ - 1, maxZ = halfSizeZ + 1;
    int airCount = 0;
    for (int ox=minX; ox<=maxX; ++ox) for (int oy=minY; oy<=maxY; ++oy) for (int oz=minZ; oz<=maxZ; ++oz) {
        Pos3D p=position.add(ox,oy,oz); bool solid=isSolidBlock(world->getBlockId(p));
        if (oy==minY && !solid) return false;
        if (oy==maxY && !solid) return false;
        if ((ox==minX||ox==maxX||oz==minZ||oz==maxZ) && oy==0 && world->isAirBlock(p) && world->isAirBlock(p.up())) ++airCount;
    }
    if (airCount < 1 || airCount > 5) return false;
    for (int ox=minX; ox<=maxX; ++ox) for (int oy=maxY-1; oy>=minY; --oy) for (int oz=minZ; oz<=maxZ; ++oz) {
        Pos3D p=position.add(ox,oy,oz);
        if (ox!=minX && oy!=minY && oz!=minZ && ox!=maxX && oy!=maxY && oz!=maxZ) {
            if (world->getBlockId(p)!=CHEST_ID) world->setBlockId(p,AIR_ID);
        } else if (p.getY()>=0 && !isSolidBlock(world->getBlockId(p.down()))) world->setBlockId(p,AIR_ID);
        else if (isSolidBlock(world->getBlockId(p)) && world->getBlockId(p)!=CHEST_ID) {
            world->setBlockId(p, (oy==minY && rng.nextInt(4)!=0) ? MOSS_STONE_ID : COBBLESTONE_ID);
        }
    }
    for (int ca=0; ca<2; ++ca) for (int pa=0; pa<3; ++pa) {
        int x=position.getX()+rng.nextInt(halfSizeX*2+1)-halfSizeX;
        int y=position.getY(); int z=position.getZ()+rng.nextInt(halfSizeZ*2+1)-halfSizeZ; Pos3D cp(x,y,z);
        if (world->isAirBlock(cp)) {
            int walls=0; for (auto f:FACING_HORIZONTAL) if (isSolidBlock(world->getBlockId(cp.offset(f)))) ++walls;
            if (walls==1) { world->setBlockId(cp,CHEST_ID); rng.nextLong(); break; }
        }
    }
    world->setBlockId(position, MONSTER_SPAWNER_ID);
    static const char* mobs[] = {"Skeleton","Zombie","Zombie","Spider"};
    mobOut = mobs[rng.nextInt(4)];
    return true;
}

int main() {
    constexpr int64_t SEED=-5155870644958840792LL;
    Generator g(lce::CONSOLE::XBOX1, LCEVERSION::ELYTRA, SEED, lce::WORLDSIZE::LARGE, lce::BIOMESCALE::LARGE);
    Biome::registerBiomes();
    Placement::setWorldSize(lce::WORLDSIZE::LARGE);
    Pos2D spawn=g.getSpawnBlock(); Pos2D spawnChunk(spawn.x>>4, spawn.z>>4);
    const int radius=24; // 48x48 chunk search area around spawn
    AreaRange buildRange(spawnChunk-Pos2D(radius+2,radius+2), spawnChunk+Pos2D(radius+2,radius+2));
    AreaRange scanRange(spawnChunk-Pos2D(radius,radius), spawnChunk+Pos2D(radius,radius));
    World world(&g);
    world.createChunks(buildRange);
    world.decorateCaves(buildRange, false);
    world.generateMineshafts(); world.generateVillages(); world.generateStrongholds(); world.generateScatteredFeatures();

    std::vector<DungeonHit> hits;
    for (Pos2D cp: scanRange) {
        Chunk::populateStructures(world, cp);
        ChunkPrimer* ch=world.getChunk(cp); if (!ch || ch->stage!=Stage::STAGE_DECORATE) continue;
        const Generator* gp=world.getGenerator();
        if (Pos3D p=FeaturePositions::waterLake(gp,ch->decorateRng,cp.x,cp.z); !p.isNull()) { WorldGenLakes(gp,lce::BlocksInit::STILL_WATER.getState()).generate(&world,ch->decorateRng,p); }
        if (Pos3D p=FeaturePositions::lavaLake(ch->decorateRng,cp.x,cp.z); !p.isNull()) { WorldGenLakes(gp,lce::BlocksInit::STILL_LAVA.getState()).generate(&world,ch->decorateRng,p); }
        for (int i=0;i<8;++i) {
            Pos3D p=FeaturePositions::dungeon(ch->decorateRng,cp.x,cp.z); std::string mob;
            if (generateDungeon(&world,ch->decorateRng,p,mob)) {
                double dx=p.getX()-spawn.x, dz=p.getZ()-spawn.z;
                hits.push_back({p,mob,std::sqrt(dx*dx+dz*dz)});
            }
        }
        ch->stage=Stage::STAGE_DONE;
    }
    std::sort(hits.begin(),hits.end(),[](const auto&a,const auto&b){return a.dist<b.dist;});
    std::filesystem::create_directories("dungeon_output");
    std::ofstream csv("dungeon_output/dungeons_near_spawn.csv"); csv<<"rank,mob,x,y,z,distance_from_spawn\n";
    for(size_t i=0;i<hits.size();++i) csv<<i+1<<","<<hits[i].mob<<","<<hits[i].pos.getX()<<","<<hits[i].pos.getY()<<","<<hits[i].pos.getZ()<<","<<hits[i].dist<<"\n";
    std::ofstream txt("dungeon_output/closest_dungeons.txt");
    txt<<"Spawn: "<<spawn.x<<", "<<spawn.z<<"\nTotal successful dungeons in scan: "<<hits.size()<<"\n\n";
    if(!hits.empty()) txt<<"Closest dungeon: "<<hits[0].mob<<" at X="<<hits[0].pos.getX()<<" Y="<<hits[0].pos.getY()<<" Z="<<hits[0].pos.getZ()<<" distance="<<hits[0].dist<<"\n";
    auto sk=std::find_if(hits.begin(),hits.end(),[](auto&h){return h.mob=="Skeleton";});
    if(sk!=hits.end()) txt<<"Closest skeleton dungeon: X="<<sk->pos.getX()<<" Y="<<sk->pos.getY()<<" Z="<<sk->pos.getZ()<<" distance="<<sk->dist<<"\n";
    txt<<"\nNearest 20:\n"; for(size_t i=0;i<std::min<size_t>(20,hits.size());++i) txt<<i+1<<". "<<hits[i].mob<<" X="<<hits[i].pos.getX()<<" Y="<<hits[i].pos.getY()<<" Z="<<hits[i].pos.getZ()<<" d="<<hits[i].dist<<"\n";
    std::cout<<txt.rdbuf();
}
