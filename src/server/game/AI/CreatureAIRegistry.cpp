
#include "CreatureAIRegistry.h"
#include "NullCreatureAI.h"
#include "AOEAI.h"
#include "GuardAI.h"
#include "PetAI.h"
#include "TotemAI.h"
#include "SmartAI.h"
#include "GameObjectAI.h"
#include "CombatAI.h"
#include "ReactorAI.h"
#include "OutdoorPvPObjectiveAI.h"
#include "RandomMovementGenerator.h"
#include "CreatureAIImpl.h"
#include "MovementGeneratorImpl.h"
#include "MapManager.h"
#include "WaypointMovementGenerator.h"

namespace AIRegistry
{
    void Initialize()
    {
        (new CreatureAIFactory<NullCreatureAI>("NullCreatureAI"))->RegisterSelf();
        (new CreatureAIFactory<AggressorAI>("AggressorAI"))->RegisterSelf();
        (new CreatureAIFactory<ReactorAI>("ReactorAI"))->RegisterSelf();
        (new CreatureAIFactory<PassiveAI>("PassiveAI"))->RegisterSelf();
        (new CreatureAIFactory<CritterAI>("CritterAI"))->RegisterSelf();
        (new CreatureAIFactory<GuardAI>("GuardAI"))->RegisterSelf();
        (new CreatureAIFactory<PetAI>("PetAI"))->RegisterSelf();
        (new CreatureAIFactory<TotemAI>("TotemAI"))->RegisterSelf();
        (new CreatureAIFactory<OutdoorPvPObjectiveAI>("OutdoorPvPObjectiveAI"))->RegisterSelf();
        (new CreatureAIFactory<PossessedAI>("PossessedAI"))->RegisterSelf();
        (new CreatureAIFactory<AOEAI>("AOEAI"))->RegisterSelf();
        (new CreatureAIFactory<SmartAI>(SMARTAI_AI_NAME))->RegisterSelf();
        
        (new GameObjectAIFactory<GameObjectAI>("GameObjectAI"))->RegisterSelf();
        (new GameObjectAIFactory<SmartGameObjectAI>(SMARTAI_GOBJECT_AI_NAME))->RegisterSelf();

        (new MovementGeneratorFactory<RandomMovementGenerator<Creature> >(RANDOM_MOTION_TYPE))->RegisterSelf();
        (new MovementGeneratorFactory<WaypointMovementGenerator<Creature> >(WAYPOINT_MOTION_TYPE))->RegisterSelf();
    }
} // namespace AIRegistry
