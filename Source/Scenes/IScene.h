// IScene.h - the contract every screen implements.
#pragma once

#include "../Core/Types.h"

namespace woc
{
    enum class SceneId
    {
        MainMenu,
        PartySetup,
        Settings,
        Editor,
        Game
    };

    class IScene
    {
    public:
        virtual ~IScene() = default;

        virtual SceneId Id() const = 0;
        virtual const char* Name() const = 0;

        virtual void OnEnter() {}
        virtual void OnExit() {}

        /// Gameplay and input; `deltaTime` is real seconds.
        virtual void Update(f32 deltaTime) = 0;
        /// Submits draw calls for this frame.
        virtual void Render() = 0;
    };
}
