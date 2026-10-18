#include <World.h>
#include <Components.h>

#include <Services/CharacterService.h>
#include <Services/ObjectService.h>
#include <Services/QuestService.h>
#include <Services/ServerListService.h>
#include <Services/PublicStatusService.h>
#include <Services/ActorValueService.h>
#include <Services/AdminService.h>
#include <Services/InventoryService.h>
#include <Services/DroppedItemService.h>
#include <Services/MagicService.h>
#include <Services/OverlayService.h>
#include <Services/CommandService.h>
#include <Services/StringCacheService.h>
#include <Services/CombatService.h>
#include <Services/WeatherService.h>
#include <Services/ScriptService.h>
#include <Services/MapService.h>
#include <Services/VoiceService.h>
#include <Services/WhereaboutsService.h>
#include <Services/ReviveService.h>

#include <es_loader/ESLoader.h>

World::World()
{
    // Reserves server id 0 for an empty entity, so real entities start at 1.
    // A player character at id 0 breaks the defender's rule for other players' hits.
    [[maybe_unused]] const entt::entity cReservedZero = create();
    assert(ToInteger(cReservedZero) == 0);

    m_spAdminService = std::make_shared<AdminService>(*this, m_dispatcher);
    spdlog::default_logger()->sinks().push_back(std::static_pointer_cast<spdlog::sinks::sink>(m_spAdminService));

    ctx().emplace<CharacterService>(*this, m_dispatcher);
    ctx().emplace<PlayerService>(*this, m_dispatcher);
    ctx().emplace<CalendarService>(*this, m_dispatcher);
    ctx().emplace<ObjectService>(*this, m_dispatcher);
    ctx().emplace<ModsComponent>();
    ctx().emplace<ServerListService>(*this, m_dispatcher);
    ctx().emplace<PublicStatusService>(*this, m_dispatcher);
    ctx().emplace<QuestService>(*this, m_dispatcher);
    ctx().emplace<PartyService>(*this, m_dispatcher);
    ctx().emplace<ActorValueService>(*this, m_dispatcher);
    ctx().emplace<InventoryService>(*this, m_dispatcher);
    ctx().emplace<DroppedItemService>(*this, m_dispatcher);
    ctx().emplace<MagicService>(*this, m_dispatcher);
    ctx().emplace<OverlayService>(*this, m_dispatcher);
    ctx().emplace<CommandService>(*this, m_dispatcher);
    ctx().emplace<StringCacheService>(*this, m_dispatcher);
    ctx().emplace<CombatService>(*this, m_dispatcher);
    ctx().emplace<WeatherService>(*this, m_dispatcher);
    ctx().emplace<MapService>(*this, m_dispatcher);
    ctx().emplace<VoiceService>(*this, m_dispatcher);
    ctx().emplace<WhereaboutsService>(*this, m_dispatcher);
    ctx().emplace<ReviveService>(*this, m_dispatcher);

    ESLoader::ESLoader loader;
    // emplace loaded mods into modscomponent.
    m_recordCollection = loader.BuildRecordCollection();
    for (const auto& it : loader.GetLoadOrder())
    {
        ctx().emplace<ModsComponent>().AddServerMod(it);
    }

    // late initialize the ScriptService to ensure all components are valid
    m_pScriptService = TiltedPhoques::MakeUnique<ScriptService>(*this, m_dispatcher);
}

World::~World()
{
    m_pScriptService.reset();
}
