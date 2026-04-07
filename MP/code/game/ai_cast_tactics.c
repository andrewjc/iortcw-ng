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
// Name:			ai_cast_tactics.c
// Function:		Distraction & Tactical Maneuvers
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
#include "ai_cast_tactics.h"
#include "ai_cast_cover.h"
#include "ai_cast_comm.h"

vmCvar_t ai_tactics;

#define TACTIC_TIMEOUT      10000   // 10 seconds max for any tactic
#define GRENADE_HIDE_DELAY  500     // ms between grenade throw and hide
#define EXPLOIT_DELAY       2000    // ms to wait after distraction before exploiting

// forward declarations
static qboolean AICast_Tactics_ExecGrenadeAndHide( cast_state_t *cs );
static qboolean AICast_Tactics_ExecBaitAndAmbush( cast_state_t *cs );

/*
============
AICast_Tactics_Init

  Called at level start
============
*/
void AICast_Tactics_Init( void ) {
	trap_Cvar_Register( &ai_tactics, "ai_tactics", "1", 0 );
}

/*
============
AICast_Tactics_ScoreDistract

  Score the grenade-and-hide distraction tactic
============
*/
float AICast_Tactics_ScoreDistract( cast_state_t *cs ) {
	float score = 0.0f;
	gentity_t *ent = &g_entities[cs->entityNum];
	float dist;
	float tactical;

	if ( !ai_tactics.integer ) {
		return 0.0f;
	}

	if ( cs->enemyNum < 0 ) {
		return 0.0f;
	}

	tactical = cs->attributes[TACTICAL];
	dist = Distance( ent->r.currentOrigin, g_entities[cs->enemyNum].r.currentOrigin );

	// need grenades
	if ( !COM_BitCheck( ent->client->ps.weapons, WP_GRENADE_LAUNCHER ) ||
		 !AICast_GotEnoughAmmoForWeapon( cs, WP_GRENADE_LAUNCHER ) ) {
		return 0.0f;
	}

	// best at medium range
	if ( dist > 256.0f && dist < 768.0f ) {
		score += 0.2f;
	} else {
		return 0.0f;
	}

	// tactical attribute drives this
	score += tactical * 0.4f;

	// if already executing a tactic, don't start another
	if ( cs->tacticState.type != TACTIC_NONE ) {
		return 0.0f;
	}

	// cooldown from last grenade flush
	if ( cs->grenadeFlushEndTime > level.time - 8000 ) {
		return 0.0f;
	}

	return score;
}

/*
============
AICast_Tactics_ScoreBait

  Score the bait-and-ambush tactic
============
*/
float AICast_Tactics_ScoreBait( cast_state_t *cs ) {
	float score = 0.0f;
	float tactical;
	float healthRatio;
	gentity_t *ent = &g_entities[cs->entityNum];

	if ( !ai_tactics.integer ) {
		return 0.0f;
	}

	if ( cs->enemyNum < 0 ) {
		return 0.0f;
	}

	tactical = cs->attributes[TACTICAL];
	healthRatio = (float)ent->health / cs->attributes[STARTING_HEALTH];

	// bait works when we're low health but have good tactical sense
	if ( healthRatio < 0.5f && tactical > 0.6f ) {
		score += 0.3f;
	}

	// need a place to hide
	if ( cs->aiFlags & AIFL_IN_COVER ) {
		score += 0.1f;
	}

	// already running a tactic
	if ( cs->tacticState.type != TACTIC_NONE ) {
		return 0.0f;
	}

	score += tactical * 0.2f;

	return score;
}

/*
============
AICast_Tactics_Execute

  Execute the current tactic phase, or start a new one.
  Returns qtrue if we're handling the AI this frame.
============
*/
qboolean AICast_Tactics_Execute( cast_state_t *cs ) {
	ai_tactic_state_t *ts = &cs->tacticState;

	if ( !ai_tactics.integer ) {
		return qfalse;
	}

	// start a new tactic if none active
	if ( ts->type == TACTIC_NONE ) {
		// decide which tactic to use
		if ( AICast_Tactics_ScoreDistract( cs ) > AICast_Tactics_ScoreBait( cs ) ) {
			ts->type = TACTIC_GRENADE_AND_HIDE;
		} else {
			ts->type = TACTIC_BAIT_AND_AMBUSH;
		}
		ts->phase = TACTIC_PHASE_SETUP;
		ts->startTime = level.time;
		ts->phaseStartTime = level.time;
		ts->timeoutTime = level.time + TACTIC_TIMEOUT;
	}

	// timeout check
	if ( level.time > ts->timeoutTime ) {
		AICast_Tactics_Abort( cs );
		return qfalse;
	}

	switch ( ts->type ) {
	case TACTIC_GRENADE_AND_HIDE:
		return AICast_Tactics_ExecGrenadeAndHide( cs );

	case TACTIC_BAIT_AND_AMBUSH:
		return AICast_Tactics_ExecBaitAndAmbush( cs );

	default:
		AICast_Tactics_Abort( cs );
		return qfalse;
	}
}

/*
============
AICast_Tactics_ExecGrenadeAndHide

  Phase-based grenade-and-hide tactic execution
============
*/
static qboolean AICast_Tactics_ExecGrenadeAndHide( cast_state_t *cs ) {
	ai_tactic_state_t *ts = &cs->tacticState;

	switch ( ts->phase ) {
	case TACTIC_PHASE_SETUP:
		// find a cover position to retreat to after throwing
		if ( AICast_FindBestCoverSpot( cs, ts->coverPos ) ) {
			VectorCopy( g_entities[cs->enemyNum].r.currentOrigin, ts->targetPos );
			ts->phase = TACTIC_PHASE_EXECUTE;
			ts->phaseStartTime = level.time;
			return qtrue;
		}
		// couldn't find cover, abort
		AICast_Tactics_Abort( cs );
		return qfalse;

	case TACTIC_PHASE_EXECUTE:
		// throw the grenade
		AIFunc_GrenadeFlushStart( cs );
		AICast_Comm_BroadcastMessage( cs, COMM_GRENADE_OUT, ts->targetPos, cs->enemyNum );

		// after a brief delay, transition to exploit (hide)
		if ( level.time - ts->phaseStartTime > GRENADE_HIDE_DELAY ) {
			ts->phase = TACTIC_PHASE_EXPLOIT;
			ts->phaseStartTime = level.time;
		}
		return qtrue;

	case TACTIC_PHASE_EXPLOIT:
		// move to cover and wait
		VectorCopy( ts->coverPos, cs->takeCoverPos );
		cs->takeCoverTime = level.time + 3000;
		AIFunc_BattleTakeCoverStart( cs );

		// after the exploit delay, we're done
		if ( level.time - ts->phaseStartTime > EXPLOIT_DELAY ) {
			// tactic complete, return to normal combat
			AICast_Tactics_Abort( cs );
			AIFunc_BattleStart( cs );
		}
		return qtrue;

	default:
		AICast_Tactics_Abort( cs );
		return qfalse;
	}
}

/*
============
AICast_Tactics_ExecBaitAndAmbush

  Phase-based bait-and-ambush tactic execution
============
*/
static qboolean AICast_Tactics_ExecBaitAndAmbush( cast_state_t *cs ) {
	ai_tactic_state_t *ts = &cs->tacticState;

	switch ( ts->phase ) {
	case TACTIC_PHASE_SETUP:
		// find cover position for ambush
		if ( AICast_FindBestCoverSpot( cs, ts->coverPos ) ) {
			ts->phase = TACTIC_PHASE_EXECUTE;
			ts->phaseStartTime = level.time;
			return qtrue;
		}
		AICast_Tactics_Abort( cs );
		return qfalse;

	case TACTIC_PHASE_EXECUTE:
		// briefly expose ourselves (bait)
		if ( level.time - ts->phaseStartTime < 800 ) {
			// stand in the open briefly
			AIFunc_BattleStart( cs );
			return qtrue;
		}
		// now hide
		ts->phase = TACTIC_PHASE_EXPLOIT;
		ts->phaseStartTime = level.time;
		VectorCopy( ts->coverPos, cs->takeCoverPos );
		cs->takeCoverTime = level.time + 5000;
		AIFunc_BattleTakeCoverStart( cs );
		return qtrue;

	case TACTIC_PHASE_EXPLOIT:
		// wait in ambush position
		if ( level.time - ts->phaseStartTime > 3000 ) {
			// ambush - spring out and attack
			AIFunc_BattleStart( cs );
			AICast_Tactics_Abort( cs );
		} else {
			// still waiting
			AIFunc_BattleAmbushStart( cs );
		}
		return qtrue;

	default:
		AICast_Tactics_Abort( cs );
		return qfalse;
	}
}

/*
============
AICast_Tactics_Abort

  Cancel the current tactic and reset state
============
*/
void AICast_Tactics_Abort( cast_state_t *cs ) {
	memset( &cs->tacticState, 0, sizeof( cs->tacticState ) );
	cs->tacticState.type = TACTIC_NONE;
	cs->tacticState.phase = TACTIC_PHASE_NONE;
}
