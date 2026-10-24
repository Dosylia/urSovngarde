#pragma once

struct Actor;

/**
 * @brief Dispatched whenever an actor has been unloaded from the environment.
 */
struct ActorRemovedEvent
{
    ActorRemovedEvent(const uint32_t aFormId, Actor* apActor)
        : FormId(aFormId)
        , pActor(apActor)
    {
    }

    uint32_t FormId;
    // The removed actor, checked through its handle; null once the game has freed it or given its id to another object.
    // Listeners use it instead of looking the id up, which can find a freed actor or the id's next one.
    Actor* pActor;
};
