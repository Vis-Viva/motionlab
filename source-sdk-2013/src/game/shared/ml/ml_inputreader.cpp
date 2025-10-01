#include "cbase.h"
#include "movevars_shared.h"
#include "in_buttons.h"
#include "ml_inputreader.h"

using namespace motionlab;


InputReader::InputReader()
{
	Setup( NULL );
}


void InputReader::Setup( CMoveData* moveData )
{
	mv = moveData;
}


float InputReader::ForwardVal() const
{
	const float fwdMove   = mv->m_flForwardMove;
	const bool  isForward = (fwdMove >= 0.0f);
	const float denom     = isForward ? cl_forwardspeed.GetFloat() : cl_backspeed.GetFloat();
	if ( denom <= 0.0f )
		return 0.0f;

	float axis = fwdMove / denom;
	return clamp( axis, -1.0f, 1.0f );
}


float InputReader::StrafeVal() const
{
	const float sideMove = mv->m_flSideMove;
	const float denom    = cl_sidespeed.GetFloat();
	if ( denom <= 0.0f )
		return 0.0f;

	float axis = sideMove / denom;
	return clamp( axis, -1.0f, 1.0f );
}


bool InputReader::JumpIsPressed() const
{
	return mv->m_nButtons & IN_JUMP;
}