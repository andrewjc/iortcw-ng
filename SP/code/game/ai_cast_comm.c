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
// Name:			ai_cast_comm.c
// Function:		AI Communication & Coordination System
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

//
// Communication channels (one per team)
//
static ai_comm_channel_t commChannels[MAX_AI_TEAMS];

/*
============
AICast_Comm_Init

  Called at level start
============
*/
void AICast_Comm_Init( void ) {
	memset( commChannels, 0, sizeof( commChannels ) );
}

/*
============
AICast_Comm_GetTeamChannel

  Map a cast_state_t to its team's communication channel.
  Uses the ai_cast.h AITEAM_* enum values.
============
*/
static ai_comm_channel_t *AICast_Comm_GetTeamChannel( cast_state_t *cs ) {
	int team;
	gentity_t *ent = &g_entities[cs->entityNum];

	team = ent->aiTeam;
	if ( team < 0 || team >= MAX_AI_TEAMS ) {
		team = 0;
	}

	return &commChannels[team];
}

/*
============
AICast_Comm_PushMessage

  Push a message into a team's ring buffer
============
*/
static void AICast_Comm_PushMessage( ai_comm_channel_t *channel, ai_comm_msg_t *msg ) {
	int idx = channel->writeIndex % COMM_RING_SIZE;
	channel->messages[idx] = *msg;
	channel->writeIndex++;
	if ( channel->count < COMM_RING_SIZE ) {
		channel->count++;
	}
}

/*
============
AICast_Comm_BroadcastMessage

  Broadcast a message to all teammates within range
============
*/
void AICast_Comm_BroadcastMessage( cast_state_t *cs, commMsgType_t type, vec3_t pos, int enemyNum ) {
	ai_comm_channel_t *channel;
	ai_comm_msg_t msg;

	channel = AICast_Comm_GetTeamChannel( cs );

	memset( &msg, 0, sizeof( msg ) );
	msg.type = type;
	msg.senderNum = cs->entityNum;
	msg.targetNum = -1; // broadcast
	VectorCopy( pos, msg.pos );
	msg.timestamp = level.time;
	msg.enemyNum = enemyNum;

	AICast_Comm_PushMessage( channel, &msg );

	// play order sound if this is a leadership communication
	if ( type == COMM_ENEMY_SPOTTED || type == COMM_NEED_HELP ) {
		gentity_t *ent = &g_entities[cs->entityNum];
		cast_state_t *entCs = AICast_GetCastState( cs->entityNum );
		if ( entCs ) {
			AICast_ScriptEvent( entCs, "order", "" );
		}
		(void)ent;
	}
}

/*
============
AICast_Comm_SendMessage

  Send a directed message to a specific entity
============
*/
void AICast_Comm_SendMessage( cast_state_t *cs, int targetNum, commMsgType_t type, vec3_t pos, int enemyNum ) {
	ai_comm_channel_t *channel;
	ai_comm_msg_t msg;

	channel = AICast_Comm_GetTeamChannel( cs );

	memset( &msg, 0, sizeof( msg ) );
	msg.type = type;
	msg.senderNum = cs->entityNum;
	msg.targetNum = targetNum;
	VectorCopy( pos, msg.pos );
	msg.timestamp = level.time;
	msg.enemyNum = enemyNum;

	AICast_Comm_PushMessage( channel, &msg );
}

/*
============
AICast_Comm_ProcessMessages

  Each agent calls this each think tick to process messages
============
*/
void AICast_Comm_ProcessMessages( cast_state_t *cs ) {
	ai_comm_channel_t *channel;
	int i, idx;
	ai_comm_msg_t *msg;
	gentity_t *ent = &g_entities[cs->entityNum];

	channel = AICast_Comm_GetTeamChannel( cs );

	for ( i = 0; i < channel->count; i++ ) {
		idx = ( channel->writeIndex - channel->count + i ) % COMM_RING_SIZE;
		if ( idx < 0 ) {
			idx += COMM_RING_SIZE;
		}
		msg = &channel->messages[idx];

		// skip expired messages
		if ( level.time - msg->timestamp > COMM_MSG_LIFETIME ) {
			continue;
		}

		// skip our own messages
		if ( msg->senderNum == cs->entityNum ) {
			continue;
		}

		// skip messages not for us (directed messages)
		if ( msg->targetNum >= 0 && msg->targetNum != cs->entityNum ) {
			continue;
		}

		// range check for broadcasts
		if ( msg->targetNum < 0 ) {
			float dist = Distance( ent->r.currentOrigin, g_entities[msg->senderNum].r.currentOrigin );
			if ( dist > COMM_RANGE ) {
				continue;
			}
		}

		// process the message based on type
		switch ( msg->type ) {
		case COMM_ENEMY_SPOTTED:
			// update our visibility info for the reported enemy
			if ( msg->enemyNum >= 0 && msg->enemyNum < level.maxclients ) {
				if ( !( cs->vislist[msg->enemyNum].flags & AIVIS_ENEMY ) ) {
					VectorCopy( msg->pos, cs->vislist[msg->enemyNum].visible_pos );
					cs->vislist[msg->enemyNum].visible_timestamp = level.time;
				}
			}
			break;

		case COMM_UNDER_FIRE:
			// teammate is under fire - increase alertness
			if ( cs->aiState < AISTATE_COMBAT ) {
				AICast_StateChange( cs, AISTATE_ALERT );
			}
			break;

		case COMM_NEED_HELP:
			// teammate needs help - if we're not busy, move towards them
			if ( cs->aiState < AISTATE_COMBAT && cs->squadRole == ROLE_NONE ) {
				cs->squadRole = ROLE_SUPPORT;
			}
			break;

		case COMM_GOING_FLANK:
			// teammate is flanking - if we have no role, start suppressing
			if ( cs->squadRole == ROLE_NONE && cs->enemyNum >= 0 ) {
				cs->squadRole = ROLE_PINDOWN;
			}
			break;

		case COMM_GOING_COVER:
			// teammate is taking cover - be aware
			break;

		case COMM_CLEAR:
			// area clear - can relax
			break;

		case COMM_GRENADE_OUT:
			// teammate threw a grenade - be aware of position
			break;

		case COMM_RETREATING:
			// teammate is retreating - consider covering them
			if ( cs->squadRole == ROLE_NONE ) {
				cs->squadRole = ROLE_OVERWATCH;
			}
			break;

		case COMM_SUPPRESSING:
			// teammate is suppressing - good time to flank if we have the role
			break;

		default:
			break;
		}
	}
}

/*
============
AICast_Comm_FindLeader

  Find the best leader among nearby teammates.
  Returns entity number of leader, or -1 if none found.
============
*/
int AICast_Comm_FindLeader( cast_state_t *cs, float range ) {
	int i, bestLeader = -1;
	float bestLeaderScore = -1.0f;
	gentity_t *ent = &g_entities[cs->entityNum];

	for ( i = 0; i < level.maxclients; i++ ) {
		cast_state_t *other;
		float dist, leaderScore;

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
		if ( dist > range ) {
			continue;
		}

		leaderScore = other->attributes[LEADER];
		if ( leaderScore > bestLeaderScore ) {
			bestLeaderScore = leaderScore;
			bestLeader = i;
		}
	}

	return bestLeader;
}

/*
============
AICast_Comm_AssignRole

  Assign a combat role to a teammate
============
*/
void AICast_Comm_AssignRole( cast_state_t *cs, int targetNum, aiRole_t role ) {
	cast_state_t *target;

	if ( targetNum < 0 || targetNum >= level.maxclients ) {
		return;
	}

	target = AICast_GetCastState( targetNum );
	if ( !target || !target->bs ) {
		return;
	}

	target->squadRole = role;
}
