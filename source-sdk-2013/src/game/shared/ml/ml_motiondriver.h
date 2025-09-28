#pragma once

#include "mathlib/vector.h"
#include "mathlib/mathlib.h"
#include "gametrace.h"
#include "vphysics_interface.h"
#include "utlvector.h"
#include "gamemovement.h"
#include "ml_defs.h"
#include "ml_player.h"
#include "ml_forcecalculator.h"

class CBaseEntity;

// We subclass CGameMovement so we can override PlayerMove as our per-tick entry point
class MotionDriver : public CGameMovement
{
private:
	float           FRAMETIME;
	MLabPlayer      MLPlayer;     
	ForceCalculator FCalc;


	// ----- ANCILLARY SOURCE OVERRIDES -----------------------------------------------------------	
	void          CheckParameters( void );
	// ----- END ANCILLARY SOURCE OVERRIDES -------------------------------------------------------


    void          TickSetup();
	void          ResetPhysAccumulators();
	void          SpaghettiContainment();
	void          UpdateMovementAxes();
	bool          PlayerIsStuck();
    float         GetCurrentSpeed() const;
	bool          TraceHitEntity( const hulltrace& tr ) const;
	surfacedata*  GetTraceSurfaceData( const hulltrace& tr ) const;
	void          UpdatePlayerGameMaterial( const hulltrace& groundTr );
	bool          PlaneIsStandable( const plane& pl ) const;
	CBaseEntity*  GetTraceCollisionEntity( const hulltrace* tr ) const;
	void          HandleGroundTransitionVel( CBaseEntity* oldGround, CBaseEntity* newGround );
	float         GetTraceFriction( const hulltrace* tr ) const;
	void          UpdateGrounding( const hulltrace* groundTr );
	bool          RegisterTouch( const hulltrace& tr, const Vector& collisionVel );
	void          SetGroundEntity( const hulltrace *groundTr );
    void          CategorizePosition();
	void          MoreSpaghettiContainment();
    void          CalcCurrentForces();
    bool          GetJumpInput() const;
	float         GetJumpImpulseVel() const;
	void          VPhysJump();
	void          UpdateVPhysVel();
	void          Accelerate();
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