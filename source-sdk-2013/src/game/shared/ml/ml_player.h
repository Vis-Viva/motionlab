#pragma once

#include "mathlib/vector.h"
#include "ml_defs.h"

class CBasePlayer;
class CMoveData;
class CBaseEntity;

namespace MotionLab {

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
        // property accessors for mv & baseplayer
        const Vector& CurrentPosition() const;
        const Vector& CurrentVelocity() const;
		const Vector& CurrentBaseVelocity() const;
        void          UpdatePosition( const Vector& newPos );
        void          UpdateVelocity( const Vector& newVel );
		void          ZeroVelocity();
		void          UpdateBaseVelocity( const Vector& newBaseVel );

		// per-tick helpers
		void          RecordFallVelocity();
		void          UpdateMovementAxes();
		void          ResetFriction();
		bool          IsObserver() const;

		// ground/material helpers
		CBaseEntity*  CurrentGroundEntity() const;
		void          UpdateGroundEntity( CBaseEntity* newGround );
		char          PreviousTextureType() const;
		void          UpdateTextureType( char newTextureType );
		void          UpdateStepSound();
		float         StepHeight() const;

        // Derived state (computed once per tick by MotionDriver)
		bool          IsGrounded;
		bool          CanJump;
		Vector        CurrentGroundNormal;
		float         CurrentGroundFriction;
        Vector        FwdMoveDir;
        Vector        RightMoveDir;
        Vector        UpMoveDir;

        // Placeholder player properties - eventually these will be properties of the player's build
        float         Mass;
        float         DragCoeff;
        float         BoostForce;
        float         JumpForce;
};

} // namespace MotionLab