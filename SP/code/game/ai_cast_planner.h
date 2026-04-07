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
// Name:			ai_cast_planner.h
// Function:		Utility-based AI Action Planner
// Programmer:		AI Uplift
// Tab Size:		4 (real tabs)
//===========================================================================

#ifndef AI_CAST_PLANNER_H
#define AI_CAST_PLANNER_H

// forward declarations
struct cast_state_s;

//
// Action IDs
//
typedef enum {
	AI_ACTION_ATTACK,
	AI_ACTION_TAKE_COVER,
	AI_ACTION_ADVANCE,
	AI_ACTION_FLANK,
	AI_ACTION_THROW_GRENADE,
	AI_ACTION_RELOAD,
	AI_ACTION_CALL_FOR_HELP,
	AI_ACTION_WAIT_IN_AMBUSH,
	AI_ACTION_INSPECT_SOUND,
	AI_ACTION_RETREAT,
	AI_ACTION_DISTRACT,
	AI_ACTION_SUPPRESS,
	AI_ACTION_PEEK_AND_FIRE,

	AI_ACTION_MAX
} aiActionId_t;

//
// Action definition
//
typedef struct ai_action_s {
	aiActionId_t    id;
	const char      *name;
	qboolean        ( *precondition )( struct cast_state_s *cs );
	float           ( *score )( struct cast_state_s *cs );
	qboolean        ( *execute )( struct cast_state_s *cs );
} ai_action_t;

//
// Planner state tracked per agent
//
#define PLANNER_HISTORY_SIZE    4

typedef struct ai_planner_state_s {
	aiActionId_t    currentAction;
	aiActionId_t    lastAction;
	int             actionStartTime;
	int             lastEvalTime;
	float           lastScores[AI_ACTION_MAX];
	aiActionId_t    history[PLANNER_HISTORY_SIZE];
	int             historyIndex;
} ai_planner_state_t;

//
// Planner evaluation interval (ms)
//
#define PLANNER_EVAL_INTERVAL   200

//
// Public API
//
void AICast_Planner_Init( void );
qboolean AICast_Planner_Think( struct cast_state_s *cs );
const char *AICast_Planner_ActionName( aiActionId_t id );

//
// Cvar
//
extern vmCvar_t ai_planner;

#endif // AI_CAST_PLANNER_H
