// Headers
#include "cbase.h"
#include "movevars_shared.h"   // cl_*speed ConVars, GetCurrentGravity
#include "ml_defs.h"
#include "ml_inputreader.h"
#include "ml_player.h" 
#include "ml_forcecalculator.h"

using namespace motionlab;


ForceCalculator::ForceCalculator()
{
	Setup( NULL, NULL, 0.0f );
}


// MDriver calls this per tick per player to clean initialize force calc quantities
void ForceCalculator::Setup( InputReader* pInput, MLabPlayer* mlPlayer, float frameTime )
{
	// Store reference to current inputs, player, and frametime
	FRAMETIME   = frameTime;
	PlayerInput = pInput;
	MLPlayer    = mlPlayer;
	
	PlayerJumped = false;  // have to track this because reasons
	
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


// Project vector onto plane: v_projected = v - (v · n̂) * n̂
void ForceCalculator::VectorProjectOntoPlane( Vector& v, const Vector& planeNormal ) const
{
	float dot = DotProduct( v,planeNormal );
	v.x -= planeNormal.x * dot;
	v.y -= planeNormal.y * dot;
	v.z -= planeNormal.z * dot;
}

void ForceCalculator::VectorRescale( Vector& v, const float targetLength ) const
{
	VectorNormalize( v );
	v *= targetLength;
}


// Quadratic air drag: F_drag = -k * |v|² * v̂
void ForceCalculator::CalcAirDrag()
{
	Vector currentVel   = MLPlayer->CurrentVelocity();
	float  currentSpeed = currentVel.Length();
	if ( currentSpeed > 0.0f )
	{
		float  dragMag   = MLPlayer->DragCoeff * ( currentSpeed * currentSpeed );
		Vector dragDir   = currentVel * -1.0f;
		VectorNormalize( dragDir );
		CurrentDragForce = dragDir * dragMag;
	}
}

// Kinetic friction: f = μN
void ForceCalculator::CalcFriction()
{
	if ( MLPlayer->IsGrounded )
	{
		// Project velocity onto ground plane
		Vector tangentV = MLPlayer->CurrentVelocity();
		VectorProjectOntoPlane( tangentV, MLPlayer->CurrentGroundNormal );
		float  tanSpeed = tangentV.Length();
		if ( tanSpeed > 0.0f )
		{
			// Normal force: N = m*g*cos(θ), cos(θ) = n̂⋅up
			float m    = MLPlayer->Mass;
			float nDot = DotProduct( MLPlayer->CurrentGroundNormal, WORLD_UP );
			float N    = m * GetCurrentGravity() * nDot;

			// Friction magnitude: f = μN, but make sure we don't accidentally reverse movement dir
			float u    = MLPlayer->CurrentGroundFriction;
			float f    = u * N;
			float fMax = ( tanSpeed * m ) / FRAMETIME; // Force that will stop the player
			f = MIN( f,fMax );                         // Prevent friction from reversing vel
			
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
	Vector fwdInput  = MLPlayer->ForwardDir * PlayerInput->ForwardVal();
	Vector sideInput = MLPlayer->StrafeDir  * PlayerInput->StrafeVal();
	Vector inputDir  = fwdInput + sideInput;
	float  inputMag  = inputDir.Length();
	
	if ( inputMag > 0.0f )
	{
		if ( inputMag > 1.0f )
		{
			VectorNormalize( inputDir );
		}
		
		CurrentWASDForce = inputDir * MLPlayer->BoostForce;
		
		// Project onto ground plane if grounded, otherwise keep horizontal
		if ( MLPlayer->IsGrounded )
		{
			VectorProjectOntoPlane( CurrentWASDForce, MLPlayer->CurrentGroundNormal );
			VectorRescale( CurrentWASDForce, MLPlayer->BoostForce );  // prevent projection slowdown on slopes
		}
	}
}

void ForceCalculator::CalcVerticalDrivers()
{
	if ( MLPlayer->IsGrounded )
	{
		if ( MLPlayer->CanJump && PlayerInput->JumpIsPressed() )
		{
			CurrentJumpForce = WORLD_UP * MLPlayer->JumpForce;
			PlayerJumped     = true;  // signal for VPhys bookkeeping downstream
		}
	}
	else
	{
		CurrentGravForce = WORLD_DOWN * ( GetCurrentGravity() * MLPlayer->Mass );
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