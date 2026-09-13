#include "SceneFactory.h"
#include "SceneManager.h"

#include "EditorScene.h"
#include "GameScene.h"
#include "MainMenuScene.h"
#include "PartySetupScene.h"

namespace woc
{
    Scope<IScene> SceneFactory::Create(SceneId id)
    {
        switch (id)
        {
        case SceneId::MainMenu:   return MakeScope<MainMenuScene>();
        case SceneId::PartySetup: return MakeScope<PartySetupScene>();
        case SceneId::Editor:     return MakeScope<EditorScene>();
        case SceneId::Game:       return MakeScope<GameScene>();
        }
        return nullptr;
    }

    void SceneFactory::RegisterAll(SceneManager& manager)
    {
        for (SceneId id : { SceneId::MainMenu, SceneId::PartySetup,
                            SceneId::Editor, SceneId::Game })
        {
            manager.Register(id, [id]() { return Create(id); });
        }
    }
}
