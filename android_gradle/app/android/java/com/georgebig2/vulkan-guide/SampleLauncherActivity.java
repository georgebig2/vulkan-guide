/* Copyright (c) 2019-2021, Arm Limited and Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 the "License";
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.georgebig2.vulkan;

import android.os.Build.VERSION;
import android.os.Build.VERSION_CODES;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager.LayoutParams;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;
import com.google.androidgamesdk.GameActivity;

import android.widget.Toast;


public class SampleLauncherActivity extends GameActivity  {

    //SampleListView sampleListView;
    //PermissionView permissionView;

    // Some code to load our native library:
    static {
        // Load the STL first to workaround issues on old Android versions:
        // "if your app targets a version of Android earlier than Android 4.3
        // (Android API level 18),
        // and you use libc++_shared.so, you must load the shared library before any other
        // library that depends on it."
        // See https://developer.android.com/ndk/guides/cpp-support#shared_runtimes
        //System.loadLibrary("c++_shared");

        // Optional: reload the memory advice library explicitly (it will be loaded
        // implicitly when loading agdktunnel library as a dependent library)
        //System.loadLibrary("memory_advice");

        // Optional: reload the native library.
        // However this is necessary when any of the following happens:
        //     - agdktunnel library is not configured to the following line in the manifest:
        //        <meta-data android:name="android.app.lib_name" android:value="agdktunnel" />
        //     - GameActivity derived class calls to the native code before calling
        //       the super.onCreate() function.
        System.loadLibrary("vulkan_guide");
    }

    private boolean isBenchmarkMode = false;
    private boolean isHeadless = false;

    //public SampleStore samples;

    // Required sample permissions
    //List<Permission> permissions;

    @Override
    protected void onCreate(Bundle savedInstanceState)
    {
        // When true, the app will fit inside any system UI windows.
        // When false, we render behind any system UI windows.
        WindowCompat.setDecorFitsSystemWindows(getWindow(), false);
        //hideSystemUI();
        super.onCreate(savedInstanceState);

        // You can set IME fields here or in native code using GameActivity_setImeEditorInfoFields.
        // We set the fields in native_engine.cpp.
        // super.setImeEditorInfoFields(InputType.TYPE_CLASS_TEXT,
        //     IME_ACTION_NONE, IME_FLAG_NO_FULLSCREEN );
        //setContentView(savedInstanceState);

        //Toolbar toolbar = findViewById(R.id.toolbar);
        //setSupportActionBar(toolbar);

/*        if (loadNativeLibrary(getResources().getString(R.string.native_lib_name))) {
            // Initialize cpp android platform
            File external_files_dir = getExternalFilesDir("");
            File temp_files_dir = getCacheDir();
            if (external_files_dir != null && temp_files_dir != null) {
                // User no longer has permissions to access applications' storage, save files in
                // top level (shared) external storage directory
                String shared_storage = external_files_dir.getPath().split(Pattern.quote("Android"))[0];
                external_files_dir = new File(shared_storage, getPackageName());
                initFilePath(external_files_dir.toString(), temp_files_dir.toString());
            }

            // Get sample info from cpp cmake generated file
            samples = new SampleStore(Arrays.asList(getSamples()));
        }*/

 /*       // Required Permissions
        permissions = new ArrayList<>();
        permissions.add(new Permission(Manifest.permission.WRITE_EXTERNAL_STORAGE, 1));
        permissions.add(new Permission(Manifest.permission.READ_EXTERNAL_STORAGE, 2));

        if (checkPermissions()) {
            // Permissions previously granted skip requesting them
            parseArguments();
            showSamples();
        } else {
            // Chain request permissions
            requestNextPermission();
        }*/

     /*   if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager())
            {
                // Prompt the user to "Allow access to all files"
                Intent intent = new Intent();
                                    intent.setAction(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                                    Uri uri = Uri.fromParts("package", this.getPackageName(), null);
                                    intent.setData(uri);
                startActivity(intent);
            }
        }*/
    }


    private void hideSystemUI() {
        // This will put the game behind any cutouts and waterfalls on devices which have
        // them, so the corresponding insets will be non-zero.
        if (VERSION.SDK_INT >= VERSION_CODES.P) {
            getWindow().getAttributes().layoutInDisplayCutoutMode
                    = LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
        }
        // From API 30 onwards, this is the recommended way to hide the system UI, rather than
        // using View.setSystemUiVisibility.
        View decorView = getWindow().getDecorView();
        WindowInsetsControllerCompat controller = new WindowInsetsControllerCompat(getWindow(),
                decorView);
        controller.hide(WindowInsetsCompat.Type.systemBars());
        controller.hide(WindowInsetsCompat.Type.displayCutout());
        controller.setSystemBarsBehavior(
                WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
    }

 /*   @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        super.onCreateOptionsMenu(menu);
        getMenuInflater().inflate(R.menu.main_menu, menu);
        return true;
    }*/

   /* @Override
    public boolean onPrepareOptionsMenu(Menu menu) {
        super.onPrepareOptionsMenu(menu);
        MenuItem checkable = menu.findItem(R.id.menu_benchmark_mode);
        checkable.setChecked(isBenchmarkMode);

        checkable = menu.findItem(R.id.menu_headless);
        checkable.setChecked(isHeadless);
        return true;
    }*/

   /* @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        if(item.getItemId() == R.id.filter_button) {
            sampleListView.dialog.show(getSupportFragmentManager(), "filter");
            return true;
        } else if(item.getItemId() == R.id.menu_run_samples) {
            String category = "";
            ViewPagerAdapter adapter = ((ViewPagerAdapter) sampleListView.viewPager.getAdapter());
            if (adapter != null) {
                category = adapter.getCurrentFragment().getCategory();
            }

            List<String> arguments = new ArrayList<>();
            arguments.add("batch");
            arguments.add("--category");
            arguments.add(category);
            arguments.addAll(sampleListView.dialog.getFilter());

            String[] sa = {};
            launchWithCommandArguments(arguments.toArray(sa));
            return true;
        } else if(item.getItemId() == R.id.menu_benchmark_mode) {
            isBenchmarkMode = !item.isChecked();
            item.setChecked(isBenchmarkMode);
            return true;
        } else if(item.getItemId() == R.id.menu_headless) {
            isHeadless = !item.isChecked();
            item.setChecked(isHeadless);
            return true;
        } else {
            return super.onOptionsItemSelected(item);
        }
    }*/

    /*@Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions,
            @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        requestNextPermission();
    }*/

    /**
     * Load a native library
     *
     * @param native_lib_name Native library to load
     * @return True if loaded correctly; False otherwise
     */
/*    private boolean loadNativeLibrary(String native_lib_name) {
        boolean status = true;

        try {
            System.loadLibrary(native_lib_name);
        } catch (UnsatisfiedLinkError e) {
            Toast.makeText(getApplicationContext(), "Native code library failed to load.", Toast.LENGTH_SHORT).show();

            status = false;
        }

        return status;
    }*/

    /**
     * Chain request permissions from the required permission list. When called the
     * next unrequested permission with be requested If all permission are requested
     * but not all granted; requested states will be reset and the PermissionView
     * will be shown
     */
   /* public void requestNextPermission() {
        boolean granted = true;

        for (Permission permission : permissions) {

            // Permission previously requested but not granted
            if (!permission.granted(getApplication())) {
                // Permission not previously requested - request it
                if (!permission.isRequested()) {
                    permission.request(this);
                    return;
                }

                granted = false;
            }
        }

        if (granted) {
            parseArguments();
            showSamples();
        } else {
            // Reset all permissions request state so they can be requested again
            for (Permission permission : permissions) {
                permission.setRequested(false);
            }
            showPermissionsMessage();
        }
    }*/

    /**
     * Check all required permissions have been granted
     *
     * @return True if all granted; False otherwise
     */
   /* public boolean checkPermissions() {
        for (Permission permission : permissions) {
            if (!permission.granted(getApplicationContext())) {
                return false;
            }
        }

        return true;
    }*/

    /**
     * Show/Create the Permission View. Hides all other views
     */
   /* public void showPermissionsMessage() {
        if (permissionView == null) {
            permissionView = new PermissionView(this);
        }

        if (sampleListView != null) {
            sampleListView.hide();
        }

        permissionView.show();
    }*/

    /**
     * Show/Create the Sample View. Hides all other views
     */
    /*public void showSamples() {
        if (sampleListView == null) {
            sampleListView = new SampleListView(this);
        }

        if (permissionView != null) {
            permissionView.hide();
        }

        sampleListView.show();
    }

    // Allow Orientation locking for testing
    @SuppressLint("SourceLockedOrientationActivity")
    public void parseArguments() {
        // Handle argument passing
        Bundle extras = this.getIntent().getExtras();
        if (extras != null) {
            if (extras.containsKey("cmd")) {
                String[] commands = null;
                String[] command_arguments = extras.getStringArray("cmd");
                String command = extras.getString("cmd");
                if (command_arguments != null) {
                    commands = command_arguments;
                } else if (command != null) {
                    commands = command.split("[ ]+");
                }

                if (commands != null) {
                    for (String cmd : commands) {
                        if (cmd.equals("test")) {
                            setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
                            break;
                        }
                    }
                    launchWithCommandArguments(commands);
                    finishAffinity();
                }

            } else if (extras.containsKey("sample")) {
                launchSample(extras.getString("sample"));
                finishAffinity();
            } else if (extras.containsKey("test")) {
                launchTest(extras.getString("test"));
                finishAffinity();
            }

        }
    }
*/
    /**
     * Set arguments of the Native Activity You may also use a String[]
     *
     * @param args A string array of arguments
     */
   /* public void setArguments(String... args) {
        List<String> arguments = new ArrayList<>(Arrays.asList(args));
        if (isBenchmarkMode) {
            arguments.add("--benchmark");
        }
        if (isHeadless) {
            arguments.add("--headless");
        }
        String[] argArray = new String[arguments.size()];
        sendArgumentsToPlatform(arguments.toArray(argArray));
    }
*/
   /* public void launchWithCommandArguments(String... args) {
        setArguments(args);
        Intent intent = new Intent(SampleLauncherActivity.this, NativeSampleActivity.class);
        startActivity(intent);
    }

    public void launchTest(String testID) {
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        launchWithCommandArguments("test", testID);
    }*/

   /* public void launchSample(String sampleID) {
        Sample sample = samples.findByID(sampleID);
        if (sample == null) {
            Notifications.toast(this, "Could not find sample " + sampleID);
            showSamples();
            return;
        }
        launchWithCommandArguments("sample", sampleID);
    }*/

    /**
     * Get samples from the Native Application
     *
     * @return A list of Samples that are currently installed in the Native
     *         Application
     */
    //private native Sample[] getSamples();

    /**
     * Set the arguments of the Native Application
     *
     * @param args The arguments that are to be passed to the app
     */
    private native void sendArgumentsToPlatform(String[] args);

    /**
     * @brief Initiate the file system for the Native Application
     */
    //private native void initFilePath(String external_dir, String temp_path);
}
