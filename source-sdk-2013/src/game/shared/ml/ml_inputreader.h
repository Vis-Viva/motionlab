#pragma once

class CMoveData;

namespace motionlab {

class InputReader
{
	private:
		CMoveData* mv;

	public:
		InputReader();
		void  Setup( CMoveData* moveData );
		
		float ForwardVal() const;
		float StrafeVal() const;
		bool  JumpIsPressed() const;
};

} // namespace motionlab