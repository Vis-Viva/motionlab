#include "cbase.h"
#include "movevars_shared.h" // GetCurrentGravity
#include "coordsize.h"       // COORD_RESOLUTION, DIST_EPSILON
#include "mathlib/mathlib.h"
#include "ml_motiondriver.h"

#include "tier0/memdbgon.h"

MotionDriver::MotionDriver()  = default;
MotionDriver::~MotionDriver() = default;


// ------------------------------------------------------------------------------------------------
// NECESSARY ANCILLARY SOURCE OVERRIDES
// ------------------------------------------------------------------------------------------------
// This stuff is just necessary engine housekeeping which needed to be tweaked to avoid interfering
// with our authority over movement. Should NOT be considered a part of our custom movement logic.


// Identical to Source's CheckParameters except with movement clamping removed.
void MotionDriver::CheckParameters( void )
{
	QAngle v_angle;

	// Zero out movement magnitudes if we can't move
	if ( player->GetFlags() & FL_FROZEN || IsDead() )
	{
		mv->m_flForwardMove = 0;
		mv->m_flSideMove    = 0;
		mv->m_flUpMove      = 0;
	}

	// View punch decay and angle setup (visual/aim housekeeping).
	DecayPunchAngle();

	if ( !IsDead() )
	{
		v_angle = mv->m_vecAngles;
		v_angle = v_angle + player->m_Local.m_vecPunchAngle;

		// Set roll for visual effect in modes where Source would normally apply it.
		if ( player->GetMoveType() != MOVETYPE_ISOMETRIC &&
			 player->GetMoveType() != MOVETYPE_NOCLIP )
		{
			mv->m_vecAngles[ROLL] = CalcRoll( v_angle, mv->m_vecVelocity, 
											  sv_rollangle.GetFloat(), sv_rollspeed.GetFloat() );
		}
		else
		{
			mv->m_vecAngles[ROLL] = 0.0f;
		}

		mv->m_vecAngles[PITCH] = v_angle[PITCH];
		mv->m_vecAngles[YAW]   = v_angle[YAW];
	}
	else
	{
		mv->m_vecAngles = mv->m_vecOldAngles;
	}

	// Dead-player camera offset.
	if ( IsDead() )
	{
		player->SetViewOffset( VEC_DEAD_VIEWHEIGHT_SCALED( player ) );
	}

	// Normalize yaw to [-180, 180]
	if ( mv->m_vecAngles[YAW] > 180.0f )
	{
		mv->m_vecAngles[YAW] -= 360.0f;
	}
}


// ------------------------------------------------------------------------------------------------
// END ANCILLARY SOURCE OVERRIDES
// ------------------------------------------------------------------------------------------------


// Reset various fields to defaults so we start with a clean state, no contamination between players/ticks
void MotionDriver::ClearState()
{
	// TODO: Implement me once we know exactly what needs to be reset/cleared
}


// These are used for moving the serverside player physics shadow and must be zeroed on tick entry
void MotionDriver::ResetPhysAccumulators()
{
	mv->m_outWishVel.Init();
	mv->m_outJumpVel.Init();
}


// Engine housekeeping. Not really part of our movement logic
void MotionDriver::SpaghettiContainment()
{
	CheckParameters();
	ResetPhysAccumulators();
	MoveHelper()->ResetTouchList();
	ReduceTimers();
}


// Derives fwd, right, up movement axes in world space from current player view dir
void MotionDriver::UpdateMovementAxes()
{
	// Ugly native Source names stay here for engine compatibility, don't use these
	AngleVectors( mv->m_vecViewAngles, &m_vecForward, &m_vecRight, &m_vecUp );
	// PlayerState is the preferred way to interface with move axes
	MLPlayer.UpdateMovementAxes();
}


// Wrapper for internal Source logic to determine if the player is able to move
bool MotionDriver::PlayerIsStuck()
{
	if ( !player->pl.deadflag )
	{
		if ( CheckInterval( STUCK ) )
		{
			return CheckStuck(); // TODO: Does this interfere with movement authority?
		}
	}
	return false;
}


bool MotionDriver::TraceHitEntity( const hulltrace& tr ) const
{
	return tr.m_pEnt != NULL;  // dude
}


// Useful when we need to pull phys properties from a trace's contact surface
surfacedata* MotionDriver::GetTraceSurfaceData( const hulltrace& tr ) const
{
	IPhysicsSurfaceProps *physprops = MoveHelper()->GetSurfaceProps();
	if ( physprops )
	{
		return physprops->GetSurfaceData( tr.surface.surfaceProps );
	}
	return NULL;
}


// If player's standing surface material has changed, update it for any surface triggers that consume it
void MotionDriver::UpdatePlayerGameMaterial( const hulltrace& groundTr )
{
	surfacedata *currentSrfData = GetTraceSurfaceData( groundTr );
	char         currentGameMat = currentSrfData ? currentSrfData->game.material : 0;
	char         prevGameMat    = MLPlayer.PreviousTextureType();

	if ( MLPlayer.CurrentGroundEntity() == NULL )
	{
		currentGameMat = 0;
	}

	// Changed?
	if ( prevGameMat != currentGameMat )
	{
		CEnvPlayerSurfaceTrigger::SetPlayerSurface( player, currentGameMat );
	}

	MLPlayer.UpdateTextureType( currentGameMat );
}


// Is this plane horizontal enough to stand on?
bool MotionDriver::PlaneIsStandable( const plane& pl ) const
{
	return DotProduct( pl.normal, WORLD_UP ) >= GROUND_MIN_DOT;
}


CBaseEntity* MotionDriver::GetTraceCollisionEntity( const hulltrace* tr ) const
{
	return tr ? tr->m_pEnt : NULL;
}


// Modifies player base velocity appropriately when landing/leaving ground
// TODO: Make this a PlayerState method probably?
void MotionDriver::HandleGroundTransitionVel( CBaseEntity* oldGround, CBaseEntity* newGround )
{
	Vector newBaseVel = MLPlayer.CurrentBaseVelocity();

	if ( !oldGround && newGround )
	{
		// Subtract ground velocity at instant we hit ground
		newBaseVel  -= newGround->GetAbsVelocity();
		newBaseVel.z = newGround->GetAbsVelocity().z;
	}
	else if ( oldGround && !newGround )
	{
		// Add in ground velocity at instant we started jumping
		newBaseVel  += oldGround->GetAbsVelocity();
		newBaseVel.z = oldGround->GetAbsVelocity().z;
	}

	MLPlayer.UpdateBaseVelocity( newBaseVel );
}


float MotionDriver::GetTraceFriction( const hulltrace* tr ) const
{
    if ( !tr )
        return 1.0f;  // default friction
    
    surfacedata* surfData = GetTraceSurfaceData( *tr );
    return surfData ? surfData->physics.friction : 1.0f;
}


// Store ground properties & set jump gate for force calculation later
// TODO: Make these trace util funcs standalones in ml_defs and move this whole function to PlayerState
void MotionDriver::UpdateGrounding( const hulltrace* groundTr )
{
	// Not grounded
	if ( !groundTr || !TraceHitEntity( *groundTr ) )
	{
		MLPlayer.CurrentGroundFriction = 1.0f;
		MLPlayer.CurrentGroundNormal   = WORLD_UP;
		MLPlayer.IsGrounded            = false;
		MLPlayer.CanJump               = false;
		return;
	}

	// Trace found standable ground (NOTE: Normal has already been checked by now)
	MLPlayer.CurrentGroundFriction = GetTraceFriction( groundTr );
	MLPlayer.CurrentGroundNormal   = groundTr->plane.normal;
	MLPlayer.IsGrounded            = true;
	MLPlayer.CanJump               = true;
}


bool MotionDriver::RegisterTouch( const hulltrace& tr, const Vector& collisionVel )
{
	return MoveHelper()->AddToTouched( tr, collisionVel );
}


// Does typical Source grounding ops, minus the zeroing of z vel, plus some ML-specific stuff
// TODO: Make this a PlayerState method probably??
void MotionDriver::SetGroundEntity( const hulltrace *groundTr )
{
	UpdateGrounding( groundTr );  // ML-specific logic

	CBaseEntity *oldGround = MLPlayer.CurrentGroundEntity();
	CBaseEntity *newGround = GetTraceCollisionEntity( groundTr );
	
	HandleGroundTransitionVel( oldGround, newGround );
	MLPlayer.UpdateGroundEntity( newGround );

	// If we are on something, categorize surface and record touch
	if ( newGround && groundTr )
	{
		CategorizeGroundSurface( *groundTr );
		player->m_flWaterJumpTime = 0;

		// Signal that we touched an object if we're standing on a non-world entity
		if ( !groundTr->DidHitWorld() ) 
		{
				RegisterTouch( *groundTr, MLPlayer.CurrentVelocity() );
		}

		//mv->m_vecVelocity.z = 0.0f;
	}
}


// Does downward hull tracing to look for a standable entity under the player, updates related properties
void MotionDriver::CategorizePosition( void )
{

	// Reset friction to default every time we recategorize (prevents bogus friction in certain edge cases)
	MLPlayer.ResetFriction();

	// observers don't have a ground entity
	if ( MLPlayer.IsObserver() )
	{
		return;
	}

	// Initial simple short downward trace
	Vector    currentPos = MLPlayer.CurrentPosition();
	Vector 	  endPoint   = Vector( currentPos.x, currentPos.y, currentPos.z - VERT_PROBE_DIST );
	hulltrace groundTr;
	TryTouchGround( currentPos, endPoint, GetPlayerMins(), GetPlayerMaxs(), 
					MASK_PLAYERSOLID, COLLISION_GROUP_PLAYER_MOVEMENT, groundTr );

	// If ground trace fails to find something standable, retry with a quadrant trace fallback
	if ( !TraceHitEntity( groundTr ) || !PlaneIsStandable( groundTr.plane ) )
	{
		// Test four sub-boxes, to see if any of them would have found shallower slope we could actually stand on
		TryTouchGroundInQuadrants( currentPos, endPoint, 
								   MASK_PLAYERSOLID, COLLISION_GROUP_PLAYER_MOVEMENT, groundTr );

		// Fallback still finds nothing standable, defintely not on ground
		if ( !TraceHitEntity( groundTr ) || !PlaneIsStandable( groundTr.plane ) )
		{
			SetGroundEntity( NULL );
		}
		else  // Fallback found something standable, neat
		{
			SetGroundEntity( &groundTr );
		}
	}
	else // Original ground trace found a standable entity
	{
		SetGroundEntity( &groundTr );
	}

	// On server side, need to update player's surface material for phys listeners if changed
	#ifndef CLIENT_DLL
		UpdatePlayerGameMaterial( groundTr );
	#endif
}


// Last bit of tick-entry engine housekeeping
void MotionDriver::MoreSpaghettiContainment()
{
	MLPlayer.RecordFallVelocity();
	m_nOnLadder = 0;
	MLPlayer.UpdateStepSound();
}


// Amount of upward velocity we get by applying one tick of jump force
float MotionDriver::GetJumpImpulseVel() const
{
	return ( MLPlayer.JumpForce / MLPlayer.Mass ) * FRAMETIME;
}


// Source VPhys bookkeeping - update serverside phys shadow to match player jumps
void MotionDriver::VPhysJump()
{
	if( MLPlayer.CanJump && GetJumpInput() )
	{
		mv->m_outJumpVel.z  += GetJumpImpulseVel();
		mv->m_outStepHeight += 0.15f;
	}
}


// Source VPhys bookkeeping - update planar push force serverside phys shadow applies to props
void MotionDriver::UpdateVPhysVel()
{
	Vector wishForce   = CurrentWASDForce + CurrentFrictionForce; wishForce.z = 0.0f;
	Vector wishAccel   = wishForce / MLPlayer.Mass;
	Vector wishDelta   = wishAccel * FRAMETIME;
	mv->m_outWishVel  += wishDelta;
	mv->m_outWishVel.z = 0.0f;
}


// F = ma -> a = F/m -> dv = a*dt
void MotionDriver::Accelerate()
{
	Vector acceleration = CurrentNetForce * ( 1.0f / MLPlayer.Mass );
	Vector deltaVel     = acceleration * FRAMETIME;
	Vector newVel       = MLPlayer.CurrentVelocity() + deltaVel;
	if ( newVel.Length() < MIN_VEL )
	{
		newVel.Zero();  // do not do Zeno paradox
	}
	MLPlayer.UpdateVelocity( newVel );	
}


// For movement ops, trace solidmask & collisiongroup args are always the same - less boilerplate = more good
void MotionDriver::TracePlayerMovementBBox( const Vector& startPos, const Vector& targetPos, hulltrace& outTr ) const
{
	TracePlayerBBox( startPos, targetPos, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, outTr );
}


// Handles a known float precision bug where a trace can 'successfully' complete but still be stuck in terrain
bool MotionDriver::CheckTraceStuck( const hulltrace& tr ) const
{
	hulltrace struckTr;
	TracePlayerMovementBBox( tr.endpos, tr.endpos, struckTr );
	return ( struckTr.startsolid || struckTr.fraction != 1.0f );
}


// Just some gating checks for common things that make a slide trace unusable
bool MotionDriver::CheckSlideTraceInvalid( const hulltrace& slideTr ) const
{
	return ( slideTr.allsolid || ( slideTr.fraction == 1.0f && CheckTraceStuck( slideTr ) ) );
}


// Deflect velocity along plane indicated by normal
Vector MotionDriver::DeflectVelocity( const Vector& currentVel, const Vector& normal, float overbounce )
{
	// Determine how far along plane to deflect based on incoming direction
	float  intoSurface = DotProduct( currentVel, normal );
	float  backoff     = intoSurface * overbounce;
	Vector deflectedV  = currentVel - ( normal * backoff );

	// Guard against float noise causing residual velocity into plane
	float adjust = DotProduct( deflectedV, normal );
	if ( adjust < 0.0f )
	{
		deflectedV -= ( normal * adjust );
	}

	return deflectedV;
}


// Simple p₀+vt slide, returns true if slide completes cleanly (no collisions), else false
bool MotionDriver::Slide()
{
	// Accumulate collision plane normals to constrain velocity redirections
	CUtlVectorFixed<Vector, MAX_CLIP_PLANES> planeNormals;
	float  totalFraction    = 0.0f;
	float  timeLeft         = FRAMETIME;
	bool   cleanSlide       = true;
	Vector originalStartVel = MLPlayer.CurrentVelocity();
	Vector segmentStartVel  = MLPlayer.CurrentVelocity();
	
	for ( int bumpCount=0; bumpCount < MAX_BUMPS; bumpCount++ )
	{
		if ( GetCurrentSpeed() == 0.0f )  // nothing to slide if we're not moving
		{
			break;
		}
		
		hulltrace slideTr;
		Vector    endPos; 
		VectorMA( MLPlayer.CurrentPosition(), timeLeft, MLPlayer.CurrentVelocity(), endPos );
		TracePlayerMovementBBox( MLPlayer.CurrentPosition(), endPos, slideTr );
		totalFraction += slideTr.fraction;

		// First make sure we can actually use this trace for a slide
		if ( CheckSlideTraceInvalid( slideTr ) )
		{
			MLPlayer.ZeroVelocity();  // :(
			cleanSlide = false;
			break;
		}
		
		// Ok the trace is usable, now what does it tell us?
		if ( slideTr.fraction > 0.0f )  // hooray we went somewhere
		{
			MLPlayer.UpdatePosition( slideTr.endpos );
			if ( slideTr.fraction == 1.0f )  // We made it all the way to the end, we're done
			{
				break;
			}
			// Only made it part way, get ready to handle collisions
			planeNormals.RemoveAll();
			segmentStartVel = MLPlayer.CurrentVelocity();
		}
		
		// Didn't break above, must have bumped into something - record touch and handle collision
		RegisterTouch( slideTr, MLPlayer.CurrentVelocity() );          // Source bookkeeping for vphys, surface triggers, etc
		float timeTravelled = timeLeft * slideTr.fraction;  // Amount of timestep consumed before collision
		timeLeft           -= timeTravelled;
		cleanSlide          = false;
		Vector bumpNormal   = slideTr.plane.normal;

		// Hit too many planes - we're stuck, zero vel & return false - this shouldn't really happen but whatev
		if ( planeNormals.Count() >= MAX_CLIP_PLANES )
		{
			MLPlayer.ZeroVelocity();
			break;
		}

		// Add plane to contact list, look for unobstructed deflection path off current planes
		planeNormals.AddToTail( bumpNormal );
		Vector newVel    = Vector( 0.0f, 0.0f, 0.0f );
		bool   pathFound = false;
		for ( int i=0; i < planeNormals.Count(); ++i )
		{
			Vector candidateVel = DeflectVelocity( segmentStartVel, planeNormals[i], OVERCLIP );
			bool   clearDeflect = true;
			
			for ( int j=0; j < planeNormals.Count(); ++j )
			{
				if ( j==i )
				{
					continue;
				}
				if ( DotProduct( candidateVel, planeNormals[j] ) < 0.0f )  // Got deflected into secondary plane
				{
					clearDeflect = false;
					break;
				}
			}

			if ( clearDeflect )  // No secondary plane encountered, this deflection works
			{
				newVel    = candidateVel;
				pathFound = true;
				break;
			}
		}

		if ( !pathFound )  // We got deflected into something
		{
			if ( planeNormals.Count() == 2 )  // "Something" = "crease between two planes"
			{
				Vector creaseDir = CrossProduct( planeNormals[0], planeNormals[1] );
				VectorNormalize( creaseDir );
				newVel = creaseDir * DotProduct( creaseDir, segmentStartVel );  // Deflect vel along crease 
			}
			else  // We're hitting > 2 planes, prob stuck in a corner, zero vel and be sad
			{
				MLPlayer.ZeroVelocity();
				break;
			}
		}
		
		// Guard against velocity reversal from deflections to prevent oscillations in corners
		if ( DotProduct( newVel, originalStartVel ) <= 0.0f )
		{
			MLPlayer.ZeroVelocity();
			break;
		}
		
		MLPlayer.UpdateVelocity( newVel );  // Apply deflected velocity and continue
	}

	// No progress across all bumps => no movement => sad
	if ( totalFraction <= 0.0f )
	{
		MLPlayer.ZeroVelocity();
	}

	return cleanSlide;
}


// For straight up/down traces frequently needed for stepping/probing ops
void MotionDriver::TraceStep( const Vector& start, float signedDist, hulltrace& tr )
{
	Vector end = Vector( start.x, start.y, start.z + signedDist );
	TracePlayerMovementBBox( start, end, tr );
}


// Pull player down to maintain ground contact when running on uneven ground
void MotionDriver::StayOnGround( void )
{
	// Trace up to find safe starting position (mitigates ground clipping and float noise)
	Vector    currentPos = MLPlayer.CurrentPosition();
	hulltrace upTr;
	TraceStep( currentPos, VERT_PROBE_DIST, upTr );
	Vector    safeStart  = upTr.endpos;
	
	// Trace down one step to find ground
	hulltrace dnTr;
	TraceStep( safeStart, -MLPlayer.StepHeight(), dnTr );
	
	// If downward trace found a standable surface, snap to it
	if ( 0.0f < dnTr.fraction && dnTr.fraction < 1.0f && !dnTr.startsolid && PlaneIsStandable( dnTr.plane ) )
	{
		if ( fabs( currentPos.z - dnTr.endpos.z ) > 0.5f * COORD_RESOLUTION )
		{
			MLPlayer.UpdatePosition( dnTr.endpos );
		}
	}
}


// Move serverside physics shadow to match player steps
void MotionDriver::VPhysStep( float stepHeight )
{
	mv->m_outStepHeight += stepHeight;
}


void MotionDriver::Step( const Vector& preSlidePos, const Vector& preSlideVel )
{
	// Unstepped slide results from upstream
	Vector straightSlideEndPos = MLPlayer.CurrentPosition();
	Vector straightSlideEndVel = MLPlayer.CurrentVelocity();
	float  straightSlideDist   = (straightSlideEndPos - preSlidePos).Length2D();
	
	// Reset to starting state for step-up attempt
	MLPlayer.UpdatePosition( preSlidePos );
	MLPlayer.UpdateVelocity( preSlideVel );

	// Now try stepping up to get around whatever the unstepped slide bumped into
	hulltrace stepUpTr;
	float     stepSize = MLPlayer.StepHeight() + STEP_EPS;
	TraceStep( preSlidePos, stepSize, stepUpTr );

	// If step-up succeeded, move to stepped position
	if ( !stepUpTr.startsolid && !stepUpTr.allsolid )
	{
		MLPlayer.UpdatePosition( stepUpTr.endpos );
	}

	// Slide over obstacle from current position
	Slide();
	
	// Trace downward to return to ground level
	hulltrace stepDownTr;
	TraceStep( MLPlayer.CurrentPosition(), -stepSize, stepDownTr );
	
	// Check if step-down landed on non-standable ground
	if ( stepDownTr.fraction < 1.0f && !PlaneIsStandable( stepDownTr.plane ) )
	{
		// Landed on steep surface - reject stepped path, use straight slide
		MLPlayer.UpdatePosition( straightSlideEndPos );
		MLPlayer.UpdateVelocity( straightSlideEndVel );
		
		float slideZ = straightSlideEndPos.z - preSlidePos.z;
		if ( slideZ > 0.0f )
		{
			VPhysStep( slideZ );
		}
		return;
	}
	
	// Step-down trace valid, update position to endpoint
	if ( !stepDownTr.startsolid && !stepDownTr.allsolid )
	{
		MLPlayer.UpdatePosition( stepDownTr.endpos );
	}
	
	// Which path moved further?
	Vector steppedSlideEndPos = MLPlayer.CurrentPosition();
	Vector steppedSlideEndVel = MLPlayer.CurrentVelocity();
	float  steppedSlideDist   = (steppedSlideEndPos - preSlidePos).Length2D();
	
	if ( straightSlideDist > steppedSlideDist )  // original unstepped slide got us further
	{
		MLPlayer.UpdatePosition( straightSlideEndPos );
		MLPlayer.UpdateVelocity( straightSlideEndVel );
	}
	else  // stepped slide got further but have to make sure stepping doesn't screw with our z vel
	{
		Vector finalVel = steppedSlideEndVel;
		finalVel.z = straightSlideEndVel.z;
		MLPlayer.UpdateVelocity( finalVel );
	}
	
	// Record final step height offset
	float stepZ = MLPlayer.CurrentPosition().z - preSlidePos.z;
	if ( stepZ > 0.0f )
	{
		VPhysStep( stepZ );
	}
}


void MotionDriver::Move()
{
	Vector startPos = MLPlayer.CurrentPosition();
	Vector startVel = MLPlayer.CurrentVelocity();

	if ( Slide() )
	{
		if ( MLPlayer.IsGrounded )
		{
			StayOnGround();
		}
		return;
	}

	Step( startPos, startVel );
	if ( MLPlayer.IsGrounded )
	{
		StayOnGround();
	}
}


// Per-tick entry point Source override. This is where we divert from Source's pipeline into ours.
void MotionDriver::PlayerMove()
{
	TickSetup();
	SpaghettiContainment();      // Blood sacrifices for Gaben, keep the engine happy
	UpdateMovementAxes();        // Set current fwd, right, up axes based on player view dir
	if ( PlayerIsStuck() )
	{
		return;
	}
	CategorizePosition();        // Update grounding status, friction/material values etc
	MoreSpaghettiContainment();  // More engine housekeeping, nothing to do with us
	
	CalcCurrentForces();         // Calculate & store all force vectors acting on the player
	UpdateVPhysVel();            // Yet more housekeeping for downstream engine ops
	Accelerate();                // Modify player velocity according to current forces
	Move();                      // Modify player position according to current velocity
}


// Expose MotionDriver as the IGameMovement provider (mirrors Valve pattern)
static MotionDriver g_GameMovement;
IGameMovement *g_pGameMovement = ( IGameMovement * )&g_GameMovement;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CGameMovement, IGameMovement, INTERFACENAME_GAMEMOVEMENT, g_GameMovement );

