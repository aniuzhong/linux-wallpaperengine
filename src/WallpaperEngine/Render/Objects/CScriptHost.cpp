#include "CScriptHost.h"

using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Render::Objects;

CScriptHost::CScriptHost (Wallpapers::CScene& scene, const Object& object) :
    CObject (scene, object), Scripting::ScriptableObject (scene, object) { }
