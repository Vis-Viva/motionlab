#pragma once

#include "mathlib/vector.h"

class CBasePlayer;
class CMoveData;

namespace motionlab {

class InputReader;
class MLabPlayer;
	
class ForceCalculator 
{
	public:
		ForceCalculator();
		void  Setup( InputReader* pInput, MLabPlayer* mlPlayer, float frameTime );		
		float FRAMETIME;
	
		// Pointer to current player + inputs being processed
		InputReader* PlayerInput;
		MLabPlayer*  MLPlayer;
	
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
		void         CalcCurrentForces();

	private:
		// Vector math helpers
		void         VectorProjectOntoPlane( Vector& v, const Vector& planeNormal ) const;
		void         VectorRescale( Vector& v, const float targetLength ) const;
	
		// Force calculation methods
		void         CalcAirDrag();
		void         CalcFriction();
		void         CalcResistForce();
		void         CalcPlanarDrivers();
		void         CalcVerticalDrivers();
		void         CalcDriveForce();
};

} // namespace motionlab