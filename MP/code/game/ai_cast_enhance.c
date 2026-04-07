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
// Name:			ai_cast_enhance.c
// Function:		Phase 7 AI Enhancements - Morale, Prediction,
//					Suppression, Adaptive Difficulty, Alert Propagation
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
#include "ai_cast_comm.h"
#include "ai_cast_squad.h"

//
// Morale system constants
//
#define MORALE_UPDATE_INTERVAL  500     // update morale every 500ms
#define MORALE_TEAMMATE_DEATH_HIT   0.15f
#define MORALE_DAMAGE_FACTOR    0.001f  // per damage point lost
#define MORALE_RECOVERY_RATE    0.02f   // recovery per update when safe
#define MORALE_ENEMY_RETREAT_BOOST  0.1f
#define MORALE_MIN              0.0f
#define MORALE_MAX              1.0f

//
// Suppression constants
//
#define SUPPRESSION_DURATION    1500    // ms of suppression per bullet impact
#define SUPPRESSION_COOLDOWN    500     // ms between suppression checks

//
// Adaptive difficulty constants
//
#define ADAPTIVE_UPDATE_INTERVAL    10000   // 10 seconds
#define ADAPTIVE_MIN_SCALE          0.6f
#define ADAPTIVE_MAX_SCALE          1.4f
#define PLAYER_EFFICIENCY_WINDOW    30000   // 30 second window

static int lastAdaptiveUpdateTime = 0;
static int playerKillsInWindow = 0;
static int playerKillWindowStart = 0;
static int playerHeadshotsInWindow = 0;

//===========================================================================
// Phase 7a: Morale System
//===========================================================================

/*
============
AICast_UpdateMorale

  Update an agent's morale based on health, team status, and combat situation
============
*/
void AICast_UpdateMorale( cast_state_t *cs ) {
	gentity_t *ent = &g_entities[cs->entityNum];
	float healthRatio;
	float maxHealth;
	int i;
	int teammatesDead = 0;
	int teammatesAlive = 0;

	if ( level.time - cs->lastMoraleUpdateTime < MORALE_UPDATE_INTERVAL ) {
		return;
	}
	cs->lastMoraleUpdateTime = level.time;

	// health contribution
	maxHealth = cs->attributes[STARTING_HEALTH];
	if ( maxHealth <= 0.0f ) {
		maxHealth = 100.0f;
	}
	healthRatio = (float)ent->health / maxHealth;

	// if health dropped since last check, lose morale
	if ( healthRatio < 0.5f ) {
		cs->morale -= ( 0.5f - healthRatio ) * 0.05f;
	}

	// check teammates
	for ( i = 0; i < level.maxclients; i++ ) {
		if ( i == cs->entityNum ) {
			continue;
		}
		if ( !g_entities[i].inuse ) {
			continue;
		}
		if ( !AICast_SameTeam( cs, i ) ) {
			continue;
		}
		if ( Distance( ent->r.currentOrigin, g_entities[i].r.currentOrigin ) > 768.0f ) {
			continue;
		}
		if ( g_entities[i].health <= 0 ) {
			teammatesDead++;
		} else {
			teammatesAlive++;
		}
	}

	// teammate deaths lower morale
	if ( teammatesDead > 0 ) {
		cs->morale -= teammatesDead * MORALE_TEAMMATE_DEATH_HIT * 0.1f; // scaled down per frame
	}

	// having alive teammates boosts morale
	if ( teammatesAlive > 0 ) {
		cs->morale += teammatesAlive * 0.005f;
	}

	// if not in combat, slowly recover morale
	if ( cs->aiState < AISTATE_COMBAT ) {
		cs->morale += MORALE_RECOVERY_RATE;
	}

	// aggression attribute provides morale floor
	{
		float moraleFloor = cs->attributes[AGGRESSION] * 0.3f;
		if ( cs->morale < moraleFloor ) {
			cs->morale = moraleFloor;
		}
	}

	// clamp
	if ( cs->morale < MORALE_MIN ) {
		cs->morale = MORALE_MIN;
	}
	if ( cs->morale > MORALE_MAX ) {
		cs->morale = MORALE_MAX;
	}
}

//===========================================================================
// Phase 7b: Memory & Prediction
//===========================================================================

/*
============
AICast_RecordEnemyPos

  Record the enemy's position into the history buffer for prediction
============
*/
void AICast_RecordEnemyPos( cast_state_t *cs ) {
	int idx;

	if ( cs->enemyNum < 0 || cs->enemyNum >= level.maxclients ) {
		return;
	}

	if ( !g_entities[cs->enemyNum].inuse || g_entities[cs->enemyNum].health <= 0 ) {
		return;
	}

	// only record if we can see the enemy
	if ( cs->vislist[cs->enemyNum].visible_timestamp < level.time - 1000 ) {
		return;
	}

	idx = cs->enemyPosHistoryIndex % 3;
	VectorCopy( g_entities[cs->enemyNum].r.currentOrigin, cs->enemyPosHistory[idx] );
	cs->enemyPosHistoryTime[idx] = level.time;
	cs->enemyPosHistoryIndex++;

	// record patrol origin on first combat entry
	if ( !cs->patrolOriginSet ) {
		VectorCopy( g_entities[cs->entityNum].r.currentOrigin, cs->patrolOrigin );
		cs->patrolOriginSet = qtrue;
	}
}

/*
============
AICast_PredictEnemyPos

  Predict enemy position based on velocity history using linear extrapolation.
  timeAhead is in seconds.
============
*/
void AICast_PredictEnemyPos( cast_state_t *cs, float timeAhead, vec3_t outPos ) {
	int newest, secondNewest;
	int timeDelta;
	vec3_t velocity;

	// need at least 2 history entries
	if ( cs->enemyPosHistoryIndex < 2 ) {
		// fallback: just use last known position
		if ( cs->enemyNum >= 0 && cs->enemyNum < level.maxclients ) {
			VectorCopy( cs->vislist[cs->enemyNum].visible_pos, outPos );
		} else {
			VectorClear( outPos );
		}
		return;
	}

	newest = ( cs->enemyPosHistoryIndex - 1 ) % 3;
	secondNewest = ( cs->enemyPosHistoryIndex - 2 ) % 3;

	timeDelta = cs->enemyPosHistoryTime[newest] - cs->enemyPosHistoryTime[secondNewest];
	if ( timeDelta <= 0 ) {
		VectorCopy( cs->enemyPosHistory[newest], outPos );
		return;
	}

	// compute velocity
	VectorSubtract( cs->enemyPosHistory[newest], cs->enemyPosHistory[secondNewest], velocity );
	VectorScale( velocity, 1.0f / ( (float)timeDelta / 1000.0f ), velocity );

	// extrapolate
	VectorMA( cs->enemyPosHistory[newest], timeAhead, velocity, outPos );
}

//===========================================================================
// Phase 7c: Reactive Suppression
//===========================================================================

/*
============
AICast_UpdateSuppression

  When bullets impact near the agent, enter a suppressed state
============
*/
void AICast_UpdateSuppression( cast_state_t *cs ) {
	// check for recent bullet impacts
	if ( cs->bulletImpactTime > level.time - 500 ) {
		float resistance = cs->attributes[AGGRESSION] * 0.5f;

		// suppression duration reduced by aggression
		int duration = (int)( SUPPRESSION_DURATION * ( 1.0f - resistance ) );
		if ( duration < 200 ) {
			duration = 200;
		}

		cs->suppressedUntil = level.time + duration;

		// broadcast that we're under fire
		AICast_Comm_BroadcastMessage( cs, COMM_UNDER_FIRE,
									  g_entities[cs->entityNum].r.currentOrigin,
									  cs->enemyNum );
	}

	// store resistance for other systems to query
	cs->suppressionResistance = cs->attributes[AGGRESSION];
}

//===========================================================================
// Phase 7e: Adaptive Difficulty
//===========================================================================

/*
============
AICast_RegisterPlayerKill

  Called externally when the player kills an AI.
  Tracks kills for adaptive difficulty.
============
*/
void AICast_RegisterPlayerKill( qboolean headshot ) {
	if ( level.time - playerKillWindowStart > PLAYER_EFFICIENCY_WINDOW ) {
		playerKillsInWindow = 0;
		playerHeadshotsInWindow = 0;
		playerKillWindowStart = level.time;
	}
	playerKillsInWindow++;
	if ( headshot ) {
		playerHeadshotsInWindow++;
	}
}

/*
============
AICast_UpdateAdaptiveDifficulty

  Periodically adjust AI difficulty based on player performance.
  Called from AICast_StartFrame.
============
*/
void AICast_UpdateAdaptiveDifficulty( void ) {
	float killRate;
	float headshotRatio;
	float diffScale;
	int i;

	if ( level.time - lastAdaptiveUpdateTime < ADAPTIVE_UPDATE_INTERVAL ) {
		return;
	}
	lastAdaptiveUpdateTime = level.time;

	// compute player efficiency
	if ( level.time - playerKillWindowStart > 0 ) {
		killRate = (float)playerKillsInWindow / ( (float)( level.time - playerKillWindowStart ) / 1000.0f );
	} else {
		killRate = 0.0f;
	}

	headshotRatio = ( playerKillsInWindow > 0 ) ?
					(float)playerHeadshotsInWindow / (float)playerKillsInWindow : 0.0f;

	// higher kill rate + headshot ratio = player is performing well -> increase AI difficulty
	// base difficulty = 1.0, range [0.6, 1.4]
	diffScale = 1.0f;
	diffScale += ( killRate - 0.3f ) * 0.5f;       // 0.3 kills/sec is baseline
	diffScale += ( headshotRatio - 0.2f ) * 0.3f;  // 20% headshots is baseline

	// clamp
	if ( diffScale < ADAPTIVE_MIN_SCALE ) {
		diffScale = ADAPTIVE_MIN_SCALE;
	}
	if ( diffScale > ADAPTIVE_MAX_SCALE ) {
		diffScale = ADAPTIVE_MAX_SCALE;
	}

	// apply to all active AI
	for ( i = 0; i < level.maxclients; i++ ) {
		cast_state_t *cs;

		if ( !g_entities[i].inuse || g_entities[i].health <= 0 ) {
			continue;
		}

		cs = AICast_GetCastState( i );
		if ( !cs || !cs->bs ) {
			continue;
		}

		cs->difficultyScale = diffScale;
	}
}

//===========================================================================
// Phase 7f: Group Alert Propagation
//===========================================================================

/*
============
AICast_PropagateAlert

  When an agent becomes alerted, propagate the alert to nearby agents
  through the communication system, using range-based wave propagation.
  This extends AICast_AudibleEvent with richer team comms.
============
*/
void AICast_PropagateAlert( cast_state_t *cs, vec3_t enemyPos, int enemyNum ) {
	int i;
	gentity_t *ent = &g_entities[cs->entityNum];

	// broadcast enemy spotted
	AICast_Comm_BroadcastMessage( cs, COMM_ENEMY_SPOTTED, enemyPos, enemyNum );

	// also propagate through AAS areas for wave-based alerting
	// agents further away hear the call with increased delay
	for ( i = 0; i < level.maxclients; i++ ) {
		cast_state_t *other;
		float dist;

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

		dist = Distance( ent->r.currentOrigin, g_entities[i].r.currentOrigin );

		// extended range through walls (agents can hear shouting)
		if ( dist < COMM_RANGE * 1.5f ) {
			// update their visibility with our knowledge
			if ( enemyNum >= 0 && enemyNum < level.maxclients ) {
				VectorCopy( enemyPos, other->vislist[enemyNum].visible_pos );
				other->vislist[enemyNum].visible_timestamp = level.time;
			}

			// raise their alert state if they're idle
			if ( other->aiState < AISTATE_ALERT ) {
				AICast_StateChange( other, AISTATE_ALERT );
			}
		}
	}
}
