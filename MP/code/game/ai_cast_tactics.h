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
// Name:			ai_cast_tactics.h
// Function:		Distraction & Tactical Maneuvers
// Programmer:		AI Uplift
// Tab Size:		4 (real tabs)
//===========================================================================

#ifndef AI_CAST_TACTICS_H
#define AI_CAST_TACTICS_H

struct cast_state_s;

//
// Tactic types
//
typedef enum {
	TACTIC_NONE,
	TACTIC_GRENADE_AND_HIDE,    // throw grenade then duck into cover
	TACTIC_NOISE_DISTRACTION,   // create noise in one direction, move another
	TACTIC_BAIT_AND_AMBUSH,     // expose briefly to lure, then ambush
	TACTIC_SUPPRESS_AND_MOVE    // suppress fire then reposition
} tacticType_t;

//
// Tactic phases
//
typedef enum {
	TACTIC_PHASE_NONE,
	TACTIC_PHASE_SETUP,        // move to starting position
	TACTIC_PHASE_EXECUTE,      // perform the main action (throw grenade, fire, etc)
	TACTIC_PHASE_EXPLOIT,      // exploit the result (ambush, flank, etc)
	TACTIC_PHASE_ABORT         // cancel and fallback
} tacticPhase_t;

//
// Tactic state per agent
//
typedef struct ai_tactic_state_s {
	tacticType_t    type;
	tacticPhase_t   phase;
	int             startTime;
	int             phaseStartTime;
	vec3_t          targetPos;      // where to direct the distraction
	vec3_t          coverPos;       // where to take cover after
	vec3_t          exploitPos;     // where to move for the exploit phase
	int             timeoutTime;    // abort if exceeded
} ai_tactic_state_t;

//
// Public API
//
void AICast_Tactics_Init( void );
float AICast_Tactics_ScoreDistract( struct cast_state_s *cs );
float AICast_Tactics_ScoreBait( struct cast_state_s *cs );
qboolean AICast_Tactics_Execute( struct cast_state_s *cs );
void AICast_Tactics_Abort( struct cast_state_s *cs );

//
// Cvar
//
extern vmCvar_t ai_tactics;

#endif // AI_CAST_TACTICS_H
