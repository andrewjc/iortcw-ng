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
// Name:			ai_cast_cover.h
// Function:		Geometry-aware Cover System
// Programmer:		AI Uplift
// Tab Size:		4 (real tabs)
//===========================================================================

#ifndef AI_CAST_COVER_H
#define AI_CAST_COVER_H

struct cast_state_s;

//
// Cover node - a position that has been confirmed as good cover
//
#define MAX_COVER_NODES         128
#define COVER_NODE_EXPIRE_TIME  60000   // 60 seconds before a node is re-evaluated
#define COVER_SAMPLE_COUNT      12      // number of candidate positions to sample
#define COVER_PEEK_ANGLE        30.0f   // degrees to lean from cover for peek

typedef struct coverNode_s {
	vec3_t      origin;
	int         useCount;       // how many times agents used this successfully
	int         lastUsedTime;
	int         occupiedByNum;  // entitynum of agent currently using it, -1 if free
	float       quality;        // cached quality score (0-1)
	qboolean    active;
} coverNode_t;

//
// Cover evaluation result
//
typedef struct coverSpot_s {
	vec3_t      origin;
	float       score;          // combined quality score
	qboolean    hasPeekAngle;   // can peek at primary threat
	vec3_t      peekDir;        // direction to peek for a shot
	float       wallDepth;      // thickness of cover wall
} coverSpot_t;

//
// Public API
//
void AICast_Cover_Init( void );
qboolean AICast_FindBestCoverSpot( struct cast_state_s *cs, vec3_t outPos );
qboolean AICast_IsInCover( struct cast_state_s *cs );
void AICast_Cover_ReleaseCoverNode( struct cast_state_s *cs );
void AICast_Cover_RegisterNode( vec3_t origin, float quality );
qboolean AICast_Cover_GetPeekPos( struct cast_state_s *cs, vec3_t outPeekPos );

#endif // AI_CAST_COVER_H
