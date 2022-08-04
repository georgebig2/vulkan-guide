
#include <iostream>

#include <game-activity/native_app_glue/android_native_app_glue.h>
#define VK_USE_PLATFORM_ANDROID_KHR
//#define VK_NO_PROTOTYPES 1
#include "rengine.h"

//#include <vulkan/vulkan_android.h>
//#define VK_USE_PLATFORM_ANDROID_KHR
//#include <vulkan/vulkan.h>
//extern PFN_vkCreateAndroidSurfaceKHR vkCreateAndroidSurfaceKHR;

//we want to immediately abort when there is an error. In normal engines this would give an error message to the user, or perform a dump of state.
using namespace std;
#define VK_CHECK(x)                                                 \
	do                                                              \
	{                                                               \
		VkResult err = x;                                           \
		if (err)                                                    \
		{                                                           \
			std::cout <<"Detected Vulkan error: " << err << std::endl; \
			abort();                                                \
		}                                                           \
	} while (0)

class AndroidEngine : public REngine {
public:

	bool create_surface(VkInstance instance, VkSurfaceKHR* surface) override
	{
		VkAndroidSurfaceCreateInfoKHR info{ VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR };
		//info.window = handle;
		VK_CHECK(vkCreateAndroidSurfaceKHR(instance, &info, nullptr, surface));
		return true;
	}

};

// Implement input event handling function.
static int32_t engine_handle_input(struct android_app* app)
{
	//auto* engine = (struct engine*)app->userData;
	auto ib = android_app_swap_input_buffers(app);

	if (ib && ib->motionEventsCount)
    {
/*		for (int i = 0; i < ib->motionEventsCount; i++) {
			auto *event = &ib->motionEvents[i];
			int32_t ptrIdx = 0;
			switch (event->action & AMOTION_EVENT_ACTION_MASK) {
				case AMOTION_EVENT_ACTION_POINTER_DOWN:
				case AMOTION_EVENT_ACTION_POINTER_UP:
					// Retrieve the index for the starting and the ending of any secondary pointers
					ptrIdx = (event->action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
																					   AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
				case AMOTION_EVENT_ACTION_DOWN:
				case AMOTION_EVENT_ACTION_UP:
					engine->state.x = GameActivityPointerAxes_getAxisValue(
							&event->pointers[ptrIdx], AMOTION_EVENT_AXIS_X);
					engine->state.y = GameActivityPointerAxes_getAxisValue(
							&event->pointers[ptrIdx], AMOTION_EVENT_AXIS_Y);
					break;
				case AMOTION_EVENT_ACTION_MOVE:
					// Process the move action: the new coordinates for all active touch pointers
					// are inside the event->pointers[]. Compare with our internally saved
					// coordinates to find out which pointers are actually moved. Note that there is
					// no index embedded inside event->action for AMOTION_EVENT_ACTION_MOVE (there
					// might be multiple pointers moved at the same time).
					...
					break;
			}
		}
*/
		android_app_clear_motion_events(ib);
	}

	// Process the KeyEvent in a similar way.

	return 0;
}

static void _handle_cmd_proxy(struct android_app *app, int32_t cmd)
{
    //NativeEngine *engine = (NativeEngine *) app->userData;
    //engine->HandleCommand(cmd);
	//VLOGD("NativeEngine: handling command %d.", cmd);
	switch (cmd)
	{
		case APP_CMD_SAVE_STATE:
			// The system has asked us to save our current state.
			//VLOGD("NativeEngine: APP_CMD_SAVE_STATE");
			//mState.mHasFocus = mHasFocus;
			//mApp->savedState = malloc(sizeof(mState));
			//*((NativeEngineSavedState *) mApp->savedState) = mState;
			//mApp->savedStateSize = sizeof(mState);
			break;
		case APP_CMD_INIT_WINDOW:
			// We have a window!
			//VLOGD("NativeEngine: APP_CMD_INIT_WINDOW");
			if (app->window != NULL)
			{
				//mHasWindow = true;
/*				if (app->savedStateSize == sizeof(mState) && app->savedState != nullptr) {
					mState = *((NativeEngineSavedState *) mApp->savedState);
					mHasFocus = mState.mHasFocus;
				} else {
					// Workaround APP_CMD_GAINED_FOCUS issue where the focus state is not
					// passed down from NativeActivity when restarting Activity
					mHasFocus = appState.mHasFocus;
				}*/
			}
			//VLOGD("HandleCommand(%d): hasWindow = %d, hasFocus = %d", cmd, mHasWindow ? 1 : 0, mHasFocus ? 1 : 0);
			break;
		case APP_CMD_TERM_WINDOW:
			// The window is going away -- kill the surface
			//VLOGD("NativeEngine: APP_CMD_TERM_WINDOW");
			//KillSurface();
			//mHasWindow = false;
			break;
		case APP_CMD_GAINED_FOCUS:
			//VLOGD("NativeEngine: APP_CMD_GAINED_FOCUS");
			//mHasFocus = true;
			//mState.mHasFocus = appState.mHasFocus = mHasFocus;
			break;
		case APP_CMD_LOST_FOCUS:
			//VLOGD("NativeEngine: APP_CMD_LOST_FOCUS");
			//mHasFocus = false;
			//mState.mHasFocus = appState.mHasFocus = mHasFocus;
			break;
		case APP_CMD_PAUSE:
			//VLOGD("NativeEngine: APP_CMD_PAUSE");
			//mgr->OnPause();
			break;
		case APP_CMD_RESUME:
			//VLOGD("NativeEngine: APP_CMD_RESUME");
			//mgr->OnResume();
			break;
		case APP_CMD_STOP:
			//VLOGD("NativeEngine: APP_CMD_STOP");
			//Paddleboat_onStop(mJniEnv);
			//mIsVisible = false;
			break;
		case APP_CMD_START:
			//VLOGD("NativeEngine: APP_CMD_START");
			//Paddleboat_onStart(mJniEnv);
			//mIsVisible = true;
			break;
		case APP_CMD_WINDOW_RESIZED:
		case APP_CMD_CONFIG_CHANGED:
			//VLOGD("NativeEngine: %s", cmd == APP_CMD_WINDOW_RESIZED ?
			//						  "APP_CMD_WINDOW_RESIZED" : "APP_CMD_CONFIG_CHANGED");
			// Window was resized or some other configuration changed.
			// Note: we don't handle this event because we check the surface dimensions
			// every frame, so that's how we know it was resized. If you are NOT doing that,
			// then you need to handle this event!
			break;
		case APP_CMD_LOW_MEMORY:
			//VLOGD("NativeEngine: APP_CMD_LOW_MEMORY");
			// system told us we have low memory. So if we are not visible, let's
			// cooperate by deallocating all of our graphic resources.
			//if (!mHasWindow) {
			//	VLOGD("NativeEngine: trimming memory footprint (deleting GL objects).");
			//	KillGLObjects();
			//}
			break;
		case APP_CMD_WINDOW_INSETS_CHANGED:
			//VLOGD("NativeEngine: APP_CMD_WINDOW_INSETS_CHANGED");
			//UpdateSystemBarOffset();
			break;
		default:
			//VLOGD("NativeEngine: (unknown command).");
			break;
	}

	//VLOGD("NativeEngine: STATUS: F%d, V%d, W%d, EGL: D %p, S %p, CTX %p, CFG %p",
	//	  mHasFocus, mIsVisible, mHasWindow, mEglDisplay, mEglSurface, mEglContext, mEglConfig);
}

/**
 * This is the main entry point of a native application that is using
 * android_native_app_glue.  It runs in its own thread, with its own
 * event loop for receiving input events and doing other things.
 */
void android_main(android_app* app)
{
	AndroidEngine engine;
	engine.init();

    //app->userData = this;
    app->onAppCmd = _handle_cmd_proxy;
    //mApp->onInputEvent = _handle_input_proxy;

    //auto activity = NativeEngine::GetInstance()->GetAndroidApp()->activity;
    //GameActivity_setWindowFlags(activity,
     //                           AWINDOW_FLAG_KEEP_SCREEN_ON | AWINDOW_FLAG_TURN_SCREEN_ON |
     //                           AWINDOW_FLAG_FULLSCREEN | AWINDOW_FLAG_SHOW_WHEN_LOCKED, 0);

    //UpdateSystemBarOffset();

	android_app_set_key_event_filter(app, NULL);
	android_app_set_motion_event_filter(app, NULL);

	while (1)
	{
		// Read all pending events.
		int events;
		struct android_poll_source* source;

		// If not animating, block forever waiting for events.
		// If animating, loop until all events are read, then continue
		// to draw the next frame of animation.
		while ((ALooper_pollAll(true ? 0 : -1, nullptr, &events, (void**)&source)) >= 0)
        {
			// Process this app cycle or inset change event.
			if (source) {
				source->process(source->app, source);
			}

			//... // Other processing.

			// Check if app is exiting.
			if (app->destroyRequested) {
				//engine_term_display(&engine);
				engine.cleanup();
				return;
			}
		}
		// Process input events if there are any.
		engine_handle_input(app);

		engine.update();
		//if (engine.animating) {
			// Draw a game frame.
		//}
	}

}