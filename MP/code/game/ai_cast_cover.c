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
// Name:			ai_cast_cover.c
// Function:		Geometry-aware Cover System
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
#include "ai_cast_cover.h"

//
// Global cover node table
//
static coverNode_t coverNodes[MAX_COVER_NODES];
static int numCoverNodes = 0;

/*
============
AICast_Cover_Init

  Called at level start
============
*/
void AICast_Cover_Init( void ) {
	memset( coverNodes, 0, sizeof( coverNodes ) );
	numCoverNodes = 0;
}

/*
============
AICast_Cover_RegisterNode

  Register a position as a known-good cover node
============
*/
void AICast_Cover_RegisterNode( vec3_t origin, float quality ) {
	int i;
	int oldest = 0;
	int oldestTime = level.time;

	// check if this position already exists (within tolerance)
	for ( i = 0; i < numCoverNodes; i++ ) {
		if ( coverNodes[i].active && Distance( coverNodes[i].origin, origin ) < 64.0f ) {
			// update existing
			coverNodes[i].useCount++;
			coverNodes[i].lastUsedTime = level.time;
			if ( quality > coverNodes[i].quality ) {
				coverNodes[i].quality = quality;
			}
			return;
		}
		if ( coverNodes[i].active && coverNodes[i].lastUsedTime < oldestTime ) {
			oldestTime = coverNodes[i].lastUsedTime;
			oldest = i;
		}
	}

	// add new node
	if ( numCoverNodes < MAX_COVER_NODES ) {
		i = numCoverNodes++;
	} else {
		// replace oldest
		i = oldest;
	}

	VectorCopy( origin, coverNodes[i].origin );
	coverNodes[i].useCount = 1;
	coverNodes[i].lastUsedTime = level.time;
	coverNodes[i].occupiedByNum = -1;
	coverNodes[i].quality = quality;
	coverNodes[i].active = qtrue;
}

/*
============
AICast_Cover_ReleaseCoverNode

  Release the cover node an agent is occupying
============
*/
void AICast_Cover_ReleaseCoverNode( cast_state_t *cs ) {
	int i;
	for ( i = 0; i < numCoverNodes; i++ ) {
		if ( coverNodes[i].active && coverNodes[i].occupiedByNum == cs->entityNum ) {
			coverNodes[i].occupiedByNum = -1;
		}
	}
	cs->aiFlags &= ~AIFL_IN_COVER;
}

/*
============
Cover_ScoreCandidate

  Score a candidate cover position based on multiple factors
============
*/
static float Cover_ScoreCandidate( cast_state_t *cs, vec3_t candidatePos, vec3_t enemyPos ) {
	float score = 0.0f;
	trace_t trace;
	vec3_t dir, right, wallTestEnd;
	float dist;
	int i;
	gentity_t *ent = &g_entities[cs->entityNum];

	// (a) Is the position blocked from enemy line of sight?
	trap_Trace( &trace, candidatePos, NULL, NULL, enemyPos, cs->entityNum, CONTENTS_SOLID );
	if ( trace.fraction < 1.0f ) {
		// enemy can't see this position - good cover!
		score += 0.4f;
	} else {
		// partially exposed
		score += 0.05f;
	}

	// (b) Check for solid geometry behind (wall depth)
	VectorSubtract( enemyPos, candidatePos, dir );
	VectorNormalize( dir );
	// trace away from enemy to check for backing wall
	VectorMA( candidatePos, -64.0f, dir, wallTestEnd );
	trap_Trace( &trace, candidatePos, NULL, NULL, wallTestEnd, cs->entityNum, CONTENTS_SOLID );
	if ( trace.fraction < 1.0f ) {
		float wallDepth = 64.0f * ( 1.0f - trace.fraction );
		score += 0.1f + ( wallDepth / 64.0f ) * 0.15f;
	}

	// (c) Peek opportunity - check if we can peek around to get a shot
	{
		vec3_t up = { 0, 0, 1 };
		CrossProduct( dir, up, right );
	}
	VectorNormalize( right );
	{
		vec3_t peekPos;
		VectorMA( candidatePos, 48.0f, right, peekPos );
		trap_Trace( &trace, peekPos, NULL, NULL, enemyPos, cs->entityNum, CONTENTS_SOLID );
		if ( trace.fraction >= 0.9f ) {
			score += 0.15f; // good peek angle
		}
	}

	// (d) Distance scoring - prefer positions that aren't too far
	dist = Distance( ent->r.currentOrigin, candidatePos );
	if ( dist < 128.0f ) {
		score += 0.15f; // close, quick to reach
	} else if ( dist < 256.0f ) {
		score += 0.1f;
	} else if ( dist > 512.0f ) {
		score -= 0.1f; // too far to reach quickly
	}

	// (e) Not already occupied by a teammate
	for ( i = 0; i < numCoverNodes; i++ ) {
		if ( coverNodes[i].active &&
			 coverNodes[i].occupiedByNum >= 0 &&
			 coverNodes[i].occupiedByNum != cs->entityNum &&
			 Distance( coverNodes[i].origin, candidatePos ) < 64.0f ) {
			score -= 0.3f;
			break;
		}
	}

	// (f) Bonus for known-good cover nodes nearby
	for ( i = 0; i < numCoverNodes; i++ ) {
		if ( coverNodes[i].active &&
			 Distance( coverNodes[i].origin, candidatePos ) < 48.0f ) {
			score += coverNodes[i].quality * 0.1f;
			score += ( coverNodes[i].useCount > 5 ? 5 : coverNodes[i].useCount ) * 0.02f;
			break;
		}
	}

	return score;
}

/*
============
AICast_FindBestCoverSpot

  Sample candidate positions and return the best cover position.
  Returns qtrue if a good spot was found.
============
*/
qboolean AICast_FindBestCoverSpot( cast_state_t *cs, vec3_t outPos ) {
	int i;
	float bestScore = -1.0f;
	vec3_t bestPos;
	gentity_t *ent = &g_entities[cs->entityNum];
	vec3_t enemyPos;
	float angleStep;

	if ( cs->enemyNum < 0 ) {
		return qfalse;
	}

	VectorCopy( g_entities[cs->enemyNum].r.currentOrigin, enemyPos );

	angleStep = 360.0f / COVER_SAMPLE_COUNT;
	VectorClear( bestPos );

	for ( i = 0; i < COVER_SAMPLE_COUNT; i++ ) {
		vec3_t candidatePos, dir;
		float angle, score;
		float sampleDist;
		trace_t trace;

		angle = i * angleStep + ( random() * angleStep * 0.5f );
		dir[0] = cos( DEG2RAD( angle ) );
		dir[1] = sin( DEG2RAD( angle ) );
		dir[2] = 0;

		// try multiple distances
		for ( sampleDist = 96.0f; sampleDist <= 320.0f; sampleDist += 112.0f ) {
			VectorMA( ent->r.currentOrigin, sampleDist, dir, candidatePos );

			// make sure position is reachable (basic ground check)
			candidatePos[2] += 16.0f;
			trap_Trace( &trace, candidatePos, ent->r.mins, ent->r.maxs, candidatePos, cs->entityNum, MASK_PLAYERSOLID );
			candidatePos[2] -= 16.0f;

			if ( trace.startsolid || trace.allsolid ) {
				continue;
			}

			// check that there is ground
			{
				vec3_t groundTest;
				VectorCopy( candidatePos, groundTest );
				groundTest[2] -= 64.0f;
				trap_Trace( &trace, candidatePos, NULL, NULL, groundTest, cs->entityNum, MASK_PLAYERSOLID );
				if ( trace.fraction >= 1.0f ) {
					continue;   // no ground
				}
				// adjust to ground
				VectorCopy( trace.endpos, candidatePos );
				candidatePos[2] += 1.0f;
			}

			score = Cover_ScoreCandidate( cs, candidatePos, enemyPos );

			if ( score > bestScore ) {
				bestScore = score;
				VectorCopy( candidatePos, bestPos );
			}
		}
	}

	if ( bestScore > 0.2f ) {
		VectorCopy( bestPos, outPos );

		// register this as a cover node
		AICast_Cover_RegisterNode( bestPos, bestScore );

		// mark as occupied
		{
			int j;
			for ( j = 0; j < numCoverNodes; j++ ) {
				if ( coverNodes[j].active && Distance( coverNodes[j].origin, bestPos ) < 48.0f ) {
					coverNodes[j].occupiedByNum = cs->entityNum;
					break;
				}
			}
		}

		cs->aiFlags |= AIFL_IN_COVER;
		return qtrue;
	}

	// fallback to the original cover system
	return AICast_GetTakeCoverPos( cs, cs->enemyNum,
								   g_entities[cs->enemyNum].r.currentOrigin, outPos );
}

/*
============
AICast_IsInCover

  Check if the agent is currently in a good cover position
============
*/
qboolean AICast_IsInCover( cast_state_t *cs ) {
	trace_t trace;

	if ( !( cs->aiFlags & AIFL_IN_COVER ) ) {
		return qfalse;
	}

	if ( cs->enemyNum < 0 ) {
		return qfalse;
	}

	// verify by tracing to enemy
	trap_Trace( &trace, g_entities[cs->entityNum].r.currentOrigin,
				NULL, NULL, g_entities[cs->enemyNum].r.currentOrigin,
				cs->entityNum, CONTENTS_SOLID );

	return ( trace.fraction < 0.9f );
}

/*
============
AICast_Cover_GetPeekPos

  Get a position near the current cover to peek out and fire
============
*/
qboolean AICast_Cover_GetPeekPos( cast_state_t *cs, vec3_t outPeekPos ) {
	vec3_t dir, right, peekPos;
	trace_t trace;

	if ( cs->enemyNum < 0 ) {
		return qfalse;
	}

	VectorSubtract( g_entities[cs->enemyNum].r.currentOrigin,
					g_entities[cs->entityNum].r.currentOrigin, dir );
	VectorNormalize( dir );

	{
		vec3_t up = { 0, 0, 1 };
		CrossProduct( dir, up, right );
	}
	VectorNormalize( right );

	// try right peek
	VectorMA( g_entities[cs->entityNum].r.currentOrigin, 48.0f, right, peekPos );
	trap_Trace( &trace, peekPos, NULL, NULL,
				g_entities[cs->enemyNum].r.currentOrigin,
				cs->entityNum, CONTENTS_SOLID );

	if ( trace.fraction >= 0.9f ) {
		VectorCopy( peekPos, outPeekPos );
		return qtrue;
	}

	// try left peek
	VectorMA( g_entities[cs->entityNum].r.currentOrigin, -48.0f, right, peekPos );
	trap_Trace( &trace, peekPos, NULL, NULL,
				g_entities[cs->enemyNum].r.currentOrigin,
				cs->entityNum, CONTENTS_SOLID );

	if ( trace.fraction >= 0.9f ) {
		VectorCopy( peekPos, outPeekPos );
		return qtrue;
	}

	return qfalse;
}
