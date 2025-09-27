#pragma once

#include "utlvector.h"
#include "gamemovement.h"
#include "ml_types.h"

class CBaseEntity;

// We subclass CGameMovement so we can override PlayerMove as our per-tick entry point
class MotionDriver : public CGameMovement
{
private:
	// MotionDriver state - eventually some of this will get refactored but it's good enough for now
	float         FRAMETIME;
	Vector        ForwardMoveAxis;
	Vector        RightMoveAxis;
	Vector        UpMoveAxis;
	float         CurrentGroundFriction;
	Vector        CurrentGroundNormal;
	bool          PlayerIsGrounded;
	bool          PlayerCanJump;
	Vector        CurrentFrictionForce;
	Vector        CurrentDragForce;
	Vector        CurrentWASDForce;
	Vector        CurrentGravForce;
	Vector        CurrentJumpForce;
	Vector        CurrentDriveForce;
	Vector        CurrentResistForce;
	Vector        CurrentNetForce;


	// ----- ANCILLARY SOURCE OVERRIDES -----------------------------------------------------------	
	void          CheckParameters( void );
	// ----- END ANCILLARY SOURCE OVERRIDES -------------------------------------------------------


	void          ClearState();
	void          ResetPhysAccumulators();
	void          SpaghettiContainment();
	void          UpdateMovementAxes();
	bool          PlayerIsStuck();
	void          ResetPlayerFriction( void );
	const Vector& GetCurrentPos() const;
	const Vector& GetCurrentVel() const;
	float         GetCurrentSpeed() const;
	bool          TraceHitEntity( const hulltrace& tr ) const;
	CBaseEntity*  GetPlayerGroundEntity() const;
	surfacedata*  GetTraceSurfaceData( const hulltrace& tr ) const;
	char          GetPlayerPrevTextureType() const;
	void          SetPlayerTextureType( char newTextureType );
	void          UpdatePlayerGameMaterial( const hulltrace& groundTr );
	bool          PlaneIsStandable( const plane& pl ) const;
	CBaseEntity*  GetTraceCollisionEntity( const hulltrace* tr ) const;
	void          HandleGroundTransitionVel( CBaseEntity* oldGround, CBaseEntity* newGround );
	float         GetTraceFriction( const hulltrace* tr ) const;
	void          UpdateGrounding( const hulltrace* groundTr );
	bool          RegisterTouch( const hulltrace& tr, const Vector& collisionVel );
	void          SetGroundEntity( const hulltrace *groundTr );
	void          CategorizePosition( void );
	void          RecordFallVelocity();
	void          UpdatePlayerStepSound();
	void          MoreSpaghettiContainment();
	float         GetForwardAxisInput() const;
	float         GetSideAxisInput() const;
	bool          GetJumpInput() const;
	void          VectorPlaneProjection( const Vector& v, const Vector& planeNormal, Vector& out ) const;
	void          VectorRescale( Vector& v, const float targetLength ) const;
	float         GetPlayerMass() const;
	float         GetJumpImpulseVel() const;
	void          VPhysJump();
	void          CalcAirDrag();
	void          CalcFriction();
	void          CalcResistForce();
	void          CalcPlanarDrivers();
	void          CalcVerticalDrivers();
	void          CalcDriveForce();
	void          CalcCurrentForces();
	void          UpdateVPhysVel();
	void          ZeroVelocity();
	void          UpdateVelocity( const Vector& newVel );
	void          Accelerate();
	void          UpdatePosition( const Vector& newPos );
	void          TracePlayerMovementBBox( const Vector& startPos, const Vector& targetPos, hulltrace& outTr );
	bool          CheckTraceStuck( const hulltrace& tr ) const;
	bool          CheckSlideTraceInvalid( const hulltrace& tr ) const;
	Vector        DeflectVelocity( const Vector& currentVel, const Vector& normal, float overbounce );
	bool          Slide();
	void          TraceStep( const Vector& start, float signedDist, hulltrace& tr );
	void          StayOnGround( void );
	void          VPhysStep( float stepHeight );
	void          Step( const Vector& preSlidePos, const Vector& preSlideVel );
	void          Move();

public:

	MotionDriver();
	virtual ~MotionDriver();

	virtual void PlayerMove() OVERRIDE; // Core override - this is our entry point
};
