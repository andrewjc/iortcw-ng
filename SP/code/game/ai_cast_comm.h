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
// Name:			ai_cast_comm.h
// Function:		AI Communication & Coordination System
// Programmer:		AI Uplift
// Tab Size:		4 (real tabs)
//===========================================================================

#ifndef AI_CAST_COMM_H
#define AI_CAST_COMM_H

struct cast_state_s;

//
// Communication message types
//
typedef enum {
	COMM_ENEMY_SPOTTED,
	COMM_UNDER_FIRE,
	COMM_GOING_FLANK,
	COMM_GOING_COVER,
	COMM_NEED_HELP,
	COMM_CLEAR,
	COMM_GRENADE_OUT,
	COMM_RETREATING,
	COMM_SUPPRESSING,

	COMM_MAX_TYPES
} commMsgType_t;

//
// AI Role assignments (from squad leader)
//
typedef enum {
	ROLE_NONE,
	ROLE_PINDOWN,       // suppress/pin enemy
	ROLE_FLANK,         // move to flank position
	ROLE_OVERWATCH,     // cover teammates from elevated/rear position
	ROLE_SUPPORT        // general support, fallback role
} aiRole_t;

//
// Communication message
//
typedef struct ai_comm_msg_s {
	commMsgType_t   type;
	int             senderNum;
	int             targetNum;      // -1 for broadcast
	vec3_t          pos;            // relevant position (enemy pos, cover pos, etc)
	int             timestamp;
	int             enemyNum;       // enemy entity number if relevant
} ai_comm_msg_t;

//
// Ring buffer for team communication
//
#define COMM_RING_SIZE      32
#define COMM_RANGE          768.0f  // range for comm messages
#define COMM_MSG_LIFETIME   5000    // messages expire after 5 seconds

typedef struct ai_comm_channel_s {
	ai_comm_msg_t   messages[COMM_RING_SIZE];
	int             writeIndex;
	int             count;
} ai_comm_channel_t;

// one channel per team
#define MAX_AI_TEAMS    8

//
// Public API
//
void AICast_Comm_Init( void );
void AICast_Comm_BroadcastMessage( struct cast_state_s *cs, commMsgType_t type, vec3_t pos, int enemyNum );
void AICast_Comm_SendMessage( struct cast_state_s *cs, int targetNum, commMsgType_t type, vec3_t pos, int enemyNum );
void AICast_Comm_ProcessMessages( struct cast_state_s *cs );
int  AICast_Comm_FindLeader( struct cast_state_s *cs, float range );
void AICast_Comm_AssignRole( struct cast_state_s *cs, int targetNum, aiRole_t role );

#endif // AI_CAST_COMM_H
