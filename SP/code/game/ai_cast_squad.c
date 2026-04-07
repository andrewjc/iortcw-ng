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
// Name:			ai_cast_squad.c
// Function:		Dynamic Squad Formation System
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
#include "ai_cast_squad.h"
#include "ai_cast_comm.h"

vmCvar_t ai_squads;

//
// Global squad table
//
static aiSquad_t squads[MAX_SQUADS];
static int nextSquadId = 1;

// forward declarations
static aiSquad_t *AICast_Squad_FindSquad( int squadId );
static aiSquad_t *AICast_Squad_FindFreeSlot( void );
static void AICast_Squad_AssignRoles( aiSquad_t *squad );

/*
============
AICast_Squad_Init

  Called at level start
============
*/
void AICast_Squad_Init( void ) {
	memset( squads, 0, sizeof( squads ) );
	nextSquadId = 1;
	trap_Cvar_Register( &ai_squads, "ai_squads", "1", 0 );
}

/*
============
AICast_Squad_FindSquad

  Find the squad with given ID, or NULL
============
*/
static aiSquad_t *AICast_Squad_FindSquad( int squadId ) {
	int i;
	for ( i = 0; i < MAX_SQUADS; i++ ) {
		if ( squads[i].active && squads[i].id == squadId ) {
			return &squads[i];
		}
	}
	return NULL;
}

/*
============
AICast_Squad_FindFreeSlot

  Find a free squad slot, or NULL
============
*/
static aiSquad_t *AICast_Squad_FindFreeSlot( void ) {
	int i;
	for ( i = 0; i < MAX_SQUADS; i++ ) {
		if ( !squads[i].active ) {
			return &squads[i];
		}
	}
	return NULL;
}

/*
============
AICast_Squad_FormSquad

  Attempt to form a squad around the given agent.
  Returns the squad ID, or SQUAD_NONE if formation failed.
============
*/
int AICast_Squad_FormSquad( cast_state_t *cs ) {
	aiSquad_t *squad;
	gentity_t *ent = &g_entities[cs->entityNum];
	int i;
	int bestLeader = cs->entityNum;
	float bestLeaderScore = cs->attributes[LEADER];

	if ( !ai_squads.integer ) {
		return SQUAD_NONE;
	}

	// already in a squad?
	if ( cs->squadId != SQUAD_NONE ) {
		return cs->squadId;
	}

	squad = AICast_Squad_FindFreeSlot();
	if ( !squad ) {
		return SQUAD_NONE;
	}

	memset( squad, 0, sizeof( *squad ) );
	squad->id = nextSquadId++;
	squad->active = qtrue;
	squad->aiTeam = ent->aiTeam;
	squad->formationTime = level.time;
	squad->lastUpdateTime = level.time;
	squad->state = SQUAD_IDLE;
	squad->targetEnemy = -1;

	// add self as first member
	squad->members[0] = cs->entityNum;
	squad->memberCount = 1;
	cs->squadId = squad->id;

	// find nearby teammates of the same type to join
	for ( i = 0; i < level.maxclients && squad->memberCount < MAX_SQUAD_SIZE; i++ ) {
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

		// must be same team
		if ( !AICast_SameTeam( cs, i ) ) {
			continue;
		}

		// must be within range
		dist = Distance( ent->r.currentOrigin, g_entities[i].r.currentOrigin );
		if ( dist > SQUAD_FORM_RANGE ) {
			continue;
		}

		// must not already be in a squad
		if ( other->squadId != SQUAD_NONE ) {
			continue;
		}

		// add to squad
		squad->members[squad->memberCount] = i;
		squad->memberCount++;
		other->squadId = squad->id;

		// check if they're a better leader
		if ( other->attributes[LEADER] > bestLeaderScore ) {
			bestLeaderScore = other->attributes[LEADER];
			bestLeader = i;
		}
	}

	// need at least 2 members to form a squad
	if ( squad->memberCount < 2 ) {
		cs->squadId = SQUAD_NONE;
		squad->active = qfalse;
		return SQUAD_NONE;
	}

	squad->leaderNum = bestLeader;

	// assign initial roles
	AICast_Squad_AssignRoles( squad );

	return squad->id;
}

/*
============
AICast_Squad_AssignRoles

  Leader assigns roles to squad members
============
*/
static void AICast_Squad_AssignRoles( aiSquad_t *squad ) {
	int i;
	qboolean flankerAssigned = qfalse;

	for ( i = 0; i < squad->memberCount; i++ ) {
		cast_state_t *member = AICast_GetCastState( squad->members[i] );
		if ( !member ) {
			continue;
		}

		if ( squad->members[i] == squad->leaderNum ) {
			member->squadRole = ROLE_PINDOWN;   // leader pins
			continue;
		}

		// assign one flanker (highest tactical)
		if ( !flankerAssigned && member->attributes[TACTICAL] > 0.5f ) {
			member->squadRole = ROLE_FLANK;
			flankerAssigned = qtrue;
			continue;
		}

		// rest are support/overwatch
		if ( i == squad->memberCount - 1 ) {
			member->squadRole = ROLE_OVERWATCH;
		} else {
			member->squadRole = ROLE_SUPPORT;
		}
	}
}

/*
============
AICast_Squad_DissolveSquad
============
*/
void AICast_Squad_DissolveSquad( int squadId ) {
	aiSquad_t *squad = AICast_Squad_FindSquad( squadId );
	int i;

	if ( !squad ) {
		return;
	}

	for ( i = 0; i < squad->memberCount; i++ ) {
		cast_state_t *member = AICast_GetCastState( squad->members[i] );
		if ( member ) {
			member->squadId = SQUAD_NONE;
			member->squadRole = ROLE_NONE;
		}
	}

	squad->active = qfalse;
}

/*
============
AICast_Squad_RemoveMember
============
*/
void AICast_Squad_RemoveMember( int squadId, int entityNum ) {
	aiSquad_t *squad = AICast_Squad_FindSquad( squadId );
	int i, j;
	cast_state_t *member;

	if ( !squad ) {
		return;
	}

	for ( i = 0; i < squad->memberCount; i++ ) {
		if ( squad->members[i] == entityNum ) {
			member = AICast_GetCastState( entityNum );
			if ( member ) {
				member->squadId = SQUAD_NONE;
				member->squadRole = ROLE_NONE;
			}

			// shift members down
			for ( j = i; j < squad->memberCount - 1; j++ ) {
				squad->members[j] = squad->members[j + 1];
			}
			squad->memberCount--;
			break;
		}
	}

	// dissolve if too few members
	if ( squad->memberCount < 2 ) {
		AICast_Squad_DissolveSquad( squadId );
		return;
	}

	// if leader was removed, pick a new one
	if ( entityNum == squad->leaderNum ) {
		float bestLeaderScore = -1.0f;
		squad->leaderNum = squad->members[0];
		for ( i = 0; i < squad->memberCount; i++ ) {
			member = AICast_GetCastState( squad->members[i] );
			if ( member && member->attributes[LEADER] > bestLeaderScore ) {
				bestLeaderScore = member->attributes[LEADER];
				squad->leaderNum = squad->members[i];
			}
		}
		AICast_Squad_AssignRoles( squad );
	}
}

/*
============
AICast_Squad_UpdateState

  Update the squad's tactical state based on combined situation
============
*/
void AICast_Squad_UpdateState( int squadId ) {
	aiSquad_t *squad = AICast_Squad_FindSquad( squadId );
	int i;
	float totalHealth = 0.0f;
	float maxHealth = 0.0f;
	int membersInCombat = 0;
	int membersAlive = 0;

	if ( !squad ) {
		return;
	}

	if ( level.time - squad->lastUpdateTime < SQUAD_UPDATE_INTERVAL ) {
		return;
	}
	squad->lastUpdateTime = level.time;

	// gather squad stats
	for ( i = 0; i < squad->memberCount; i++ ) {
		cast_state_t *member = AICast_GetCastState( squad->members[i] );
		gentity_t *memberEnt;

		if ( !member ) {
			continue;
		}

		memberEnt = &g_entities[squad->members[i]];
		if ( memberEnt->health <= 0 ) {
			continue;
		}

		membersAlive++;
		totalHealth += memberEnt->health;
		maxHealth += member->attributes[STARTING_HEALTH];

		if ( member->aiState == AISTATE_COMBAT ) {
			membersInCombat++;
		}
	}

	// check if squad should dissolve (all dead or dispersed)
	if ( membersAlive < 2 ) {
		AICast_Squad_DissolveSquad( squadId );
		return;
	}

	// check distance-based dissolution
	{
		cast_state_t *leader = AICast_GetCastState( squad->leaderNum );
		if ( leader ) {
			for ( i = 0; i < squad->memberCount; i++ ) {
				if ( squad->members[i] != squad->leaderNum ) {
					float dist = Distance( g_entities[squad->leaderNum].r.currentOrigin,
										   g_entities[squad->members[i]].r.currentOrigin );
					if ( dist > SQUAD_DISSOLVE_RANGE ) {
						AICast_Squad_RemoveMember( squadId, squad->members[i] );
						break; // re-enter on next update
					}
				}
			}
		}
	}

	// determine squad state based on situation
	{
		float healthRatio = ( maxHealth > 0.0f ) ? totalHealth / maxHealth : 0.0f;

		if ( healthRatio < 0.2f ) {
			squad->state = SQUAD_RETREAT;
		} else if ( membersInCombat > 0 && healthRatio > 0.5f ) {
			// offensive: advance or flank
			if ( squad->memberCount >= 3 ) {
				squad->state = SQUAD_FLANK;
			} else {
				squad->state = SQUAD_ADVANCE;
			}
		} else if ( membersInCombat > 0 ) {
			squad->state = SQUAD_HOLD;
		} else {
			squad->state = SQUAD_IDLE;
		}
	}

	// share target information
	{
		cast_state_t *leader = AICast_GetCastState( squad->leaderNum );
		if ( leader && leader->enemyNum >= 0 ) {
			squad->targetEnemy = leader->enemyNum;
			VectorCopy( g_entities[leader->enemyNum].r.currentOrigin, squad->targetPos );
		}
	}
}

/*
============
AICast_Squad_GetFormationPos

  Get a formation position for a squad member relative to the leader.
  Returns qtrue if a position was computed.
============
*/
qboolean AICast_Squad_GetFormationPos( cast_state_t *cs, vec3_t outPos ) {
	aiSquad_t *squad = AICast_Squad_FindSquad( cs->squadId );
	cast_state_t *leader;
	int myIndex = -1;
	int i;
	vec3_t leaderForward, leaderRight;
	vec3_t offset;
	float spacing = 96.0f;

	if ( !squad || !ai_squads.integer ) {
		return qfalse;
	}

	// find my index in the squad
	for ( i = 0; i < squad->memberCount; i++ ) {
		if ( squad->members[i] == cs->entityNum ) {
			myIndex = i;
			break;
		}
	}

	if ( myIndex < 0 ) {
		return qfalse;
	}

	// leader doesn't need a formation position
	if ( cs->entityNum == squad->leaderNum ) {
		return qfalse;
	}

	leader = AICast_GetCastState( squad->leaderNum );
	if ( !leader || !leader->bs ) {
		return qfalse;
	}

	// compute leader's facing direction
	AngleVectors( leader->viewangles, leaderForward, leaderRight, NULL );

	// compute offset based on squad index
	switch ( myIndex % 3 ) {
	case 0:
		// right flank
		VectorScale( leaderRight, spacing, offset );
		VectorMA( offset, -spacing * 0.5f, leaderForward, offset );
		break;
	case 1:
		// left flank
		VectorScale( leaderRight, -spacing, offset );
		VectorMA( offset, -spacing * 0.5f, leaderForward, offset );
		break;
	case 2:
		// rear center
		VectorScale( leaderForward, -spacing, offset );
		break;
	default:
		VectorClear( offset );
		break;
	}

	VectorAdd( g_entities[squad->leaderNum].r.currentOrigin, offset, outPos );
	return qtrue;
}

/*
============
AICast_Squad_Update

  Called each server frame to update all squads
============
*/
void AICast_Squad_Update( void ) {
	int i;

	if ( !ai_squads.integer ) {
		return;
	}

	for ( i = 0; i < MAX_SQUADS; i++ ) {
		if ( squads[i].active ) {
			AICast_Squad_UpdateState( squads[i].id );
		}
	}

	// try to form new squads from agents not in squads
	for ( i = 0; i < level.maxclients; i++ ) {
		cast_state_t *cs;
		if ( !g_entities[i].inuse || g_entities[i].health <= 0 ) {
			continue;
		}

		cs = AICast_GetCastState( i );
		if ( !cs || !cs->bs ) {
			continue;
		}

		// only try to form squads for agents in combat
		if ( cs->aiState < AISTATE_COMBAT ) {
			continue;
		}

		if ( cs->squadId == SQUAD_NONE ) {
			AICast_Squad_FormSquad( cs );
		}
	}
}
