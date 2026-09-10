// ForestrySystem.h - forests and fields as living map layers.
//
// Woodland is not bolted to the terrain type: villages cut it back, sawmills cut it back
// faster, and it creeps outwards again from whatever is left standing. Fields spread from
// villages over good soil. Both are stored as density per tile and drawn as sprite masks.
#pragma once

#include "../../Core/Singleton.h"
#include "../../Core/Math.h"

namespace woc
{
    class World;

    class ForestrySystem final : public Singleton<ForestrySystem>
    {
        friend class Singleton<ForestrySystem>;
    public:
        void Tick(World& world);

        /// Pushes the current forest and field layers to the renderer.
        void UploadLayers(World& world);

        bool LayersDirty() const { return m_layersDirty; }

    private:
        ForestrySystem() = default;
        ~ForestrySystem() = default;

        void Harvest(World& world);
        void Regrow(World& world);
        void GrowFields(World& world);

        bool m_layersDirty = true;
    };
}
