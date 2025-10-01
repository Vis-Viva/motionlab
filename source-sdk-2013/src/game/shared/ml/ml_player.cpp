#include "cbase.h"
#include "ml_defs.h"
#include "ml_player.h"

using namespace motionlab;

MLabPlayer::MLabPlayer()
{
	Setup( NULL,NULL );
}


// Called per tick, per player by MotionDriver to clean initialize player interface fields
void MLabPlayer::Setup( CMoveData* moveData, CBasePlayer* basePlayer )
{
	// Source entities which this class exists to interface with
	mv         = moveData;
	baseplayer = basePlayer;
	
	// Useful derived quantities and flags for movement/force logic
	IsGrounded            = false;
	CanJump               = false;
	CurrentGroundFriction = 1.0f;
	CurrentGroundNormal.Init();
	ForwardDir.Init();
	StrafeDir.Init();
	UpDir.Init();
	
	// Placeholder properties - eventually will be determined by player's build
	Mass       = 100.0f;
	DragCoeff  = 1.0f;
	BoostForce = 1.0f;
	JumpForce  = 1.0f;
}


const Vector& MLabPlayer::CurrentPosition() const
{
	return mv->GetAbsOrigin();
}


const Vector& MLabPlayer::CurrentVelocity() const
{
	return mv->m_vecVelocity;
}


const Vector& MLabPlayer::CurrentBaseVelocity() const
{
	return baseplayer->GetBaseVelocity();
}


void MLabPlayer::UpdatePosition( const Vector& newPos )
{
	mv->SetAbsOrigin( newPos );
}


void MLabPlayer::UpdateVelocity( const Vector& newVel )
{
	mv->m_vecVelocity = newVel;
}


// Satanic function
void MLabPlayer::ZeroVelocity()
{
	mv->m_vecVelocity.Zero();
}


void MLabPlayer::UpdateBaseVelocity( const Vector& newBaseVel )
{
	baseplayer->SetBaseVelocity( newBaseVel );
}


// Have to keep track of this for various things unrelated to movement
void MLabPlayer::RecordFallVelocity()
{
	if ( CurrentGroundEntity() == NULL )
	{
		baseplayer->m_Local.m_flFallVelocity = -mv->m_vecVelocity[ 2 ];
	}
}


void MLabPlayer::UpdateMovementAxes()
{
	AngleVectors( mv->m_vecViewAngles, &ForwardDir, &StrafeDir, &UpDir );
}


void MLabPlayer::ResetFriction()
{
	baseplayer->m_surfaceFriction = BASE_FRICTION;
}


bool MLabPlayer::IsObserver() const
{
	return baseplayer->IsObserver();
}


// Pointer to actual ground entity player is standing on, not just an identifier
CBaseEntity* MLabPlayer::CurrentGroundEntity() const
{
	return baseplayer->GetGroundEntity();
}


void MLabPlayer::UpdateGroundEntity( CBaseEntity* newGround )
{
	baseplayer->SetGroundEntity( newGround );
}


// Phys material identifier for surface player was on coming into this tick
char MLabPlayer::PreviousTextureType() const
{
	return baseplayer->m_chPreviousTextureType;
}


// Update current surface material ID so next tick can check it
void MLabPlayer::UpdateTextureType( char newTextureType )
{
	baseplayer->m_chPreviousTextureType = newTextureType;
}


void MLabPlayer::UpdateStepSound()
{
	baseplayer->UpdateStepSound( baseplayer->m_pSurfaceData, CurrentPosition(), CurrentVelocity() );
}


float MLabPlayer::StepHeight() const
{
	return baseplayer->GetStepHeight();
}


// Amount of upward velocity we get by applying one tick of jump force
float MLabPlayer::JumpImpulseVel( float frameTime ) const
{
	return ( JumpForce / Mass ) * frameTime;
}