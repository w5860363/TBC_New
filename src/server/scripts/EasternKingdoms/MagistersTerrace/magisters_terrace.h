
#ifndef DEF_MAGISTERS_TERRACE_H
#define DEF_MAGISTERS_TERRACE_H

#include "CreatureAIImpl.h"

#define MGTScriptName "instance_magisters_terrace"
#define DataHeader "MT"

uint32 const EncounterCount = 4;

enum MTData
{
    DATA_SELIN_EVENT            = 0,
    DATA_VEXALLUS_EVENT            = 1,
    DATA_DELRISSA_EVENT            = 2,
    DATA_KAELTHAS_EVENT            = 3,
    MAX_ENCOUNTER                = 4
};


enum MTCreatures
{
    NPC_DELRISSA                = 24560,
    NPC_FEL_CRYSTAL                = 24722,
    NPC_KAEL_THAS                = 24664,
    NPC_PHOENIX                    = 21362,
    NPC_PHOENIX_EGG                = 21364
};

enum MTGameObjects
{
    GO_VEXALLUS_DOOR            = 187896,
    GO_SELIN_DOOR                = 187979,
    GO_SELIN_ENCOUNTER_DOOR        = 188065,
    GO_DELRISSA_DOOR            = 187770,
    GO_KAEL_DOOR                = 188064,
    GO_ESCAPE_ORB                = 188173
};


template <class AI, class T>
inline AI* GetMagistersTerraceAI(T* obj)
{
    return GetInstanceAI<AI>(obj, MGTScriptName);
}

#define RegisterMagistersTerraceCreatureAI(ai_name) RegisterCreatureAIWithFactory(ai_name, GetMagistersTerraceAI)

#endif
