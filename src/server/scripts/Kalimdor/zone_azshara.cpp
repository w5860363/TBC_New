#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Player.h"
#include "SpellInfo.h"

/*######
## npc_spitelashes
######*/

enum Spitelashes
{
    SPELL_POLYMORPH_RANK1       = 118,
    SPELL_POLYMORPH_RANK2       = 12824,
    SPELL_POLYMORPH_RANK3       = 12825,
    SPELL_POLYMORPH_RANK4       = 12826,
    SPELL_POLYMORPH             = 29124,
    SPELL_POLYMORPH_BACKFIRE    = 28406,
    SPELL_REMOVE_POLYMORPH      = 6924
};

class npc_spitelashes : public CreatureScript
{
public:
    npc_spitelashes() : CreatureScript("npc_spitelashes") { }

    struct npc_spitelashesAI : public ScriptedAI
    {
        npc_spitelashesAI(Creature* creature) : ScriptedAI(creature) { }

        uint32 morphtimer;
        bool spellhit;

        void Reset()
        {
            morphtimer = 0;
            spellhit = false;
        }

        void EnterCombat(Unit* /*who*/) { }

        void SpellHit(Unit* unit, const SpellInfo* spell)
        {
            if (spellhit)
                return;

            switch (spell->Id)
            {
                case SPELL_POLYMORPH_RANK1:
                case SPELL_POLYMORPH_RANK2:
                case SPELL_POLYMORPH_RANK3:
                case SPELL_POLYMORPH_RANK4:
                    if (Player* player = unit->ToPlayer())
                        if (player->GetQuestStatus(9364) == QUEST_STATUS_INCOMPLETE)
                        {
                            spellhit = true;
                            DoCast(me, SPELL_POLYMORPH);
                        }
                    break;
                default:
                    break;
            }
        }

        void UpdateAI(uint32 diff)
        {
            // we mustn't remove the Creature in the same round in which we cast the summon spell, otherwise there will be no summons
            if (spellhit && morphtimer >= 5000)
            {
                me->DespawnOrUnsummon();
                return;
            }
            // walk 5 seconds before summoning
            if (spellhit && morphtimer<5000)
            {
                morphtimer+=diff;
                if (morphtimer >= 5000)
                {
                    DoCast(me, SPELL_POLYMORPH_BACKFIRE); // summon copies
                    DoCast(me, SPELL_REMOVE_POLYMORPH);   // visual explosion
                }
            }
            if (!UpdateVictim())
                return;

            /// @todo add abilities for the different creatures
            DoMeleeAttackIfReady();
        }
    };

    CreatureAI* GetAI(Creature* creature) const
    {
        return new npc_spitelashesAI(creature);
    }
};

/*######
## npc_loramus_thalipedes
######*/

#define GOSSIP_HELLO_LT1    "Can you help me?"
#define GOSSIP_HELLO_LT2    "Tell me your story"
#define GOSSIP_SELECT_LT1   "Please continue"
#define GOSSIP_SELECT_LT2   "I do not understand"
#define GOSSIP_SELECT_LT3   "Indeed"
#define GOSSIP_SELECT_LT4   "I will do this with or your help, Loramus"
#define GOSSIP_SELECT_LT5   "Yes"

class npc_loramus_thalipedes : public CreatureScript
{
public:
    npc_loramus_thalipedes() : CreatureScript("npc_loramus_thalipedes") { }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action)
    {
        player->PlayerTalkClass->ClearMenus();
        switch (action)
        {
            case GOSSIP_ACTION_INFO_DEF+1:
                CloseGossipMenuFor(player);
                player->AreaExploredOrEventHappens(2744);
                break;

            case GOSSIP_ACTION_INFO_DEF+2:
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_SELECT_LT1, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 21);
                SendGossipMenuFor(player, 1813, creature->GetGUID());
                break;
            case GOSSIP_ACTION_INFO_DEF+21:
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_SELECT_LT2, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 22);
                SendGossipMenuFor(player, 1814, creature->GetGUID());
                break;
            case GOSSIP_ACTION_INFO_DEF+22:
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_SELECT_LT3, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 23);
                SendGossipMenuFor(player, 1815, creature->GetGUID());
                break;
            case GOSSIP_ACTION_INFO_DEF+23:
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_SELECT_LT4, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 24);
                SendGossipMenuFor(player, 1816, creature->GetGUID());
                break;
            case GOSSIP_ACTION_INFO_DEF+24:
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_SELECT_LT5, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 25);
                SendGossipMenuFor(player, 1817, creature->GetGUID());
                break;
            case GOSSIP_ACTION_INFO_DEF+25:
                CloseGossipMenuFor(player);
                player->AreaExploredOrEventHappens(3141);
                break;
        }
        return true;
    }

    bool OnGossipHello(Player* player, Creature* creature)
    {
        if (creature->IsQuestGiver())
            player->PrepareQuestMenu(creature->GetGUID());

        if (player->GetQuestStatus(2744) == QUEST_STATUS_INCOMPLETE)
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_HELLO_LT1, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF+1);

        if (player->GetQuestStatus(3141) == QUEST_STATUS_INCOMPLETE)
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_HELLO_LT2, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF+2);

        SendGossipMenuFor(player, player->GetGossipTextId(creature), creature->GetGUID());

        return true;
    }
};

/*####
# npc_rizzle_sprysprocket
####*/

enum RizzleSprysprocketData
{
    QUEST_CHASING_THE_MOONSTONE     = 10994,

    NPC_DEPTH_CHARGE                = 23025,

    SPELL_RIZZLE_BLACKJACK          = 39865,
    SPELL_RIZZLE_ESCAPE             = 39871,
    SPELL_RIZZLE_FROST_GRENADE      = 40525,
    SPELL_DEPTH_CHARGE_TRAP         = 38576,
    SPELL_PERIODIC_DEPTH_CHARGE     = 39912,
    SPELL_GIVE_SOUTHFURY_MOONSTONE  = 39886,

    SAY_RIZZLE_START                = 0,
    SAY_RIZZLE_GRENADE              = 1,
    SAY_RIZZLE_FINAL                = 2,
    MSG_ESCAPE_NOTICE               = 3
};

#define GOSSIP_GET_MOONSTONE "Hand over the Southfury moonstone and I'll let you go."

Position const WPs[58] =
{
    {3691.97f, -3962.41f, 35.9118f, 3.67f},
    {3675.02f, -3960.49f, 35.9118f, 3.67f},
    {3653.19f, -3958.33f, 33.9118f, 3.59f},
    {3621.12f, -3958.51f, 29.9118f, 3.48f},
    {3604.86f, -3963,     29.9118f, 3.48f},
    {3569.94f, -3970.25f, 29.9118f, 3.44f},
    {3541.03f, -3975.64f, 29.9118f, 3.41f},
    {3510.84f, -3978.71f, 29.9118f, 3.41f},
    {3472.7f,  -3997.07f, 29.9118f, 3.35f},
    {3439.15f, -4014.55f, 29.9118f, 3.29f},
    {3412.8f,  -4025.87f, 29.9118f, 3.25f},
    {3384.95f, -4038.04f, 29.9118f, 3.24f},
    {3346.77f, -4052.93f, 29.9118f, 3.22f},
    {3299.56f, -4071.59f, 29.9118f, 3.20f},
    {3261.22f, -4080.38f, 30.9118f, 3.19f},
    {3220.68f, -4083.09f, 31.9118f, 3.18f},
    {3187.11f, -4070.45f, 33.9118f, 3.16f},
    {3162.78f, -4062.75f, 33.9118f, 3.15f},
    {3136.09f, -4050.32f, 33.9118f, 3.07f},
    {3119.47f, -4044.51f, 36.0363f, 3.07f},
    {3098.95f, -4019.8f,  33.9118f, 3.07f},
    {3073.07f, -4011.42f, 33.9118f, 3.07f},
    {3051.71f, -3993.37f, 33.9118f, 3.02f},
    {3027.52f, -3978.6f,  33.9118f, 3.00f},
    {3003.78f, -3960.14f, 33.9118f, 2.98f},
    {2977.99f, -3941.98f, 31.9118f, 2.96f},
    {2964.57f, -3932.07f, 30.9118f, 2.96f},
    {2947.9f,  -3921.31f, 29.9118f, 2.96f},
    {2924.91f, -3910.8f,  29.9118f, 2.94f},
    {2903.04f, -3896.42f, 29.9118f, 2.93f},
    {2884.75f, -3874.03f, 29.9118f, 2.90f},
    {2868.19f, -3851.48f, 29.9118f, 2.82f},
    {2854.62f, -3819.72f, 29.9118f, 2.80f},
    {2825.53f, -3790.4f,  29.9118f, 2.744f},
    {2804.31f, -3773.05f, 29.9118f, 2.71f},
    {2769.78f, -3763.57f, 29.9118f, 2.70f},
    {2727.23f, -3745.92f, 30.9118f, 2.69f},
    {2680.12f, -3737.49f, 30.9118f, 2.67f},
    {2647.62f, -3739.94f, 30.9118f, 2.66f},
    {2616.6f,  -3745.75f, 30.9118f, 2.64f},
    {2589.38f, -3731.97f, 30.9118f, 2.61f},
    {2562.94f, -3722.35f, 31.9118f, 2.56f},
    {2521.05f, -3716.6f,  31.9118f, 2.55f},
    {2485.26f, -3706.67f, 31.9118f, 2.51f},
    {2458.93f, -3696.67f, 31.9118f, 2.51f},
    {2432,     -3692.03f, 31.9118f, 2.46f},
    {2399.59f, -3681.97f, 31.9118f, 2.45f},
    {2357.75f, -3666.6f,  31.9118f, 2.44f},
    {2311.99f, -3656.88f, 31.9118f, 2.94f},
    {2263.41f, -3649.55f, 31.9118f, 3.02f},
    {2209.05f, -3641.76f, 31.9118f, 2.99f},
    {2164.83f, -3637.64f, 31.9118f, 3.15f},
    {2122.42f,  -3639,    31.9118f, 3.21f},
    {2075.73f, -3643.59f, 31.9118f, 3.22f},
    {2033.59f, -3649.52f, 31.9118f, 3.42f},
    {1985.22f, -3662.99f, 31.9118f, 3.42f},
    {1927.09f, -3679.56f, 33.9118f, 3.42f},
    {1873.57f, -3695.32f, 33.9118f, 3.44f}
};

class npc_rizzle_sprysprocket : public CreatureScript
{
public:
    npc_rizzle_sprysprocket() : CreatureScript("npc_rizzle_sprysprocket") { }

    struct npc_rizzle_sprysprocketAI : public ScriptedAI
    {
        npc_rizzle_sprysprocketAI(Creature* creature) : ScriptedAI(creature) { }

        void Reset()
        {
            SpellEscapeTimer = 1300;
            TeleportTimer = 3500;
            CheckTimer = 10000;
            GrenadeTimer = 30000;
            MustDieTimer = 3000;
            CurrWP = 0;

            PlayerGUID.Clear();

            MustDie = false;
            Escape = false;
            ContinueWP = false;
            Reached = false;
        }

        void EnterCombat(Unit* /*who*/) { }

        void AttackStart(Unit* who)
        {
            if (!who || PlayerGUID)
                return;

            Player* player = who->ToPlayer();

            if (player && player->GetQuestStatus(QUEST_CHASING_THE_MOONSTONE) == QUEST_STATUS_INCOMPLETE)
            {
                PlayerGUID.Set(who->GetGUID());
                Talk(SAY_RIZZLE_START);
                DoCast(who, SPELL_RIZZLE_BLACKJACK, false);
                return;
            }
        }

        void sGossipSelect(Player* player, uint32 /*sender*/, uint32 /*action*/)
        {
            CloseGossipMenuFor(player);
            me->CastSpell(player, SPELL_GIVE_SOUTHFURY_MOONSTONE, true);
            MustDieTimer = 3000;
            MustDie = true;
        }

        void MovementInform(uint32 type, uint32 id)
        {
            if (type != POINT_MOTION_TYPE)
                return;

            if (id == 57)
            {
                me->DespawnOrUnsummon();
                return;
            }

            ++CurrWP;
            ContinueWP = true;
        }

        void UpdateAI(uint32 diff)
        {
            if (MustDie)
            {
                if (MustDieTimer <= diff)
                {
                    me->DespawnOrUnsummon();
                    return;
                } else MustDieTimer -= diff;
            }

            if (!Escape)
            {
                if (!PlayerGUID)
                    return;

                if (SpellEscapeTimer <= diff)
                {
                    DoCast(me, SPELL_RIZZLE_ESCAPE, false);
                    SpellEscapeTimer = 10000;
                } else SpellEscapeTimer -= diff;

                if (TeleportTimer <= diff)
                {
                    // temp solution - unit can't be teleported by core using spelleffect 5, only players
                    me->NearTeleportTo(3706.39f, -3969.15f, 35.9118f, me->GetOrientation());

                    //begin swimming and summon depth charges
                    Player* player = ObjectAccessor::GetPlayer(*me, PlayerGUID);
                    if (!player)
                        return;

                    Talk(MSG_ESCAPE_NOTICE, player);
                    DoCast(me, SPELL_PERIODIC_DEPTH_CHARGE);
                    me->SetHover(true);
                    me->SetSwim(true);
                    me->SetSpeedRate(MOVE_RUN, 0.85f, true);
                    //me->GetMotionMaster()->MovementExpired();
                    me->GetMotionMaster()->MovePoint(CurrWP, WPs[CurrWP]);
                    Escape = true;
                } else TeleportTimer -= diff;

                return;
            }

            if (ContinueWP)
            {
                me->GetMotionMaster()->MovePoint(CurrWP, WPs[CurrWP]);
                ContinueWP = false;
            }

            if (GrenadeTimer <= diff)
            {
                if (Player* player = ObjectAccessor::GetPlayer(*me, PlayerGUID))
                {
                   Talk(SAY_RIZZLE_GRENADE, player);
                   DoCast(player, SPELL_RIZZLE_FROST_GRENADE, true);
                }
                GrenadeTimer = 30000;
            } else GrenadeTimer -= diff;

            if (CheckTimer <= diff)
            {
                Player* player = ObjectAccessor::GetPlayer(*me, PlayerGUID);
                if (!player)
                {
                    me->DespawnOrUnsummon();
                    return;
                }

                if (me->IsWithinDist(player, 10) && me->GetPositionX() > player->GetPositionX() && !Reached)
                {
                    Talk(SAY_RIZZLE_FINAL);
                    me->SetUInt32Value(UNIT_NPC_FLAGS, 1);
                    me->SetFaction(35);
                    me->GetMotionMaster()->MoveIdle();
                    me->RemoveAurasDueToSpell(SPELL_PERIODIC_DEPTH_CHARGE);
                    Reached = true;
                }

                CheckTimer = 1000;
            } else CheckTimer -= diff;
        }

    private:
        ObjectGuid PlayerGUID;
        uint32 SpellEscapeTimer;
        uint32 TeleportTimer;
        uint32 CheckTimer;
        uint32 GrenadeTimer;
        uint32 MustDieTimer;
        uint32 CurrWP;
        bool MustDie;
        bool Escape;
        bool ContinueWP;
        bool Reached;
    };

    bool OnGossipHello(Player* player, Creature* creature)
    {
        if (player->GetQuestStatus(QUEST_CHASING_THE_MOONSTONE) != QUEST_STATUS_INCOMPLETE)
            return true;
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_GET_MOONSTONE, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
        SendGossipMenuFor(player, 10811, creature->GetGUID());
        return true;
    }

    CreatureAI* GetAI(Creature* creature) const
    {
        return new npc_rizzle_sprysprocketAI(creature);
    }
};

/*####
# npc_depth_charge
####*/
class npc_depth_charge : public CreatureScript
{
public:
    npc_depth_charge() : CreatureScript("npc_depth_charge") { }

    struct npc_depth_chargeAI : public ScriptedAI
    {
        npc_depth_chargeAI(Creature* creature) : ScriptedAI(creature) { }

        bool WeMustDie;
        uint32 WeMustDieTimer;

        void Reset()
        {
            me->SetHover(true);
            me->SetSwim(true);
            me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
            WeMustDie = false;
            WeMustDieTimer = 1000;
        }

        void EnterCombat(Unit* /*who*/) { }

        void AttackStart(Unit* /*who*/) { }

        void MoveInLineOfSight(Unit* who)
        {
            if (!who)
                return;

            if (who->GetTypeId() == TYPEID_PLAYER && me->IsWithinDistInMap(who, 5))
            {
                DoCast(who, SPELL_DEPTH_CHARGE_TRAP);
                WeMustDie = true;
                return;
            }
        }

        void UpdateAI(uint32 diff)
        {
            if (WeMustDie)
            {
                if (WeMustDieTimer <= diff)
                    me->DespawnOrUnsummon();
                else
                    WeMustDieTimer -= diff;
            }
            return;
        }
    };

    CreatureAI* GetAI(Creature* creature) const
    {
        return new npc_depth_chargeAI(creature);
    }
};

void AddSC_azshara()
{
    new npc_spitelashes();
    new npc_loramus_thalipedes();
    new npc_rizzle_sprysprocket();
    new npc_depth_charge();
}