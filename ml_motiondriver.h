#pragma once

#include "mathlib/vector.h"
#include "mathlib/mathlib.h"
#include "gametrace.h"
#include "ml_defs.h"
#include "ml_inputreader.h"
#include "ml_player.h"
#include "ml_forcecalculator.h"

class CBaseEntity;

namespace motionlab {

// We subclass CGameMovement so we can override PlayerMove as our per-tick entry point
class MotionDriver : public CGameMovement
{
private:
	float           FRAMETIME;
	InputReader     PlayerInputs;
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
	void          SyncVPhys();
	void          Accelerate();
	void          TracePlayerMovementBBox( const Vector& startPos, const Vector& targetPos, hulltrace& outTr ) const;
	bool          CheckTraceStuck( const hulltrace& tr ) const;
	bool          CheckSlideTraceInvalid( const hulltrace& tr ) const;
	Vector        DeflectVelocity( const Vector& currentVel, const Vector& normal, float overbounce ) const;
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

} // namespace motionlab