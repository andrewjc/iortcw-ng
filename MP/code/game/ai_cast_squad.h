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
// Name:			ai_cast_squad.h
// Function:		Dynamic Squad Formation System
// Programmer:		AI Uplift
// Tab Size:		4 (real tabs)
//===========================================================================

#ifndef AI_CAST_SQUAD_H
#define AI_CAST_SQUAD_H

struct cast_state_s;

//
// Squad constants
//
#define MAX_SQUADS          16
#define MAX_SQUAD_SIZE      4
#define SQUAD_FORM_RANGE    512.0f
#define SQUAD_DISSOLVE_RANGE    768.0f
#define SQUAD_UPDATE_INTERVAL   500     // ms between squad state updates
#define SQUAD_NONE          -1

//
// Squad states
//
typedef enum {
	SQUAD_IDLE,
	SQUAD_ADVANCE,
	SQUAD_HOLD,
	SQUAD_FLANK,
	SQUAD_RETREAT
} squadState_t;

//
// Squad structure
//
typedef struct aiSquad_s {
	int             id;
	int             leaderNum;                      // entity number of leader
	int             members[MAX_SQUAD_SIZE];         // entity numbers
	int             memberCount;
	int             aiTeam;                         // which AI team this squad belongs to
	squadState_t    state;
	int             targetEnemy;                    // shared enemy target
	vec3_t          targetPos;                      // shared target position
	int             lastUpdateTime;
	int             formationTime;                  // when the squad was formed
	qboolean        active;
} aiSquad_t;

//
// Public API
//
void AICast_Squad_Init( void );
void AICast_Squad_Update( void );
int  AICast_Squad_FormSquad( struct cast_state_s *cs );
void AICast_Squad_DissolveSquad( int squadId );
void AICast_Squad_RemoveMember( int squadId, int entityNum );
void AICast_Squad_UpdateState( int squadId );
qboolean AICast_Squad_GetFormationPos( struct cast_state_s *cs, vec3_t outPos );

//
// Cvar
//
extern vmCvar_t ai_squads;

#endif // AI_CAST_SQUAD_H
