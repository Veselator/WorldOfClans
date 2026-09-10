// SceneFactory.h - registers a creator for every screen.
//
// The scene manager only knows how to ask for a SceneId; this factory is the single
// place that decides which concrete class answers.
#pragma once

#include "IScene.h"

namespace woc
{
    class SceneManager;

    class SceneFactory
    {
    public:
        static void RegisterAll(SceneManager& manager);
        static Scope<IScene> Create(SceneId id);
    };
}
