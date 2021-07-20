// Copyright (C) 2019 - DevSH Graphics Programming Sp. z O.O.
// This file is part of the "Nabla Engine" and was originally part of the "Irrlicht Engine"
// For conditions of distribution and use, see copyright notice in nabla.h
// See the original file in irrlicht source for authors

#ifndef __NBL_I_NABLA_DEVICE_H_INCLUDED__
#define __NBL_I_NABLA_DEVICE_H_INCLUDED__

#include "dimension2d.h"
#include "ICursorControl.h"
#include "ITimer.h"
#include "IOSOperator.h"

#include "IFileSystem.h"
#include "nbl/asset/IAssetManager.h"
#include "ISceneManager.h"

namespace nbl
{
	class ILogger;

	//! The Irrlicht device. You can create it with createDevice() or createDeviceEx().
	/** This is the most important class of the Irrlicht Engine. You can
	access everything in the engine if you have a pointer to an instance of
	this class.  There should be only one instance of this class at any
	time.
	*/
	class IrrlichtDevice : public virtual core::IReferenceCounted
	{
	public:
        IrrlichtDevice();
        virtual ~IrrlichtDevice();

		//! Runs the device.
		/** Also increments the virtual timer by calling
		ITimer::tick();. You can prevent this
		by calling ITimer::stop(); before and ITimer::start() after
		calling IrrlichtDevice::run(). Returns false if device wants
		to be deleted. Use it in this way:
		\code
		while(device->run())
		{
			// draw everything here
		}
		\endcode
		If you want the device to do nothing if the window is inactive
		(recommended), use the slightly enhanced code shown at isWindowActive().

		Note if you are running Irrlicht inside an external, custom
		created window: Calling Device->run() will cause Irrlicht to
		dispatch windows messages internally.
		If you are running Irrlicht in your own custom window, you can
		also simply use your own message loop using GetMessage,
		DispatchMessage and whatever and simply don't use this method.
		But note that Irrlicht will not be able to fetch user input
		then. See nbl::SIrrlichtCreationParameters::WindowId for more
		informations and example code.
		*/
		virtual bool run() = 0;

		//! Cause the device to temporarily pause execution and let other processes run.
		/** This should bring down processor usage without major
		performance loss for Irrlicht */
		virtual void yield() = 0;

		//! Pause execution and let other processes to run for a specified amount of time.
		/** It may not wait the full given time, as sleep may be interrupted
		\param timeMs: Time to sleep for in milisecs.
		\param pauseTimer: If true, pauses the device timer while sleeping
		*/
		virtual void sleep(uint32_t timeMs, bool pauseTimer=false) = 0;

		//! Provides access to the operation system operator object.
		/** The OS operator provides methods for
		getting system specific informations and doing system
		specific operations, such as exchanging data with the clipboard
		or reading the operation system version.
		\return Pointer to the OS operator. */
		virtual IOSOperator* getOSOperator() = 0;

		//! Provides access to the engine's timer.
		/** The system time can be retrieved by it as
		well as the virtual time, which also can be manipulated.
		\return Pointer to the ITimer object. */
		virtual ITimer* getTimer() = 0;

		//! Sets the caption of the window.
		/** \param text: New text of the window caption. */
		virtual void setWindowCaption(const std::wstring& text) = 0;

		//! Returns if the window is active.
		/** If the window is inactive,
		nothing needs to be drawn. So if you don't want to draw anything
		when the window is inactive, create your drawing loop this way:
		\code
		while(device->run())
		{
			if (device->isWindowActive())
			{
				// draw everything here
			}
			else
				device->yield();
		}
		\endcode
		\return True if window is active. */
		virtual bool isWindowActive() const = 0;

		//! Checks if the Irrlicht window has focus
		/** \return True if window has focus. */
		virtual bool isWindowFocused() const = 0;

		//! Checks if the Irrlicht window is minimized
		/** \return True if window is minimized. */
		virtual bool isWindowMinimized() const = 0;

		//! Checks if the Irrlicht window is running in fullscreen mode
		/** \return True if window is fullscreen. */
		virtual bool isFullscreen() const = 0;

		//! Get the current color format of the window
		/** \return Color format of the window. */
		virtual asset::E_FORMAT getColorFormat() const = 0;

		//! Sends a user created event to the engine.
		/** Is is usually not necessary to use this. However, if you
		are using an own input library for example for doing joystick
		input, you can use this to post key or mouse input events to
		the engine. Internally, this method only delegates the events
		further to the scene manager and the GUI environment. */
		virtual bool postEventFromUser(const SEvent& event) = 0;

		//! Sets the input receiving scene manager.
		/** If set to null, the main scene manager (returned by
		GetSceneManager()) will receive the input
		\param sceneManager New scene manager to be used. */
		virtual void setInputReceivingSceneManager(scene::ISceneManager* sceneManager) = 0;

		//! Sets if the window should be resizable in windowed mode.
		/** The default is false. This method only works in windowed
		mode.
		\param resize Flag whether the window should be resizable. */
		virtual void setResizable(bool resize=false) = 0;

		//! Minimizes the window if possible.
		virtual void minimizeWindow() =0;

		//! Maximizes the window if possible.
		virtual void maximizeWindow() =0;

		//! Restore the window to normal size if possible.
		virtual void restoreWindow() =0;

		//! Activate any joysticks, and generate events for them.
		/** Irrlicht contains support for joysticks, but does not generate joystick events by default,
		as this would consume joystick info that 3rd party libraries might rely on. Call this method to
		activate joystick support in Irrlicht and to receive nbl::SJoystickEvent events.
		\param joystickInfo On return, this will contain an array of each joystick that was found and activated.
		\return true if joysticks are supported on this device and _NBL_COMPILE_WITH_JOYSTICK_EVENTS_
				is defined, false if joysticks are not supported or support is compiled out.
		*/
		virtual bool activateJoysticks(core::vector<SJoystickInfo>& joystickInfo) =0;
	};

} // end namespace nbl

#endif

