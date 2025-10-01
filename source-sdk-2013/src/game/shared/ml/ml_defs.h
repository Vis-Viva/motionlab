#pragma once

#include "mathlib/vector.h"
#include "mathlib/mathlib.h"
#include "gametrace.h"
#include "coordsize.h"
#include "vphysics_interface.h"

// MotionLab namespace - provides contained set of constants and aliases for ml ops
namespace motionlab {

	// -----------------------------------------------------------------------------------------
	// Type aliases for hideous Source engine type names
	// -----------------------------------------------------------------------------------------
	using plane       = cplane_t;
	using hulltrace   = trace_t; 
	using surfacedata = surfacedata_t;

	// -----------------------------------------------------------------------------------------
	// Numeric constants
	// -----------------------------------------------------------------------------------------
	constexpr float VERT_PROBE_DIST = 2.0f;
	constexpr float GROUND_MIN_DOT  = 0.7f;
	constexpr float BASE_FRICTION   = 1.0f;
	constexpr int   MAX_CLIPS       = 5;
	constexpr int   MAX_BUMPS       = 4;
	constexpr float OVERCLIP        = 1.001f;
	constexpr float STEP_EPS        = DIST_EPSILON;
	constexpr float MIN_VEL         = 0.1f;

	// -----------------------------------------------------------------------------------------
	// Direction constants
	// -----------------------------------------------------------------------------------------
	// TODO: Does this give every downstream TU a unique copy of these vectors? Can that be bypassed?
	const Vector WORLD_UP   ( 0.0f,  0.0f,  1.0f );
	const Vector WORLD_DOWN ( 0.0f,  0.0f, -1.0f );

} // namespace motionlab
