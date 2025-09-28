// Headers
#include "cbase.h"
#include "movevars_shared.h"   // cl_*speed ConVars, GetCurrentGravity
#include "ml_defs.h"
#include "ml_forcecalculator.h"

using namespace MotionLab;

void ForceCalculator::Reset()
{
	// Reset tick force calc vars for current player
	FRAMETIME = 0.0f;
	FwdMoveDir.Init();
	RightMoveDir.Init();
	UpMoveDir.Init();
	CurrentGroundNormal.Init();
	CurrentGroundFriction = BASE_FRICTION;
	PlayerIsGrounded      = false;
	PlayerCanJump         = false;
	PlayerJumped          = false;
	
	// Clear pointers
	player = NULL;
	mv     = NULL;
	
	// Reset all force results
	CurrentFrictionForce.Init();
	CurrentDragForce.Init();
	CurrentWASDForce.Init();
	CurrentGravForce.Init();
	CurrentJumpForce.Init();
	CurrentDriveForce.Init();
	CurrentResistForce.Init();
	CurrentNetForce.Init();
}

const Vector& ForceCalculator::GetCurrentPlayerPos() const
{
	Assert( mv != nullptr );
	return mv->GetAbsOrigin();
}

const Vector& ForceCalculator::GetCurrentPlayerVel() const
{
	Assert( mv != nullptr );
	return mv->m_vecVelocity;
}

float ForceCalculator::GetCurrentPlayerSpeed() const
{
	return GetCurrentVel().Length();
}

float ForceCalculator::GetFwdAxisInput() const
{
	Assert( mv != nullptr );
	
	const float fwdMove   = mv->m_flForwardMove;
	const bool  isForward = (fwdMove >= 0.0f);
	const float denom     = isForward ? cl_forwardspeed.GetFloat() : cl_backspeed.GetFloat();
	if ( denom <= 0.0f )
		return 0.0f;

	float axis = fwdMove / denom;
	return clamp(axis, -1.0f, 1.0f);
}

float ForceCalculator::GetSideAxisInput() const
{
	Assert( mv != nullptr );
	
	const float sideMove = mv->m_flSideMove;
	const float denom    = cl_sidespeed.GetFloat();
	if ( denom <= 0.0f )
		return 0.0f;

	float axis = sideMove / denom;
	return clamp(axis, -1.0f, 1.0f);
}

bool ForceCalculator::GetJumpInput() const
{
	Assert( mv != nullptr );
	return mv->m_nButtons & IN_JUMP;
}

// Project vector onto plane: v_projected = v - (v · n̂) * n̂
void ForceCalculator::VectorPlaneProjection( const Vector& v, const Vector& planeNormal, Vector& out ) const
{
	out = v - ( planeNormal * DotProduct( v, planeNormal ) );
}

void ForceCalculator::VectorRescale( Vector& v, const float targetLength ) const
{
	VectorNormalize( v );
	v *= targetLength;
}

// Placeholder. Eventually mass will be a property of the player's build
float ForceCalculator::GetPlayerMass() const
{
	return 100.0f;  // TODO: Tune this value
}

// Placeholder. Eventually drag coefficient will be a property of the player's build
float ForceCalculator::GetPlayerDragCoeff() const
{
	return 1.0f;  // TODO: Tune this value
}

// Placeholder. Eventually boost force magnitude will be a property of the player's build
float ForceCalculator::GetPlayerBoostForce() const
{
	return 1.0f;  // TODO: Tune this value
}

// Placeholder. Eventually jump force magnitude will be a property of the player's build
float ForceCalculator::GetPlayerJumpForce() const
{
	return 1.0f;  // TODO: Tune this value
}

// Quadratic air drag: F_drag = -k * |v|² * v̂
void ForceCalculator::CalcAirDrag()
{
	Vector currentVel   = GetCurrentVel();
	float  currentSpeed = currentVel.Length();
	if ( currentSpeed > 0.0f )
	{
		float  dragMag   = GetPlayerDragCoeff() * ( currentSpeed * currentSpeed );
		Vector dragDir   = currentVel * -1.0f;
		VectorNormalize( dragDir );
		CurrentDragForce = dragDir * dragMag;
	}
}

// Kinetic friction: f = μN
void ForceCalculator::CalcFriction()
{
	if ( PlayerIsGrounded )
	{
		// Project velocity onto ground plane
		Vector currentVel = GetCurrentVel();
		Vector tangentV;
		VectorPlaneProjection( currentVel, CurrentGroundNormal, tangentV );
		float  tanSpeed   = tangentV.Length();
		if (tanSpeed > 0.0f)
		{
			// Normal force: N = m*g*cos(θ), cos(θ) = n̂⋅up
			float m    = GetPlayerMass();
			float nDot = DotProduct( CurrentGroundNormal, WORLD_UP );
			float N    = m * GetCurrentGravity() * nDot;
			float f    = CurrentGroundFriction * N;     // Friction magnitude: f = μN
			float fMax = ( tanSpeed * m ) / FRAMETIME;  // Force that will stop the player
			f = MIN( f,fMax );                          // Prevent friction from reversing vel
			
			// Apply friction in opposite direction to surface velocity
			Vector fDir = tangentV * -1.0f;
			VectorNormalize( fDir );
			CurrentFrictionForce = f * fDir;
		}
	}
}

// TODO: Should the 0-speed checks here be MIN_VEL instead?
void ForceCalculator::CalcResistForce()
{
	CalcAirDrag();   // Applies always
	CalcFriction();  // Only applies while grounded
	CurrentResistForce = CurrentDragForce + CurrentFrictionForce;
}

// Compute planar input force from WASD
void ForceCalculator::CalcPlanarDrivers()
{
	Vector inputDir  = ( FwdMoveDir * GetFwdAxisInput() ) + ( RightMoveDir * GetSideAxisInput() );
	float  inputMag  = inputDir.Length();
	
	if (inputMag > 0.0f)
	{
		if (inputMag > 1.0f)
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

void ForceCalculator::CalcVerticalDrivers()
{
	if ( PlayerIsGrounded )
	{
		if ( PlayerCanJump && GetJumpInput() )
		{
			CurrentJumpForce = WORLD_UP * GetPlayerJumpForce();
			PlayerJumped = true;  // signal for VPhys bookkeeping downstream
		}
	}
	else
	{
		CurrentGravForce = WORLD_DOWN * ( GetCurrentGravity() * GetPlayerMass() );
	}
}

void ForceCalculator::CalcDriveForce()
{
	CalcPlanarDrivers();
	CalcVerticalDrivers();
	CurrentDriveForce = CurrentWASDForce + CurrentJumpForce + CurrentGravForce;
}

void ForceCalculator::CalcCurrentForces()
{
	CalcResistForce();
	CalcDriveForce(); 
	CurrentNetForce = CurrentDriveForce + CurrentResistForce;  // Resist is already < 0, hence +
}