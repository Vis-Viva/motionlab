#pragma once

#include "mathlib/vector.h"
#include "mathlib/mathlib.h"
#include "ml_defs.h"

class CBasePlayer;
class CMoveData;
class ConVar; // forward declaration for extern ConVars below

// External CVARs
extern ConVar cl_forwardspeed;
extern ConVar cl_backspeed;
extern ConVar cl_sidespeed;

namespace MotionLab {

class ForceCalculator 
{
	public:  // All of this can be written/read/called by MotionDriver
		// Tick force calculation vars for current player
		float        FRAMETIME;
		Vector       FwdMoveDir;
		Vector       RightMoveDir;
		Vector       UpMoveDir;
		float        CurrentGroundFriction;
		Vector       CurrentGroundNormal;
		bool         PlayerIsGrounded;
		bool         PlayerCanJump;
	
		// Pointers to current player and movedata being processed
		CBasePlayer* player;
		CMoveData*   mv;
	
		// Force calc results stored here for reading downstream
		Vector       CurrentFrictionForce;
		Vector       CurrentDragForce;
		Vector       CurrentWASDForce;
		Vector       CurrentGravForce;
		Vector       CurrentJumpForce;
		Vector       CurrentDriveForce;
		Vector       CurrentResistForce;
		Vector       CurrentNetForce;
		bool         PlayerJumped;  // need to signal this for downstream bookkeeping

		// Public interface
		void         Reset();
		void         CalcCurrentForces();

	private:
		// Helper functions
		const Vector GetCurrentPlayerPos() const;
		const Vector GetCurrentPlayerVel() const;
		float        GetCurrentPlayerSpeed() const;
		float        GetFwdAxisInput() const;
		float        GetSideAxisInput() const;
		bool         GetJumpInput() const;
		void         VectorPlaneProjection( const Vector& v, const Vector& planeNormal, Vector& out ) const;
		void         VectorRescale( Vector& v, const float targetLength ) const;
		float        GetPlayerMass() const;
		float        GetPlayerDragCoeff() const;
		float        GetPlayerBoostForce() const;
		float        GetPlayerJumpForce() const;
	
		// Force calculation methods
		void         CalcAirDrag();
		void         CalcFriction();
		void         CalcResistForce();
		void         CalcPlanarDrivers();
		void         CalcVerticalDrivers();
		void         CalcDriveForce();
};

} // namespace MotionLab