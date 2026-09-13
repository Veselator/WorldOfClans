// BanditSystem.h - the robbers, their camps, and what they do to everybody else.
//
// Outlaws are deliberately not a faction the diplomacy screen knows about. They hold no
// land and project none; they are simply there, in the parts of the map nobody rides
// through, and everything they do is done to somebody else's country. A camp sends out
// bands, the bands plunder whatever village is nearest - a lord's or nobody's, it makes no
// difference to them - and what they take goes back to the camp and is buried under it.
// Burn the camp and you get the lot.
//
// Because they are ordinary cohorts of an ordinary clan, they fight, retreat, take losses
// and starve exactly as anybody else does; the only thing this system adds is what they
// want. That also means two different bands meeting in the same forest fight each other,
// which is the correct behaviour and cost nothing to arrange.
#pragma once

#include "../../Core/Random.h"
#include "../../Core/Singleton.h"
#include "../World/ResourceData.h"

#include <string>
#include <vector>

namespace woc
{
    class World;
    struct BanditCamp;

    /// What the party screen decides about them.
    struct BanditSettings
    {
        bool enabled = false;
        /// 0..1, and it means what it says: how thick the country is with robbers. It sets
        /// how many camps are put down and how quickly each one musters another band.
        f32 density = 0.5f;

        Json ToJson() const;
        static BanditSettings FromJson(const Json& node);
    };

    class BanditSystem final : public Singleton<BanditSystem>
    {
        friend class Singleton<BanditSystem>;
    public:
        /// Puts camps on the map and raises the outlaw clans that keep them. Called once,
        /// when a party starts - the camps are drawn fresh every time rather than stored
        /// with the map, so the same world is never robbed in the same places twice.
        void Seed(World& world, const BanditSettings& settings, Random& random);
        void Reset();

        /// Musters bands, sends them out, brings them home and settles assaults on camps.
        void Tick(World& world, f32 days);

        const BanditSettings& Settings() const { return m_settings; }
        void SetSettings(const BanditSettings& settings) { m_settings = settings; }

        /// True while this clan is a band of outlaws rather than a house.
        static bool IsOutlaw(const World& world, EntityId clanId);

        /// What a camp is worth to whoever burns it: its hoard, and what the tents are made
        /// of. Shown on the panel before the order is given.
        static ResourceData Spoils(const BanditCamp& camp);

        Json ToJson() const;
        void FromJson(const Json& node);

    private:
        BanditSystem() = default;
        ~BanditSystem() = default;

        void MusterBands(World& world, f32 days);
        void DirectBands(World& world);
        void ResolveAssaults(World& world, f32 days);
        /// A band standing at its own camp takes on men and mends.
        void Replenish(World& world, f32 days);

        /// Raises one band out of a camp: a cohort of that camp's people, under its clan.
        EntityId RaiseBand(World& world, BanditCamp& camp, Random& random);
        /// How many bands this camp has out at the moment.
        i32 BandsOf(const World& world, EntityId campId) const;
        /// Somewhere nobody would look: passable, away from every seat and every other camp.
        bool FindCampSite(World& world, Random& random, Vec2& out) const;

        BanditSettings m_settings;
        Random m_random;
        /// Days owed to the daily pass, so the robbers keep the calendar's time and not
        /// the frame rate's.
        f32 m_carry = 0.0f;
    };
}
