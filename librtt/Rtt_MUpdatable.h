//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_Updatable_H__
#define _Rtt_Updatable_H__

namespace Rtt
{

// ----------------------------------------------------------------------------

class MUpdatable
{
	public:
		U8 GetCounter() const { return fCounter; }
		void SetCounter( U8 counter ) { fCounter = counter; }

	public:
		virtual void QueueUpdate() = 0;

	private:
		U8 fCounter;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

#endif // _Rtt_Updatable_H__
