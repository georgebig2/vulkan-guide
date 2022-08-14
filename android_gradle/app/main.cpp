
#include <iostream>

#include <game-activity/native_app_glue/android_native_app_glue.h>
#define VK_USE_PLATFORM_ANDROID_KHR
#include "rengine.h"
#include <asset_loader.h>
#include <android/asset_manager.h>
#include <android/window.h>

static float dpi = 1.f;


class AndroidEngine : public REngine {
public:

	AndroidEngine( ANativeWindow *&window_, AAssetManager* assetsMgr_) : window{window_}, assetsMgr(assetsMgr_){}
	ANativeWindow *&window;
	AAssetManager* assetsMgr;

	bool create_surface(VkInstance instance, VkSurfaceKHR* surface) override
	{
		VkAndroidSurfaceCreateInfoKHR info{ VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR };
		info.window = window;
		VK_CHECK(vkCreateAndroidSurfaceKHR(instance, &info, nullptr, surface));
		return true;
	}

	std::vector<uint32_t> load_file(const char* path) override
	{
		AAsset* infile = AAssetManager_open(assetsMgr, path, AASSET_MODE_BUFFER);
		if (!infile)
			return std::move(std::vector<uint32_t>());

		auto len = AAsset_getLength(infile) / sizeof(uint32_t);
		auto buf = (uint32_t*)AAsset_getBuffer(infile);

		auto buffer = std::vector<uint32_t>(buf, buf+len);
		return std::move(buffer);
	}

	bool load_asset(const char* path, assets::AssetFile& outputFile) override
	{
		AAsset* infile = AAssetManager_open(assetsMgr, path, AASSET_MODE_STREAMING);
		if (!infile)
			return false;

		AAsset_read(infile, &outputFile.type, 4);
		AAsset_read(infile, &outputFile.version, sizeof(uint32_t));

		uint32_t jsonlen = 0;
		AAsset_read(infile, &jsonlen, sizeof(uint32_t));

		uint32_t bloblen = 0;
		AAsset_read(infile, &bloblen, sizeof(uint32_t));

		outputFile.json.resize(jsonlen);
		AAsset_read(infile, (void*)outputFile.json.data(), jsonlen);

		outputFile.binaryBlob.resize(bloblen);
		AAsset_read(infile, outputFile.binaryBlob.data(), bloblen);

		return true;
	}

	std::string asset_path(std::string_view path) override
	{
		return "assets_export/" + std::string(path);
	}
	std::string shader_path(std::string_view path) override
	{
		return "shaders/" + std::string(path);
	}

	float get_dpi_factor() override
	{
		return dpi;
	}

};

static AndroidEngine* engine = 0;
static bool is_paused = false;
static InputData input;



static int32_t engine_handle_input(struct android_app* app)
{
	//auto* engine = (struct engine*)app->userData;
	auto ib = android_app_swap_input_buffers(app);

	if (ib && ib->motionEventsCount)
    {
        input.touch = 0;
		//input.press = 0;
		int32_t ptrIdx = 0;
		for (int i = 0; i < ib->motionEventsCount; i++)
		{
			auto *event = &ib->motionEvents[i];
			switch (event->action & AMOTION_EVENT_ACTION_MASK)
			{
				case AMOTION_EVENT_ACTION_POINTER_DOWN:
					ptrIdx = (event->action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
					input.press = ptrIdx == 1 ? 1.f : -1;
					break;
				case AMOTION_EVENT_ACTION_POINTER_UP:
					// Retrieve the index for the starting and the ending of any secondary pointers
					ptrIdx = (event->action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
					input.press = 0.f;
					break;
				case AMOTION_EVENT_ACTION_DOWN:
					if (ptrIdx == 0)
					{
						input.touch = 1.f;
						input.touch_x = GameActivityPointerAxes_getAxisValue(
							&event->pointers[ptrIdx], AMOTION_EVENT_AXIS_X);
						input.touch_y = GameActivityPointerAxes_getAxisValue(
							&event->pointers[ptrIdx], AMOTION_EVENT_AXIS_Y);
					}
					break;
				case AMOTION_EVENT_ACTION_UP:
					if (ptrIdx == 0)
	                    input.touch = 1.f;
					break;
				case AMOTION_EVENT_ACTION_MOVE:
					// Process the move action: the new coordinates for all active touch pointers
					// are inside the event->pointers[]. Compare with our internally saved
					// coordinates to find out which pointers are actually moved. Note that there is
					// no index embedded inside event->action for AMOTION_EVENT_ACTION_MOVE (there
					// might be multiple pointers moved at the same time).
					if (ptrIdx == 0)
					{
						input.touch_x = GameActivityPointerAxes_getAxisValue(
							&event->pointers[ptrIdx], AMOTION_EVENT_AXIS_X);
						input.touch_y = GameActivityPointerAxes_getAxisValue(
							&event->pointers[ptrIdx], AMOTION_EVENT_AXIS_Y);
					}
					else
					{
						//input.press = 1;
					}
					break;
			}
		}

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
		case APP_CMD_CONTENT_RECT_CHANGED:
			// Get the new size
			{
//				auto width = app->contentRect.right - app->contentRect.left;
//				auto height = app->contentRect.bottom - app->contentRect.top;
//				if (engine)
//					engine->resize_window(width, height);

			}
			break;
		case APP_CMD_INIT_WINDOW:
			// We have a window!
			//VLOGD("NativeEngine: APP_CMD_INIT_WINDOW");
			is_paused = false;
			if (!engine && app->window != NULL)
			{
				engine = new AndroidEngine(app->window, app->activity->assetManager);
				//engine->resize_window(ANativeWindow_getWidth(app->window), ANativeWindow_getHeight(app->window));
				dpi = AConfiguration_getDensity(app->config) / static_cast<float>(ACONFIGURATION_DENSITY_MEDIUM);
				engine->init(false);
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
			else
			{
				engine->window = app->window;
			}
			//VLOGD("HandleCommand(%d): hasWindow = %d, hasFocus = %d", cmd, mHasWindow ? 1 : 0, mHasFocus ? 1 : 0);
			break;
		case APP_CMD_TERM_WINDOW:
			if (engine)
			{
				is_paused = true;
				engine->window = 0;
                //engine->resize_window(0, 0);
    //            engine->cleanup();
				//delete engine;
				//engine = 0;
			}
			// The window is going away -- kill the surface
			//VLOGD("NativeEngine: APP_CMD_TERM_WINDOW");
			//KillSurface();
			//mHasWindow = false;
			break;
		case APP_CMD_GAINED_FOCUS:
			is_paused = false;
			//VLOGD("NativeEngine: APP_CMD_GAINED_FOCUS");
			//mHasFocus = true;
			//mState.mHasFocus = appState.mHasFocus = mHasFocus;
			break;
		case APP_CMD_LOST_FOCUS:
			is_paused = true;
			//VLOGD("NativeEngine: APP_CMD_LOST_FOCUS");
			//mHasFocus = false;
			//mState.mHasFocus = appState.mHasFocus = mHasFocus;
			break;
		case APP_CMD_PAUSE:
			is_paused = true;
			//VLOGD("NativeEngine: APP_CMD_PAUSE");
			//mgr->OnPause();
			break;
		case APP_CMD_RESUME:
			is_paused = false;
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
			//engine->resize_window(ANativeWindow_getWidth(app->window), ANativeWindow_getHeight(app->window));
			dpi = AConfiguration_getDensity(app->config) / static_cast<float>(ACONFIGURATION_DENSITY_MEDIUM);
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
	//AndroidEngine engine(app->window);
	//engine.init();

    //app->userData = this;
    app->onAppCmd = _handle_cmd_proxy;
    //mApp->onInputEvent = _handle_input_proxy;

    GameActivity_setWindowFlags(app->activity,
                                AWINDOW_FLAG_KEEP_SCREEN_ON | AWINDOW_FLAG_TURN_SCREEN_ON |
                                AWINDOW_FLAG_FULLSCREEN | AWINDOW_FLAG_SHOW_WHEN_LOCKED, 0);

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
		while ((ALooper_pollAll(!is_paused ? 0 : -1, nullptr, &events, (void**)&source)) >= 0)
        {
			// Process this app cycle or inset change event.
			if (source) {
				source->process(source->app, source);
			}

			// Check if app is exiting.
			if (app->destroyRequested) {
				if (engine) {
					is_paused = true;
					engine->cleanup();
					delete engine;
					engine = 0;
				}
				return;
			}
		}

		engine_handle_input(app);

		if (engine && !is_paused && engine->window) {
			engine->update(input);
		}
	}

}