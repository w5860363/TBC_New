/* ScriptData
SDName: Boss_Captain_Skarloc
SD%Complete: 75
SDComment: Missing adds, missing waypoints to move up to Thrall once spawned + speech before enter combat.
SDCategory: Caverns of Time, Old Hillsbrad Foothills
EndScriptData */

#include "ScriptPCH.h"
#include "old_hillsbrad.h"

#define SAY_ENTER                   -1560000
#define SAY_TAUNT1                  -1560001
#define SAY_TAUNT2                  -1560002
#define SAY_SLAY1                   -1560003
#define SAY_SLAY2                   -1560004
#define SAY_DEATH                   -1560005

#define SPELL_HOLY_LIGHT            29427
#define SPELL_CLEANSE               29380
#define SPELL_HAMMER_OF_JUSTICE     13005
#define SPELL_HOLY_SHIELD           31904
#define SPELL_DEVOTION_AURA         8258
#define SPELL_CONSECRATION          38385

class boss_captain_skarloc : public CreatureScript
{
public:
    boss_captain_skarloc() : CreatureScript("boss_captain_skarloc") { }

    CreatureAI* GetAI(Creature* pCreature) const
    {
        return new boss_captain_skarlocAI(pCreature);
    }

    struct boss_captain_skarlocAI : public ScriptedAI
    {
        boss_captain_skarlocAI(Creature* pCreature) : ScriptedAI(pCreature)
        {
            pInstance = pCreature->GetInstanceScript();
        }

        InstanceScript* pInstance;

        uint32 Holy_Light_Timer;
        uint32 Cleanse_Timer;
        uint32 HammerOfJustice_Timer;
        uint32 HolyShield_Timer;
        uint32 DevotionAura_Timer;
        uint32 Consecration_Timer;

        void Reset()
        {
            Holy_Light_Timer = 20000 + rand()%10000;
            Cleanse_Timer = 10000;
            HammerOfJustice_Timer = 20000 + rand()%15000;
            HolyShield_Timer = 240000;
            DevotionAura_Timer = 3000;
            Consecration_Timer = 8000;
        }

        void EnterCombat(Unit* /*pWho*/)
        {
            //This is not correct. Should taunt Thrall before engage in combat
            DoScriptText(SAY_TAUNT1, me);
            DoScriptText(SAY_TAUNT2, me);
        }

        void KilledUnit(Unit* /*pVictim*/)
        {
            DoScriptText(RAND(SAY_SLAY1, SAY_SLAY2), me);
        }

        void JustDied(Unit* /*pVictim*/)
        {
            DoScriptText(SAY_DEATH, me);

            if(pInstance && pInstance->GetData(TYPE_THRALL_EVENT) == IN_PROGRESS)
                pInstance->SetData(TYPE_THRALL_PART1, DONE);
        }

        void UpdateAI(const uint32 diff)
        {
            //Return since we have no target
            if(!UpdateVictim())
                return;

            //Holy_Light
            if(Holy_Light_Timer <= diff)
            {
                DoCast(me, SPELL_HOLY_LIGHT);
                Holy_Light_Timer = 30000;
            } else Holy_Light_Timer -= diff;

            //Cleanse
            if(Cleanse_Timer <= diff)
            {
                DoCast(me, SPELL_CLEANSE);
                Cleanse_Timer = 10000;
            } else Cleanse_Timer -= diff;

            //Hammer of Justice
            if(HammerOfJustice_Timer <= diff)
            {
                DoCast(me->GetVictim(), SPELL_HAMMER_OF_JUSTICE);
                HammerOfJustice_Timer = 60000;
            } else HammerOfJustice_Timer -= diff;

            //Holy Shield
            if(HolyShield_Timer <= diff)
            {
                DoCast(me, SPELL_HOLY_SHIELD);
                HolyShield_Timer = 240000;
            } else HolyShield_Timer -= diff;

            //Devotion_Aura
            if(DevotionAura_Timer <= diff)
            {
                DoCast(me, SPELL_DEVOTION_AURA);
                DevotionAura_Timer = 45000 + rand()%10000;
            } else DevotionAura_Timer -= diff;

            //Consecration
            if(Consecration_Timer <= diff)
            {
                //DoCast(me->getVictim(), SPELL_CONSECRATION);
                Consecration_Timer = 5000 + rand()%5000;
            } else Consecration_Timer -= diff;

            DoMeleeAttackIfReady();
        }
    };
};

void AddSC_boss_captain_skarloc()
{
    new boss_captain_skarloc;
}
