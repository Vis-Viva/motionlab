#pragma once

#include "mathlib/vector.h"

class CBasePlayer;
class CMoveData;
class CBaseEntity;

namespace motionlab {

// -------------------------------------------------------------------------------------------------
// Temp class until we properly subclass CBasePlayer. Basically a data class, wraps MoveData and
// BasePlayer in a much nicer interface. No complex logic happens here, it's just a way for us to
// store some useful quantities and talk to MoveData/BasePlayer via getters and setters.
// -------------------------------------------------------------------------------------------------
class MLabPlayer
{
	private:
		CMoveData*   mv;
		CBasePlayer* baseplayer;

	public:
		MLabPlayer();
		void Setup( CMoveData* moveData, CBasePlayer* basePlayer );

		// placeholder player properties - eventually these will be properties of the player's build
		float         Mass;
		float         DragCoeff;
		float         BoostForce;
		float         JumpForce;

		// derived state (computed once per tick by MotionDriver)
		bool          IsGrounded;
		bool          CanJump;
		Vector        CurrentGroundNormal;
		float         CurrentGroundFriction;
		Vector        ForwardDir;
		Vector        StrafeDir;
		Vector        UpDir;
		
		// interface for movement ops on movedata/baseplayer
		const Vector& CurrentPosition() const;
		const Vector& CurrentVelocity() const;
		const Vector& CurrentBaseVelocity() const;
		void          UpdateMovementAxes();
		void          UpdatePosition( const Vector& newPos );
		void          UpdateVelocity( const Vector& newVel );
		void          ZeroVelocity();
		void          UpdateBaseVelocity( const Vector& newBaseVel );
		float         JumpImpulseVel( float frameTime ) const;

		// grounding/surface/stepping ops
		CBaseEntity*  CurrentGroundEntity() const;
		void          UpdateGroundEntity( CBaseEntity* newGround );
		char          PreviousTextureType() const;
		void          UpdateTextureType( char newTextureType );
		void          UpdateStepSound();
		float         StepHeight() const;
		void          ResetFriction();

		// misc housekeeping/accessors
		void          RecordFallVelocity();
		bool          IsObserver() const;
};

} // namespace motionlab