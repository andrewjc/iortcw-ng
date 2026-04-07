/*
===========================================================================

Return to Castle Wolfenstein single player GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.

This file is part of the Return to Castle Wolfenstein single player GPL Source Code (RTCW SP Source Code).

RTCW SP Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

RTCW SP Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with RTCW SP Source Code.  If not, see <http://www.gnu.org/licenses/>.

===========================================================================
*/

//===========================================================================
//
// Name:			ai_cast_planner.c
// Function:		Utility-based AI Action Planner
// Programmer:		AI Uplift
// Tab Size:		4 (real tabs)
//===========================================================================

#include "g_local.h"
#include "../qcommon/q_shared.h"
#include "../botlib/botlib.h"
#include "../botlib/be_aas.h"
#include "../botlib/be_ea.h"
#include "../botlib/be_ai_gen.h"
#include "../botlib/be_ai_goal.h"
#include "../botlib/be_ai_move.h"
#include "../botlib/botai.h"

#include "ai_cast.h"
#include "ai_cast_planner.h"
#include "ai_cast_cover.h"
#include "ai_cast_comm.h"
#include "ai_cast_squad.h"
#include "ai_cast_tactics.h"

vmCvar_t ai_planner;

//
// Forward declarations for score/precondition/execute functions
//
static qboolean Precond_Attack( cast_state_t *cs );
static float Score_Attack( cast_state_t *cs );
static qboolean Execute_Attack( cast_state_t *cs );

static qboolean Precond_TakeCover( cast_state_t *cs );
static float Score_TakeCover( cast_state_t *cs );
static qboolean Execute_TakeCover( cast_state_t *cs );

static qboolean Precond_Advance( cast_state_t *cs );
static float Score_Advance( cast_state_t *cs );
static qboolean Execute_Advance( cast_state_t *cs );

static qboolean Precond_Flank( cast_state_t *cs );
static float Score_Flank( cast_state_t *cs );
static qboolean Execute_Flank( cast_state_t *cs );

static qboolean Precond_ThrowGrenade( cast_state_t *cs );
static float Score_ThrowGrenade( cast_state_t *cs );
static qboolean Execute_ThrowGrenade( cast_state_t *cs );

static qboolean Precond_Reload( cast_state_t *cs );
static float Score_Reload( cast_state_t *cs );
static qboolean Execute_Reload( cast_state_t *cs );

static qboolean Precond_CallForHelp( cast_state_t *cs );
static float Score_CallForHelp( cast_state_t *cs );
static qboolean Execute_CallForHelp( cast_state_t *cs );

static qboolean Precond_WaitInAmbush( cast_state_t *cs );
static float Score_WaitInAmbush( cast_state_t *cs );
static qboolean Execute_WaitInAmbush( cast_state_t *cs );

static qboolean Precond_InspectSound( cast_state_t *cs );
static float Score_InspectSound( cast_state_t *cs );
static qboolean Execute_InspectSound( cast_state_t *cs );

static qboolean Precond_Retreat( cast_state_t *cs );
static float Score_Retreat( cast_state_t *cs );
static qboolean Execute_Retreat( cast_state_t *cs );

static qboolean Precond_Distract( cast_state_t *cs );
static float Score_Distract( cast_state_t *cs );
static qboolean Execute_Distract( cast_state_t *cs );

static qboolean Precond_Suppress( cast_state_t *cs );
static float Score_Suppress( cast_state_t *cs );
static qboolean Execute_Suppress( cast_state_t *cs );

static qboolean Precond_PeekAndFire( cast_state_t *cs );
static float Score_PeekAndFire( cast_state_t *cs );
static qboolean Execute_PeekAndFire( cast_state_t *cs );

//
// Action registry
//
static ai_action_t aiActions[AI_ACTION_MAX] = {
	{ AI_ACTION_ATTACK,         "Attack",       Precond_Attack,         Score_Attack,       Execute_Attack },
	{ AI_ACTION_TAKE_COVER,     "TakeCover",    Precond_TakeCover,      Score_TakeCover,    Execute_TakeCover },
	{ AI_ACTION_ADVANCE,        "Advance",      Precond_Advance,        Score_Advance,      Execute_Advance },
	{ AI_ACTION_FLANK,          "Flank",        Precond_Flank,          Score_Flank,        Execute_Flank },
	{ AI_ACTION_THROW_GRENADE,  "ThrowGrenade", Precond_ThrowGrenade,   Score_ThrowGrenade, Execute_ThrowGrenade },
	{ AI_ACTION_RELOAD,         "Reload",       Precond_Reload,         Score_Reload,       Execute_Reload },
	{ AI_ACTION_CALL_FOR_HELP,  "CallForHelp",  Precond_CallForHelp,    Score_CallForHelp,  Execute_CallForHelp },
	{ AI_ACTION_WAIT_IN_AMBUSH, "WaitInAmbush", Precond_WaitInAmbush,   Score_WaitInAmbush, Execute_WaitInAmbush },
	{ AI_ACTION_INSPECT_SOUND,  "InspectSound", Precond_InspectSound,   Score_InspectSound, Execute_InspectSound },
	{ AI_ACTION_RETREAT,        "Retreat",      Precond_Retreat,        Score_Retreat,      Execute_Retreat },
	{ AI_ACTION_DISTRACT,       "Distract",     Precond_Distract,       Score_Distract,     Execute_Distract },
	{ AI_ACTION_SUPPRESS,       "Suppress",     Precond_Suppress,       Score_Suppress,     Execute_Suppress },
	{ AI_ACTION_PEEK_AND_FIRE,  "PeekAndFire",  Precond_PeekAndFire,    Score_PeekAndFire,  Execute_PeekAndFire },
};

//
// Helper: compute health ratio 0.0 - 1.0
//
static float HealthRatio( cast_state_t *cs ) {
	gentity_t *ent = &g_entities[cs->entityNum];
	float maxHealth = cs->attributes[STARTING_HEALTH];
	if ( maxHealth <= 0 ) {
		maxHealth = 100;
	}
	return (float)ent->health / maxHealth;
}

//
// Helper: does the agent have a visible enemy?
//
static qboolean HasVisibleEnemy( cast_state_t *cs ) {
	return ( cs->enemyNum >= 0 && cs->vislist[cs->enemyNum].visible_timestamp >= level.time - 3000 );
}

//
// Helper: get distance to current enemy
//
static float EnemyDistance( cast_state_t *cs ) {
	gentity_t *ent;
	if ( cs->enemyNum < 0 ) {
		return 99999.0f;
	}
	ent = &g_entities[cs->enemyNum];
	return Distance( g_entities[cs->entityNum].r.currentOrigin, ent->r.currentOrigin );
}

//
// Helper: count nearby teammates
//
static int NearbyTeammates( cast_state_t *cs, float range ) {
	int i, count = 0;
	for ( i = 0; i < level.maxclients; i++ ) {
		cast_state_t *other;
		if ( i == cs->entityNum ) {
			continue;
		}
		if ( !g_entities[i].inuse || g_entities[i].health <= 0 ) {
			continue;
		}
		other = AICast_GetCastState( i );
		if ( !other || !other->bs ) {
			continue;
		}
		if ( !AICast_SameTeam( cs, i ) ) {
			continue;
		}
		if ( Distance( g_entities[cs->entityNum].r.currentOrigin, g_entities[i].r.currentOrigin ) < range ) {
			count++;
		}
	}
	return count;
}

//
// Helper: has ammo for any weapon
//
static qboolean HasAmmo( cast_state_t *cs ) {
	return AICast_GotEnoughAmmoForWeapon( cs, cs->weaponNum );
}

//===========================================================================
// ACTION: Attack
//===========================================================================
static qboolean Precond_Attack( cast_state_t *cs ) {
	if ( cs->enemyNum < 0 ) {
		return qfalse;
	}
	if ( !HasAmmo( cs ) ) {
		return qfalse;
	}
	return qtrue;
}

static float Score_Attack( cast_state_t *cs ) {
	float score = 0.5f;
	float dist = EnemyDistance( cs );
	float aggression = cs->attributes[AGGRESSION];
	float tactical = cs->attributes[TACTICAL];

	// visible enemy boosts attack score significantly
	if ( HasVisibleEnemy( cs ) ) {
		score += 0.3f;
	}

	// closer enemies are higher priority to attack
	if ( dist < 256.0f ) {
		score += 0.2f;
	} else if ( dist < 512.0f ) {
		score += 0.1f;
	}

	// aggression makes attack more likely
	score += aggression * 0.2f;

	// morale affects willingness to fight
	score += cs->morale * 0.1f;

	// if health is low and tactical is high, prefer cover over attack
	if ( HealthRatio( cs ) < 0.3f && tactical > 0.5f ) {
		score -= 0.3f;
	}

	if ( score < 0.0f ) {
		score = 0.0f;
	}
	return score;
}

static qboolean Execute_Attack( cast_state_t *cs ) {
	AIFunc_BattleStart( cs );
	return qtrue;
}

//===========================================================================
// ACTION: Take Cover
//===========================================================================
static qboolean Precond_TakeCover( cast_state_t *cs ) {
	if ( cs->enemyNum < 0 ) {
		return qfalse;
	}
	return qtrue;
}

static float Score_TakeCover( cast_state_t *cs ) {
	float score = 0.2f;
	float healthRatio = HealthRatio( cs );
	float tactical = cs->attributes[TACTICAL];

	// low health strongly favors taking cover
	if ( healthRatio < 0.3f ) {
		score += 0.5f;
	} else if ( healthRatio < 0.6f ) {
		score += 0.2f;
	}

	// high tactical attribute means smarter about cover
	score += tactical * 0.3f;

	// if suppressed (bullet impacts nearby), strongly favor cover
	if ( cs->bulletImpactTime > level.time - 1000 ) {
		score += 0.4f;
	}

	// low morale -> more inclined to take cover
	score += ( 1.0f - cs->morale ) * 0.2f;

	// already in cover? less need to re-cover
	if ( cs->aiFlags & AIFL_IN_COVER ) {
		score -= 0.3f;
	}

	if ( score < 0.0f ) {
		score = 0.0f;
	}
	return score;
}

static qboolean Execute_TakeCover( cast_state_t *cs ) {
	AIFunc_BattleTakeCoverStart( cs );
	return qtrue;
}

//===========================================================================
// ACTION: Advance
//===========================================================================
static qboolean Precond_Advance( cast_state_t *cs ) {
	if ( cs->enemyNum < 0 ) {
		return qfalse;
	}
	if ( !HasAmmo( cs ) ) {
		return qfalse;
	}
	return qtrue;
}

static float Score_Advance( cast_state_t *cs ) {
	float score = 0.1f;
	float dist = EnemyDistance( cs );
	float aggression = cs->attributes[AGGRESSION];

	// want to advance when enemy is far and we're healthy
	if ( dist > 512.0f ) {
		score += 0.2f;
	}
	if ( dist > 1024.0f ) {
		score += 0.1f;
	}

	score += aggression * 0.3f;
	score += cs->morale * 0.2f;

	// if health is low, don't advance
	if ( HealthRatio( cs ) < 0.4f ) {
		score -= 0.3f;
	}

	// if teammates nearby, more likely to advance (strength in numbers)
	if ( NearbyTeammates( cs, 512.0f ) >= 2 ) {
		score += 0.15f;
	}

	if ( score < 0.0f ) {
		score = 0.0f;
	}
	return score;
}

static qboolean Execute_Advance( cast_state_t *cs ) {
	AIFunc_BattleChaseStart( cs );
	return qtrue;
}

//===========================================================================
// ACTION: Flank
//===========================================================================
static qboolean Precond_Flank( cast_state_t *cs ) {
	if ( cs->enemyNum < 0 ) {
		return qfalse;
	}
	// need to have a role assignment or high tactical
	if ( cs->squadRole != ROLE_FLANK && cs->attributes[TACTICAL] < 0.6f ) {
		return qfalse;
	}
	if ( !HasAmmo( cs ) ) {
		return qfalse;
	}
	return qtrue;
}

static float Score_Flank( cast_state_t *cs ) {
	float score = 0.0f;
	float tactical = cs->attributes[TACTICAL];

	// role assignment is the primary driver
	if ( cs->squadRole == ROLE_FLANK ) {
		score += 0.7f;
	}

	score += tactical * 0.3f;
	score += cs->morale * 0.1f;

	// need teammates to pin while we flank
	if ( NearbyTeammates( cs, 768.0f ) < 1 ) {
		score -= 0.3f;
	}

	if ( score < 0.0f ) {
		score = 0.0f;
	}
	return score;
}

static qboolean Execute_Flank( cast_state_t *cs ) {
	// use BattleAmbush as base flanking behavior (navigate to alternate position)
	AIFunc_BattleAmbushStart( cs );
	AICast_Comm_BroadcastMessage( cs, COMM_GOING_FLANK, g_entities[cs->entityNum].r.currentOrigin, cs->enemyNum );
	return qtrue;
}

//===========================================================================
// ACTION: Throw Grenade
//===========================================================================
static qboolean Precond_ThrowGrenade( cast_state_t *cs ) {
	gentity_t *ent = &g_entities[cs->entityNum];
	if ( cs->enemyNum < 0 ) {
		return qfalse;
	}
	// must have grenade weapon and ammo
	if ( !COM_BitCheck( ent->client->ps.weapons, WP_GRENADE_LAUNCHER ) ) {
		return qfalse;
	}
	if ( !AICast_GotEnoughAmmoForWeapon( cs, WP_GRENADE_LAUNCHER ) ) {
		return qfalse;
	}
	return qtrue;
}

static float Score_ThrowGrenade( cast_state_t *cs ) {
	float score = 0.1f;
	float dist = EnemyDistance( cs );
	float tactical = cs->attributes[TACTICAL];

	// grenades are best at medium range
	if ( dist > 256.0f && dist < 768.0f ) {
		score += 0.3f;
	}

	// if enemy is in cover, grenade can flush them
	if ( !HasVisibleEnemy( cs ) && cs->enemyNum >= 0 ) {
		score += 0.3f;
	}

	score += tactical * 0.2f;

	// cooldown: don't spam grenades
	if ( cs->grenadeFlushEndTime > level.time - 5000 ) {
		score -= 0.5f;
	}

	if ( score < 0.0f ) {
		score = 0.0f;
	}
	return score;
}

static qboolean Execute_ThrowGrenade( cast_state_t *cs ) {
	AIFunc_GrenadeFlushStart( cs );
	return qtrue;
}

//===========================================================================
// ACTION: Reload
//===========================================================================
static qboolean Precond_Reload( cast_state_t *cs ) {
	if ( cs->aiFlags & AIFL_NO_RELOAD ) {
		return qfalse;
	}
	if ( !HasAmmo( cs ) ) {
		return qtrue;   // no ammo, need to reload if possible
	}
	return qfalse;
}

static float Score_Reload( cast_state_t *cs ) {
	float score = 0.6f;    // reload is fairly urgent when needed

	// if in cover, safe to reload
	if ( cs->aiFlags & AIFL_IN_COVER ) {
		score += 0.2f;
	}

	// if enemy is very close, maybe don't reload
	if ( EnemyDistance( cs ) < 128.0f ) {
		score -= 0.3f;
	}

	if ( score < 0.0f ) {
		score = 0.0f;
	}
	return score;
}

static qboolean Execute_Reload( cast_state_t *cs ) {
	AICast_IdleReload( cs );
	return qtrue;
}

//===========================================================================
// ACTION: Call For Help
//===========================================================================
static qboolean Precond_CallForHelp( cast_state_t *cs ) {
	if ( cs->enemyNum < 0 ) {
		return qfalse;
	}
	// only call for help if teammates are within comm range
	if ( NearbyTeammates( cs, COMM_RANGE ) < 1 ) {
		return qfalse;
	}
	return qtrue;
}

static float Score_CallForHelp( cast_state_t *cs ) {
	float score = 0.1f;
	float healthRatio = HealthRatio( cs );

	// more likely to call for help when injured
	if ( healthRatio < 0.3f ) {
		score += 0.4f;
	} else if ( healthRatio < 0.6f ) {
		score += 0.2f;
	}

	// outnumbered? call for help
	if ( cs->numEnemies > 1 ) {
		score += 0.3f;
	}

	// low morale -> call for help
	score += ( 1.0f - cs->morale ) * 0.2f;

	if ( score < 0.0f ) {
		score = 0.0f;
	}
	return score;
}

static qboolean Execute_CallForHelp( cast_state_t *cs ) {
	AICast_Comm_BroadcastMessage( cs, COMM_NEED_HELP, g_entities[cs->entityNum].r.currentOrigin, cs->enemyNum );
	return qtrue;
}

//===========================================================================
// ACTION: Wait In Ambush
//===========================================================================
static qboolean Precond_WaitInAmbush( cast_state_t *cs ) {
	// only ambush if not currently fighting visible enemy
	if ( HasVisibleEnemy( cs ) ) {
		return qfalse;
	}
	// must have been in combat recently but lost sight
	if ( cs->enemyNum < 0 ) {
		return qfalse;
	}
	return qtrue;
}

static float Score_WaitInAmbush( cast_state_t *cs ) {
	float score = 0.2f;
	float tactical = cs->attributes[TACTICAL];
	float camper = cs->attributes[CAMPER];

	score += tactical * 0.3f;
	score += camper * 0.4f;

	// if we're already in a good spot (in cover), ambush is very appealing
	if ( cs->aiFlags & AIFL_IN_COVER ) {
		score += 0.2f;
	}

	if ( score < 0.0f ) {
		score = 0.0f;
	}
	return score;
}

static qboolean Execute_WaitInAmbush( cast_state_t *cs ) {
	AIFunc_BattleAmbushStart( cs );
	return qtrue;
}

//===========================================================================
// ACTION: Inspect Sound
//===========================================================================
static qboolean Precond_InspectSound( cast_state_t *cs ) {
	if ( cs->audibleEventTime < level.time - 5000 ) {
		return qfalse;
	}
	// don't inspect if we have a visible enemy
	if ( HasVisibleEnemy( cs ) ) {
		return qfalse;
	}
	return qtrue;
}

static float Score_InspectSound( cast_state_t *cs ) {
	float score = 0.3f;
	float alertness = cs->attributes[ALERTNESS];

	score += alertness * 0.3f;

	// if we heard something very recently, it's more urgent
	if ( cs->audibleEventTime > level.time - 1000 ) {
		score += 0.2f;
	}

	if ( score < 0.0f ) {
		score = 0.0f;
	}
	return score;
}

static qboolean Execute_InspectSound( cast_state_t *cs ) {
	AIFunc_InspectAudibleEventStart( cs, cs->audibleEventEnt );
	return qtrue;
}

//===========================================================================
// ACTION: Retreat
//===========================================================================
static qboolean Precond_Retreat( cast_state_t *cs ) {
	// only retreat if in combat and health is critical
	if ( cs->enemyNum < 0 ) {
		return qfalse;
	}
	if ( HealthRatio( cs ) > 0.4f ) {
		return qfalse;
	}
	return qtrue;
}

static float Score_Retreat( cast_state_t *cs ) {
	float score = 0.0f;
	float healthRatio = HealthRatio( cs );
	float aggression = cs->attributes[AGGRESSION];

	// very low health strongly pushes retreat
	if ( healthRatio < 0.15f ) {
		score += 0.6f;
	} else if ( healthRatio < 0.3f ) {
		score += 0.3f;
	}

	// high aggression resists retreat
	score -= aggression * 0.3f;

	// low morale pushes retreat
	score += ( 1.0f - cs->morale ) * 0.3f;

	AICast_Comm_BroadcastMessage( cs, COMM_RETREATING, g_entities[cs->entityNum].r.currentOrigin, cs->enemyNum );

	if ( score < 0.0f ) {
		score = 0.0f;
	}
	return score;
}

static qboolean Execute_Retreat( cast_state_t *cs ) {
	// retreat is implemented as take cover with high priority
	AIFunc_BattleTakeCoverStart( cs );
	return qtrue;
}

//===========================================================================
// ACTION: Distract
//===========================================================================
static qboolean Precond_Distract( cast_state_t *cs ) {
	if ( !ai_tactics.integer ) {
		return qfalse;
	}
	if ( cs->enemyNum < 0 ) {
		return qfalse;
	}
	if ( cs->attributes[TACTICAL] < 0.5f ) {
		return qfalse;
	}
	return qtrue;
}

static float Score_Distract( cast_state_t *cs ) {
	return AICast_Tactics_ScoreDistract( cs );
}

static qboolean Execute_Distract( cast_state_t *cs ) {
	return AICast_Tactics_Execute( cs );
}

//===========================================================================
// ACTION: Suppress
//===========================================================================
static qboolean Precond_Suppress( cast_state_t *cs ) {
	if ( cs->enemyNum < 0 ) {
		return qfalse;
	}
	if ( cs->squadRole != ROLE_PINDOWN ) {
		return qfalse;
	}
	if ( !HasAmmo( cs ) ) {
		return qfalse;
	}
	return qtrue;
}

static float Score_Suppress( cast_state_t *cs ) {
	float score = 0.0f;

	if ( cs->squadRole == ROLE_PINDOWN ) {
		score += 0.6f;
	}

	// if teammate is flanking, suppression is critical
	score += cs->attributes[TACTICAL] * 0.2f;

	if ( score < 0.0f ) {
		score = 0.0f;
	}
	return score;
}

static qboolean Execute_Suppress( cast_state_t *cs ) {
	AICast_Comm_BroadcastMessage( cs, COMM_SUPPRESSING, g_entities[cs->entityNum].r.currentOrigin, cs->enemyNum );
	AIFunc_BattleStart( cs );
	return qtrue;
}

//===========================================================================
// ACTION: Peek And Fire
//===========================================================================
static qboolean Precond_PeekAndFire( cast_state_t *cs ) {
	if ( cs->enemyNum < 0 ) {
		return qfalse;
	}
	if ( !( cs->aiFlags & AIFL_IN_COVER ) ) {
		return qfalse;
	}
	if ( !HasAmmo( cs ) ) {
		return qfalse;
	}
	return qtrue;
}

static float Score_PeekAndFire( cast_state_t *cs ) {
	float score = 0.3f;
	float tactical = cs->attributes[TACTICAL];
	float accuracy = cs->attributes[AIM_ACCURACY];

	score += tactical * 0.2f;
	score += accuracy * 0.2f;

	// if in cover, this is a good option
	if ( cs->aiFlags & AIFL_IN_COVER ) {
		score += 0.2f;
	}

	if ( score < 0.0f ) {
		score = 0.0f;
	}
	return score;
}

static qboolean Execute_PeekAndFire( cast_state_t *cs ) {
	vec3_t peekPos;
	if ( AICast_Cover_GetPeekPos( cs, peekPos ) ) {
		VectorCopy( peekPos, cs->combatGoalOrigin );
		cs->combatGoalTime = level.time + 1500;
	}
	AIFunc_BattleStart( cs );
	return qtrue;
}

//===========================================================================
// Public API
//===========================================================================

/*
============
AICast_Planner_Init

  Called at level start
============
*/
void AICast_Planner_Init( void ) {
	trap_Cvar_Register( &ai_planner, "ai_planner", "1", 0 );
}

/*
============
AICast_Planner_ActionName
============
*/
const char *AICast_Planner_ActionName( aiActionId_t id ) {
	if ( id >= 0 && id < AI_ACTION_MAX ) {
		return aiActions[id].name;
	}
	return "Unknown";
}

/*
============
AICast_Planner_Think

  Main entry point for the utility-based planner.
  Returns qtrue if an action was selected and executed.
  Returns qfalse if the planner should not override (scripted, etc).
============
*/
qboolean AICast_Planner_Think( cast_state_t *cs ) {
	int i;
	float bestScore;
	int bestAction;
	ai_planner_state_t *ps;

	if ( !ai_planner.integer ) {
		return qfalse;
	}

	// don't override scripted actions
	if ( cs->aiFlags & AIFL_SPECIAL_FUNC ) {
		return qfalse;
	}

	ps = &cs->plannerState;

	// throttle evaluation
	if ( ps->lastEvalTime > 0 && level.time - ps->lastEvalTime < PLANNER_EVAL_INTERVAL ) {
		return qfalse;
	}

	ps->lastEvalTime = level.time;

	// evaluate all actions
	bestScore = -1.0f;
	bestAction = -1;

	for ( i = 0; i < AI_ACTION_MAX; i++ ) {
		float score;

		// check precondition
		if ( !aiActions[i].precondition( cs ) ) {
			ps->lastScores[i] = 0.0f;
			continue;
		}

		// compute score
		score = aiActions[i].score( cs );
		ps->lastScores[i] = score;

		if ( score > bestScore ) {
			bestScore = score;
			bestAction = i;
		}
	}

	// no valid action found
	if ( bestAction < 0 || bestScore <= 0.0f ) {
		return qfalse;
	}

	// avoid re-executing the same action too frequently unless score is high
	if ( bestAction == ps->currentAction && bestScore < 0.5f ) {
		return qfalse;
	}

	// execute the best action
	if ( aiActions[bestAction].execute( cs ) ) {
		ps->lastAction = ps->currentAction;
		ps->currentAction = (aiActionId_t)bestAction;
		ps->actionStartTime = level.time;

		// record history
		ps->history[ps->historyIndex % PLANNER_HISTORY_SIZE] = (aiActionId_t)bestAction;
		ps->historyIndex++;

		return qtrue;
	}

	return qfalse;
}
