#include "cbase.h"
#include "ml_player.h"

using namespace MotionLab;


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
    AngleVectors( mv->m_vecViewAngles, &FwdMoveDir, &RightMoveDir, &UpMoveDir );
}


void MLabPlayer::ResetFriction()
{
    baseplayer->m_surfaceFriction = 1.0f;
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
