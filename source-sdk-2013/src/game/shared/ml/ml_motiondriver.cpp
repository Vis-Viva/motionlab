#include "cbase.h"
#include "movevars_shared.h" // GetCurrentGravity
#include "coordsize.h"       // COORD_RESOLUTION, DIST_EPSILON
#include "mathlib/mathlib.h"
#include "ml_motiondriver.h"

#include "tier0/memdbgon.h"

// -----------------------------------------------------------------------------------------------
// Module-scope constants
// -----------------------------------------------------------------------------------------------
namespace {
    // Numeric constants
    constexpr float MIN_VEL         = 0.1f;
    constexpr float STEP_EPS        = DIST_EPSILON;
    constexpr float GROUND_MIN_DOT  = 0.7f;
    constexpr float VERT_PROBE_DIST = 2.0f;
    constexpr float OVERCLIP        = 1.001f;
    constexpr int   MAX_BUMPS       = 4;

    // Direction constants
    static const Vector WORLD_UP   ( 0.0f,  0.0f,  1.0f );
    static const Vector WORLD_DOWN ( 0.0f,  0.0f, -1.0f );
}

// Client movement speed CVARs are replicated; shared/server code can read them via extern.
extern ConVar cl_forwardspeed;
extern ConVar cl_backspeed;

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
	// Use these instead - same values, much better names
	AngleVectors( mv->m_vecViewAngles, &ForwardMoveAxis, &RightMoveAxis, &UpMoveAxis );
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


void MotionDriver::ResetPlayerFriction( void )
{
	player->m_surfaceFriction = 1.0f;  // Default value
}


const Vector& MotionDriver::GetCurrentPos() const
{
	return mv->GetAbsOrigin();  // IT'S SO UGLY
}


const Vector& MotionDriver::GetCurrentVel() const
{
	return mv->m_vecVelocity;  // AAAAAAAAAAAAA
}


float MotionDriver::GetCurrentSpeed() const
{
	return GetCurrentVel().Length()
}


bool MotionDriver::TraceHitEntity( const hulltrace& tr ) const
{
	return tr.m_pEnt != NULL;  // dude
}


// Pointer to actual ground entity player is standing on, not just an identifier
CBaseEntity* MotionDriver::GetPlayerGroundEntity() const
{
    return player->GetGroundEntity();
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


// Phys material identifier for surface player was on coming into this tick
char MotionDriver::GetPlayerPrevTextureType() const
{
	return player->m_chPreviousTextureType;
}


// Update current surface material ID so next tick can check it
void MotionDriver::SetPlayerTextureType( char newTextureType )
{
	player->m_chPreviousTextureType = newTextureType;
}


// If player's standing surface material has changed, update it for any surface triggers that consume it
void MotionDriver::UpdatePlayerGameMaterial( const hulltrace& groundTr )
{
	surfacedata *currentSrfData = GetTraceSurfaceData( groundTr );
	char         currentGameMat = currentSrfData ? currentSrfData->game.material : 0;
	char         prevGameMat    = GetPlayerPrevTextureType();

	if ( GetPlayerGroundEntity() == NULL )
	{
		currentGameMat = 0;
	}

	// Changed?
	if ( prevGameMat != currentGameMat )
	{
		CEnvPlayerSurfaceTrigger::SetPlayerSurface( player, currentGameMat );
	}

	SetPlayerTextureType( currentGameMat );
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
void MotionDriver::HandleGroundTransitionVel( CBaseEntity* oldGround, CBaseEntity* newGround )
{
	Vector currentBaseVel = player->GetBaseVelocity();

	if ( !oldGround && newGround )
	{
		// Subtract ground velocity at instant we hit ground
		currentBaseVel  -= newGround->GetAbsVelocity();
		currentBaseVel.z = newGround->GetAbsVelocity().z;
	}
	else if ( oldGround && !newGround )
	{
		// Add in ground velocity at instant we started jumping
		currentBaseVel  += oldGround->GetAbsVelocity();
		currentBaseVel.z = oldGround->GetAbsVelocity().z;
	}

	player->SetBaseVelocity( currentBaseVel );
}


float MotionDriver::GetTraceFriction( const hulltrace* tr ) const
{
    if ( !tr )
        return 1.0f;  // default friction
    
    surfacedata* surfData = GetTraceSurfaceData( *tr );
    return surfData ? surfData->physics.friction : 1.0f;
}


// Store ground properties and set jump gate for movement logic later
void MotionDriver::UpdateGrounding( const hulltrace* groundTr )
{
	// Not grounded
	if ( !groundTr || !TraceHitEntity( *groundTr ) )
	{
		CurrentGroundFriction = 1.0f;
		CurrentGroundNormal   = WORLD_UP;
		PlayerIsGrounded      = false;
		PlayerCanJump         = false;
		return;
	}

	// Trace found standable ground (NOTE: Normal has already been checked by now)
	CurrentGroundFriction = GetTraceFriction( groundTr );
	CurrentGroundNormal   = groundTr->plane.normal;
	PlayerIsGrounded      = true;
	PlayerCanJump         = true;
}


bool MotionDriver::RegisterTouch( const hulltrace& tr, const Vector& collisionVel )
{
	return MoveHelper()->AddToTouched( tr, collisionVel );
}


// Does typical Source grounding ops, minus the zeroing of z vel and with some ML stuff added
void MotionDriver::SetGroundEntity( const hulltrace *groundTr )
{
	UpdateGrounding( groundTr );  // ML-specific logic

	CBaseEntity *oldGround = GetPlayerGroundEntity();
	CBaseEntity *newGround = GetTraceCollisionEntity( groundTr );
	
	HandleGroundTransitionVel( oldGround, newGround );
	player->SetGroundEntity( newGround );

	// If we are on something, categorize surface and record touch
	if ( newGround && groundTr )
	{
		CategorizeGroundSurface( *groundTr );
		player->m_flWaterJumpTime = 0;

		// Signal that we touched an object if we're standing on a non-world entity
		if ( !groundTr->DidHitWorld() ) 
		{
				RegisterTouch( *groundTr, GetCurrentVel() );
		}

		//mv->m_vecVelocity.z = 0.0f;
	}
}


// Does downward hull tracing to look for a standable entity under the player, updates related properties
void MotionDriver::CategorizePosition( void )
{

	// Reset friction to default every time we recategorize (prevents bogus friction in certain edge cases)
	ResetPlayerFriction();

	// observers don't have a ground entity
	if ( player->IsObserver() )
	{
		return;
	}

	// Initial simple short downward trace
	Vector    currentPos = GetCurrentPos();
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


// Have to keep track of this for various things unrelated to movement
void MotionDriver::RecordFallVelocity()
{
	if ( GetPlayerGroundEntity() == NULL )
	{
		player->m_Local.m_flFallVelocity = -mv->m_vecVelocity[ 2 ];
	}
}


// Source chores
void MotionDriver::UpdatePlayerStepSound()
{
	player->UpdateStepSound( player->m_pSurfaceData, mv->GetAbsOrigin(), mv->m_vecVelocity );
}


// Last bit of tick-entry engine housekeeping
void MotionDriver::MoreSpaghettiContainment()
{
	RecordFallVelocity();
	m_nOnLadder = 0;
	UpdatePlayerStepSound();
}


// Normalized forward-axis input ∈ [-1, 1] from current movedata
float MotionDriver::GetForwardAxisInput() const
{
	if ( !mv )
		return 0.0f;

	const float fwdMove   = mv->m_flForwardMove;  // Natively ∈ [-cl_backspeed, cl_forwardspeed] 
	const bool  isForward = ( fwdMove >= 0.0f );
	const float denom     = isForward ? cl_forwardspeed.GetFloat() : cl_backspeed.GetFloat();
	if ( denom <= 0.0f )
		return 0.0f;

	float axis = fwdMove / denom;  // Negative automatically preserved for back input
	return clamp( axis, -1.0f, 1.0f );
}


// Normalized side-axis input ∈ [-1, 1] from current movedata  
float MotionDriver::GetSideAxisInput() const
{
	if ( !mv )
		return 0.0f;

	const float sideMove = mv->m_flSideMove;  // Natively ∈ [-cl_sidespeed, cl_sidespeed]
	const float denom    = cl_sidespeed.GetFloat();
	if ( denom <= 0.0f )
		return 0.0f;

	float axis = sideMove / denom;
	return clamp( axis, -1.0f, 1.0f );
}


bool MotionDriver::GetJumpInput() const
{
	return mv->m_nButtons & IN_JUMP;
}


// Project vector onto plane: v_projected = v - (v · n̂) * n̂
void MotionDriver::VectorPlaneProjection( const Vector& v, const Vector& planeNormal, Vector& out ) const
{
	out = v - (planeNormal * DotProduct( v, planeNormal ));
}


void MotionDriver::VectorRescale( Vector& v, const float targetLength ) const
{
	VectorNormalize( v );
	v *= targetLength;
}


// Placeholder. Eventually mass will be a property of the player's build
float MotionDriver::GetPlayerMass() const
{
	return 100.0f;  // TODO: Tune this value
}


// Placeholder. Eventually jump force magnitude will be a property of the player's build
float MotionDriver::GetPlayerJumpForce() const
{
	return 1.0f;  // TODO: Tune this value
}


// Amount of upward velocity we get by applying one tick of jump force
float MotionDriver::GetJumpImpulseVel() const
{
	return ( GetPlayerJumpForce() / GetPlayerMass() ) * FRAMETIME;
}


// Source VPhys bookkeeping - update serverside phys shadow to match player jumps
void MotionDriver::VPhysJump()
{
	if( PlayerCanJump && GetJumpInput() )
	{
		mv->m_outJumpVel.z  += GetJumpImpulseVel();
		mv->m_outStepHeight += 0.15f;
	}
}


// Placeholder. Eventually drag coefficient will be a property of the player's build
float MotionDriver::GetPlayerDragCoeff() const
{
	return 1.0f;  // TODO: Tune this value
}


// Quadratic air drag: F_drag = -k * |v|² * v̂
void MotionDriver::CalcAirDrag()
{
	CurrentDragForce.Init();
	Vector currentVel   = GetCurrentVel();
	float  currentSpeed = currentVel.Length();
	if ( currentSpeed > 0.0f )
	{
		float  dragMag   = GetPlayerDragCoeff() * (currentSpeed * currentSpeed);
		Vector dragDir   = currentVel * -1.0f;
		VectorNormalize( dragDir )
		CurrentDragForce = dragDir * dragMag;
	}
}


// Kinetic friction: f = μN
void MotionDriver::CalcFriction()
{
	CurrentFrictionForce.Init();
	if ( PlayerIsGrounded )
	{
		// Project velocity onto ground plane
		Vector currentVel = GetCurrentVel();
		Vector tangentV;
		VectorPlaneProjection( currentVel, CurrentGroundNormal, tangentV );
		float  tanSpeed   = tangentV.Length();
		if ( tanSpeed > 0.0f )
		{
			// Normal force: N = m*g*cos(θ), cos(θ) = n̂⋅up
			float m    = GetPlayerMass();
			float nDot = DotProduct( CurrentGroundNormal, WORLD_UP );
			float N    = m * GetCurrentGravity() * nDot;
			float f    = CurrentGroundFriction * N;   // Friction magnitude: f = μN
			float fMax = (tanSpeed * m) / FRAMETIME;  // Force that will stop the player
			f = MIN( f, fMax );                       // Prevent friction from reversing vel
			
			// Apply friction in opposite direction to surface velocity
			Vector fDir = tangentV * -1.0f;
			VectorNormalize( fDir )
			CurrentFrictionForce = f * fDir;
		}
	}
}


// TODO: Should the 0-speed checks here be MIN_VEL instead?
void MotionDriver::CalcResistForce()
{
	CurrentResistForce.Init();
	CalcAirDrag();   // Applies always
	CalcFriction();  // Only applies while grounded
	CurrentResistForce = CurrentDragForce + CurrentFrictionForce;
}


// Placeholder. Eventually boost force magnitude will be a property of the player's build
float MotionDriver::GetPlayerBoostForce()
{
	return 1.0f;  // TODO: Tune this value
}


// Compute planar input force from WASD
void MotionDriver::CalcPlanarDrivers()
{
	CurrentWASDForce.Init();
	Vector inputDir  = (ForwardMoveAxis * GetForwardAxisInput()) + (RightMoveAxis * GetSideAxisInput());
	float  inputMag  = inputDir.Length();
	
	if ( inputMag > 0.0f )
	{
		if ( inputMag > 1.0f )
		{
			VectorNormalize( inputDir );
		}
		
		CurrentWASDForce = inputDir * GetPlayerBoostForce();
		
		// Project onto ground plane if grounded, otherwise keep horizontal
		if ( PlayerIsGrounded )
		{
			VectorPlaneProjection( CurrentWASDForce, CurrentGroundNormal, CurrentWASDForce );
			VectorRescale( CurrentWASDForce, GetPlayerBoostForce() );  // prevent projection slowdown on slopes
		}
	}
}


void MotionDriver::CalcVerticalDrivers()
{
	CurrentGravForce.Init();  // only applied when airborne
	CurrentJumpForce.Init();  // only applied when grounded

	if ( PlayerIsGrounded )
	{
		if ( PlayerCanJump && GetJumpInput() )
		{
			CurrentJumpForce = WORLD_UP * GetPlayerJumpForce();
			VPhysJump();  // Source vphys bookkeeping
			//PlayerCanJump = false; Dunno if we want to do this yet
		}
	}
	else
	{
		CurrentGravForce = WORLD_DOWN * (GetCurrentGravity() * GetPlayerMass());
	}
}


void MotionDriver::CalcDriveForce()
{
	CurrentDriveForce.Init();
	CalcPlanarDrivers();
	CalcVerticalDrivers();
	CurrentDriveForce = CurrentWASDForce + CurrentJumpForce + CurrentGravForce;
}


void MotionDriver::CalcCurrentForces()
{
	CurrentNetForce.Init();
	CalcResistForce();
	CalcDriveForce(); 
	CurrentNetForce = CurrentDriveForce + CurrentResistForce;  // Resist is already < 0, hence +
}


// Source VPhys bookkeeping - update planar push force serverside phys shadow applies to props
void MotionDriver::UpdateVPhysVel()
{
	Vector wishForce   = CurrentWASDForce + CurrentFrictionForce; wishForce.z = 0.0f;
	Vector wishAccel   = wishForce / GetPlayerMass();
	Vector wishDelta   = wishAccel * FRAMETIME;
	mv->m_outWishVel  += wishDelta;
	mv->m_outWishVel.z = 0.0f;
}


// Satanic function
void MotionDriver::ZeroVelocity()
{
	mv->m_vecVelocity.Zero();
}


void MotionDriver::UpdateVelocity( const Vector& newVel )
{
	mv->m_vecVelocity = newVel;
}


// F = ma -> a = F/m -> dv = a*dt
void MotionDriver::Accelerate()
{
	Vector acceleration = CurrentNetForce * ( 1.0f / GetPlayerMass() );
	Vector deltaVel     = acceleration * FRAMETIME;
	Vector newVel       = GetCurrentVel() + deltaVel;
	if ( newVel.Length() < MIN_VEL )
	{
		newVel.Zero();  // do not do Zeno paradox
	}
	UpdateVelocity( newVel );	
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


void MotionDriver::UpdatePosition( const Vector& newPos )
{
	mv->SetAbsOrigin( newPos );
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
	Vector originalStartVel = GetCurrentVel();
	Vector segmentStartVel  = GetCurrentVel();
	
	for ( int bumpCount=0; bumpCount < MAX_BUMPS; bumpCount++ )
	{
		if ( GetCurrentSpeed() == 0.0f )  // nothing to slide if we're not moving
		{
			break;
		}
		
		hulltrace slideTr;
		Vector    endPos; 
		VectorMA( GetCurrentPos(), timeLeft, GetCurrentVel(), endPos );
		TracePlayerMovementBBox( GetCurrentPos(), endPos, slideTr );
		totalFraction += slideTr.fraction;

		// First make sure we can actually use this trace for a slide
		if ( CheckSlideTraceInvalid( slideTr ) )
		{
			ZeroVelocity();  // :(
			cleanSlide = false;
			break;
		}
		
		// Ok the trace is usable, now what does it tell us?
		if ( slideTr.fraction > 0.0f )  // hooray we went somewhere
		{
			UpdatePosition( slideTr.endpos );
			if ( slideTr.fraction == 1.0f )  // We made it all the way to the end, we're done
			{
				break;
			}
			// Only made it part way, get ready to handle collisions
			planeNormals.RemoveAll();
			segmentStartVel = GetCurrentVel();
		}
		
		// Didn't break above, must have bumped into something - record touch and handle collision
		RegisterTouch( slideTr, GetCurrentVel() );          // Source bookkeeping for vphys, surface triggers, etc
		float timeTravelled = timeLeft * slideTr.fraction;  // Amount of timestep consumed before collision
		timeLeft           -= timeTravelled;
		cleanSlide          = false;
		Vector bumpNormal   = slideTr.plane.normal;

		// Hit too many planes - we're stuck, zero vel & return false - this shouldn't really happen but whatev
		if ( planeNormals.Count() >= MAX_CLIP_PLANES )
		{
			ZeroVelocity();
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
				ZeroVelocity();
				break;
			}
		}
		
		// Guard against velocity reversal from deflections to prevent oscillations in corners
		if ( DotProduct( newVel, originalStartVel ) <= 0.0f )
		{
			ZeroVelocity();
			break;
		}
		
		UpdateVelocity( newVel );  // Apply deflected velocity and continue
	}

	// No progress across all bumps => no movement => sad
	if ( totalFraction <= 0.0f )
	{
		ZeroVelocity();
	}

	return cleanSlide;
}


float MotionDriver::GetPlayerStepHeight() const
{
	return player->GetStepHeight();
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
	Vector    currentPos = GetCurrentPos();
	hulltrace upTr;
	TraceStep( currentPos, VERT_PROBE_DIST, upTr );
	Vector    safeStart  = upTr.endpos;
	
	// Trace down one step to find ground
	hulltrace dnTr;
	TraceStep( safeStart, -GetPlayerStepHeight(), dnTr );
	
	// If downward trace found a standable surface, snap to it
	if ( 0.0f < dnTr.fraction && dnTr.fraction < 1.0f && !dnTr.startsolid && PlaneIsStandable( dnTr.plane ) )
	{
		if ( fabs( currentPos.z - dnTr.endpos.z ) > 0.5f * COORD_RESOLUTION )
		{
			UpdatePosition( dnTr.endpos );
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
	Vector straightSlideEndPos = GetCurrentPos();
	Vector straightSlideEndVel = GetCurrentVel();
	float  straightSlideDist   = (straightSlideEndPos - preSlidePos).Length2D();
	
	// Reset to starting state for step-up attempt
	UpdatePosition( preSlidePos );
	UpdateVelocity( preSlideVel );

	// Now try stepping up to get around whatever the unstepped slide bumped into
	hulltrace stepUpTr;
	float     stepSize = GetPlayerStepHeight() + STEP_EPS;
	TraceStep( preSlidePos, stepSize, stepUpTr );

	// If step-up succeeded, move to stepped position
	if ( !stepUpTr.startsolid && !stepUpTr.allsolid )
	{
		UpdatePosition( stepUpTr.endpos );
	}

	// Slide over obstacle from current position
	Slide();
	
	// Trace downward to return to ground level
	hulltrace stepDownTr;
	TraceStep( GetCurrentPos(), -stepSize, stepDownTr );
	
	// Check if step-down landed on non-standable ground
	if ( stepDownTr.fraction < 1.0f && !PlaneIsStandable( stepDownTr.plane ) )
	{
		// Landed on steep surface - reject stepped path, use straight slide
		UpdatePosition( straightSlideEndPos );
		UpdateVelocity( straightSlideEndVel );
		
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
		UpdatePosition( stepDownTr.endpos );
	}
	
	// Which path moved further?
	Vector steppedSlideEndPos = GetCurrentPos();
	Vector steppedSlideEndVel = GetCurrentVel();
	float  steppedSlideDist   = (steppedSlideEndPos - preSlidePos).Length2D();
	
	if ( straightSlideDist > steppedSlideDist )  // original unstepped slide got us further
	{
		UpdatePosition( straightSlideEndPos );
		UpdateVelocity( straightSlideEndVel );
	}
	else  // stepped slide got further but have to make sure stepping doesn't screw with our z vel
	{
		Vector finalVel = steppedSlideEndVel;
		finalVel.z = straightSlideEndVel.z;
		UpdateVelocity( finalVel );
	}
	
	// Record final step height offset
	float stepZ = GetCurrentPos().z - preSlidePos.z;
	if ( stepZ > 0.0f )
	{
		VPhysStep( stepZ );
	}
}


void MotionDriver::Move()
{
	Vector startPos = GetCurrentPos();
	Vector startVel = GetCurrentVel();

	if ( Slide() )
	{
		if ( PlayerIsGrounded )
		{
			StayOnGround();
		}
		return;
	}

	Step( startPos, startVel );
	if ( PlayerIsGrounded )
	{
		StayOnGround();
	}
}


// Per-tick entry point Source override. This is where we divert from Source's pipeline into ours.
void MotionDriver::PlayerMove()
{
	ClearState();                // Make sure we start with a clean state the current player/tick
	SetConfig();                 // Give us a nice interface for a bunch of engine parameters
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

