# VST 3 Documentation
_by Emanuele Messina_

<br>

## Setup the project

<br>

### **Root CMakeLists.txt**

<br>

Configure the root `CMakeLists.txt` to suit your needs, this is a good starting point

<details>
<summary>CMakeLists.txt</summary>

```cmake
cmake_minimum_required(VERSION 3.14.0)

##############################################################################
#                                                                            #
#                                 GLOBALS                                    #
#                                                                            # 
##############################################################################

# SET VST-3-SDK PATH

set(RS_VST3_SDK_DIR "" CACHE PATH "Path to vst3 sdk")

if(NOT EXISTS ${RS_VST3_SDK_DIR})
    message(FATAL_ERROR "Invalid path to vst3 sdk!")
endif()

# SET REAPER PATH (for debugging)

set(RS_REAPER_PATH "S:\\_AUDIO\\Reaper\\reaper.exe" CACHE FILEPATH "Path to reaper executable")

if(NOT EXISTS ${RS_REAPER_PATH})
    message(WARNING "Reaper executable not found.")
endif()

# SET VERSION NUMBER

# set project name
set(PROJECT_NAME ReaShader) # do not change !

project(${PROJECT_NAME}
    # This is your plug-in version number. Change it here only.
    # Version number symbols usable in C++ can be found in
    # source/version.h and ${PROJECT_BINARY_DIR}/projectversion.h.
    VERSION 1.0.0.0 
    DESCRIPTION "${PROJECT_NAME} VST 3 Plug-in"
)

# ADDITIIONAL INCLUDE DIRECTORIES (absolute or relative to this root)

if(DEFINED $ENV{VK_SDK_PATH}) # env var
    message(FATAL_ERROR "Vulkan SDK is not installed!")
endif()

set(RS_GLM_PATH "$ENV{VK_SDK_PATH}/../glm" CACHE FILEPATH "GLM library include path")
set(RS_VMA_PATH "$ENV{VK_SDK_PATH}/../VulkanMemoryAllocator" CACHE FILEPATH "Vulkan Memory Allocator library path (contains include and build)")
set(RS_TOL_PATH "$ENV{VK_SDK_PATH}/../tiny_obj_loader" CACHE FILEPATH "Tiny Obj Loader library include path")
set(RS_STB_PATH "$ENV{VK_SDK_PATH}/../stb" CACHE FILEPATH "STB library include path")

include_directories (
    "$ENV{VK_SDK_PATH}/include"
    "external/reaper-sdk/sdk"
    "external/reaper-sdk/WDL"
    "source/vst3"
    "source/reashader"
    ${RS_GLM_PATH}
    "${RS_VMA_PATH}/include"
    ${RS_TOL_PATH}
    ${RS_STB_PATH}
)

# C++ STANDARD

set(CPP_ISO 20)

# PREPROCESSOR DEFINITIONS

# add_compile_definitions()

# SOURCES

file(GLOB_RECURSE SOURCE_FILES
     "source/*.h"
     "source/*.cpp"

     "source/*.glsl"
     
     "doc/*.md"

     "external/reaper-sdk/WDL/lice/lice.cpp"
)

##############################################################################
#
#                               STEINBERG
#
##############################################################################

# BOOTSTRAPPING VST PLUGIN (sets internal targets and variables)

# set vst3 sdk directory

set(vst3sdk_SOURCE_DIR "${RS_VST3_SDK_DIR}")

if(NOT vst3sdk_SOURCE_DIR)
    message(FATAL_ERROR "Path to VST3 SDK is empty!")
endif()

set(SMTG_VSTGUI_ROOT "${vst3sdk_SOURCE_DIR}")

add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)
smtg_enable_vst3_sdk()

# bind source files (calls add_library and creates sources_list)

smtg_add_vst3plugin(${PROJECT_NAME}
    ${SOURCE_FILES}
)

#- VSTGUI Wanted ----
if(SMTG_ADD_VSTGUI)
    target_sources(${PROJECT_NAME}
        PRIVATE
            resource/myplugineditor.uidesc
    )
    target_link_libraries(${PROJECT_NAME}
        PRIVATE
            vstgui_support
    )
    smtg_target_add_plugin_resources(${PROJECT_NAME}
        RESOURCES
            "resource/myplugineditor.uidesc"
    )
endif(SMTG_ADD_VSTGUI)
# -------------------

smtg_target_add_plugin_snapshots (${PROJECT_NAME}
    RESOURCES
        resource/E0F1D00DC00B511782AD083BE3690071_snapshot.png
        resource/E0F1D00DC00B511782AD083BE3690071_snapshot_2.0x.png
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        sdk
)

smtg_target_configure_version_file(${PROJECT_NAME})

##############################################################################
#
#                   CUSTOM DELAYED (waited for target creation)
#
##############################################################################

# set c++ version
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD ${CPP_ISO})

# ADDITIONAL LIBRARY INCLUDES (needs to be PRIVATE otherwise conflict with SMTG)

target_link_directories(${PROJECT_NAME} PRIVATE
    "$ENV{VK_SDK_PATH}/Lib"
    "${RS_VMA_PATH}/build/src/Debug"
    "${RS_VMA_PATH}/build/src/Release"
)

target_link_libraries(${PROJECT_NAME} PRIVATE 
    opengl32.lib 
    vulkan-1.lib
    debug VulkanMemoryAllocatord.lib
    optimized VulkanMemoryAllocator.lib
)

# file groups (IDE)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SOURCE_FILES})

##############################################################################
#
#                           PLATFORM SPECIFIC
#
##############################################################################

# CONFIGURE MAC

if(SMTG_MAC)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 10.12)
    smtg_target_set_bundle(${PROJECT_NAME}
        BUNDLE_IDENTIFIER gvng.reashader
        COMPANY_NAME "Emanuele Messina"
    )
    smtg_target_set_debug_executable(${PROJECT_NAME}
        ${RS_REAPER_PATH}
    )

# CONFIGURE WIN

elseif(SMTG_WIN)

    # windows only resource file
    #target_sources(${PROJECT_NAME} PRIVATE 
    #    resource/win32resource.rc
    #)

    # VISUAL STUDIO SPECIFIC

    if(MSVC)

        # startup project
        set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
        
        # debugger
        smtg_target_set_debug_executable(${PROJECT_NAME}
            ${RS_REAPER_PATH}
        )
       
    endif()

# CONFIGURE LINUX

elseif(SMTG_LINUX)

    

endif(SMTG_MAC)


```

</details>

<br>

### **Configure Solution**

<br>

After CMake has generated the build files, you may want to customize some settings in the IDE, something that you cant' do with CMake. You can do it with a solution configurator.

- on Windows : **[sln-make](github.com/emanuelemessina/sln-make)**

<br>

### **Visual Studio settings**

<br>

Here are the recommended things to set.
\
Some concepts regarding VST apply to all platforms.

<br>

#### <span style="color:lightgreen">Solution custom property sheet</span>

<br>

Define useful macros in a custom property sheet.
\
The most important one is `VST3BundleOutDir` : path to `myplugin.vst3` bundle folder.
\
\
In fact the generated folder will follow [this output folder structure](#output-folder-structure).

<br>

#### <span style="color:lightgreen">Solution properties overrides</span>

<br>

1. Output Directory: `$(VST3BundleOutDir)\win-$(PlatformTarget)` (Folder of the compiled .vst3 file. The compiled .vst3 file needs to reside in a subfolder next to Resources and moduleinfo.json)
2. Intermediate Directory: `$(TargetBuildDir)\intermediate` (intermediate files, will be next to the deploy folder)
3. Debug command: `$(ReaperDir)\reaper.exe` , Attach: `yes`

<br>

#### <span style="color:lightgreen">Custom build steps overrides</span>

<br>

<details>

<summary> <strong>PRE-LINK</strong> </summary>

<br>

Bootstrap macros in the Visual Studio property box (eg):
```cmd
setlocal

:: convert macros to env vars

set vst3_resources_out_dir=$(VST3ResourcesOutDir)
set project_root_dir=$(ProjectRootDir)
set assets_out_dir="$(AssetsOutDir)"

:: run pre link script

powershell $(ScriptsDir)\pre-link.ps1

endlocal
```
\
Then the actual script. Make sure to adapt the Steinberg default lines. Eg:
```powershell
######## PRE-LINK SCRIPT #########

function log {
    param($msg)

    Write-Host "[ReaShader] $($msg)"
}


function end {
    log "[ERROR] Exited with $($lastexitcode)"
}

#   STEINBERG DEFAULT PART

# create resources directory
cmake -E make_directory "$($env:vst3_resources_out_dir)"
if(-not $?){ end }

# copy uidesc file
cmake -E copy_if_different "$($env:project_root_dir)\resource\myplugineditor.uidesc" "$($env:vst3_resources_out_dir)"
if(-not $?){ end }
cmake -E echo "[SMTG] Copied resource/myplugineditor.uidesc to $($env:vst3_resources_out_dir)"
if(-not $?){ end }

# copy VST logos
cmake -E make_directory "$($env:vst3_resources_out_dir)\Snapshots"
if(-not $?){ end }
cmake -E copy_if_different "$($env:project_root_dir)\resource\E0F1D00DC00B511782AD083BE3690071_snapshot.png" "$($env:vst3_resources_out_dir)\Snapshots"
if(-not $?){ end }
cmake -E echo "[SMTG] Copied resource/E0F1D00DC00B511782AD083BE3690071_snapshot.png to $($env:vst3_resources_out_dir)\Snapshots"
if(-not $?){ end }

cmake -E make_directory "$($env:vst3_resources_out_dir)\Snapshots"
if(-not $?){ end }
cmake -E copy_if_different  "$($env:project_root_dir)\resource\E0F1D00DC00B511782AD083BE3690071_snapshot_2.0x.png" "$($env:vst3_resources_out_dir)\Snapshots"
if(-not $?){ end }
cmake -E echo "[SMTG] Copied resource/E0F1D00DC00B511782AD083BE3690071_snapshot_2.0x.png to $($env:vst3_resources_out_dir)\Snapshots"
if(-not $?){ end }

#   REASHADER

# copy resources
cmake -E copy_directory "$($env:project_root_dir)\source\shaders" "$($env:assets_out_dir)\shaders"
if(-not $?){ end }
log "Copied shaders"

cmake -E copy_directory "$($env:project_root_dir)\resource\images" "$($env:assets_out_dir)\images"
if(-not $?){ end }
log "Copied images"

cmake -E copy_directory "$($env:project_root_dir)\resource\meshes" "$($env:assets_out_dir)\meshes"
if(-not $?){ end }
log "Copied meshes"

```

</details>

<br>

<details>

<summary> <strong>POST-BUILD</strong> </summary>

<br>

Again, bootstrap the macros, then call the script, make sure to adapt Steinberg default lines. Eg:
```cmd
setlocal

:: convert macros to env vars

set vst3_resources_out_dir=$(VST3ResourcesOutDir)
set project_root_dir=$(ProjectRootDir)
set assets_out_dir=$(AssetsOutDir)
set msbuild_project_dir=$(MSBuildProjectDirectory)
set target_path=$(TargetPath)
set vst3_bundle_out_dir=$(VST3BundleOutDir)

:: run post-build script

powershell $(ScriptsDir)\post-build.ps1 -skipValidator

endlocal
```

<br>

```powershell
######### POST BUILD #########

param (
    [switch]$skipValidator = $false
)

function log {
    param($msg)

    Write-Host "[ReaShader] $($msg)"
}

function end {
    log "[ERROR] Exited with $($lastexitcode)"
    Exit
}

#    STEINBERG DEFAULT

# generate vst module info
& "$($env:msbuild_project_dir)\bin\Debug\moduleinfotool.exe" -create -version 1.0.0.0 -path "$($env:target_path)" -output "$($env:vst3_bundle_out_dir)\moduleinfo.json"
if(-not $?){ end }

# run validator
if(-not $skipValidator){
    Write-Host "[SMTG] Validator started..."
    & "$($env:msbuild_project_dir)\bin\Debug\validator.exe" "$($env:target_path)"
    Write-Host "[SMTG] Validator finished."
}

#   REASHADER  

# compile shaders
cd "$($env:project_root_dir)\source\shaders"

glslc -fshader-stage=vert vert.glsl -o vert.spv
if(-not $?){ end }
glslc -fshader-stage=frag frag.glsl -o frag.spv
if(-not $?){ end }
log "Shaders compiled"
```

</details>

<br>

---

<br>

## Project structure

<br>

### VST Info

<br>

Apart from the aforementioned settings, here are the files which need to be modified for VST specific information.

- `version.h`: file and legal
- `mypluginentry.cpp`: plugin and developer
	- plugin display name (eg. can be changed depending on release/debug)
- `CMakeLists.txt`: version number, description, mac bundle

<br>

### Main files

<br>

These are the files that contains the actual VST plugin code.

- `myplugincontroller.cpp`: communication with the DAW, handling parameters and the UI.
- `mypluginprocessor.cpp`: processing and persistence.
- `myplugincids.h`: various ids (the parameter ids enum can be put here). 

<br>

### <p id="#output-folder-structure">Output folder structure</p> 

<br>

```
- myplugin.vst3
	- Resources
		- Snapshots
        myplugineditor.uidesc
	- win-x64 (name it whatever, but must be in this level)
		myplugin.vst3
	moduleinfo.json
```

<br>

---

<br>

## Parameters

<br>

Parameters enum can be declared in `myplugincids.h`:
```c++
// Params (with defaults)

	enum ReaShaderParamIds : Steinberg::Vst::ParamID {
		uAudioGain,
		uVideoParam,

		uNumParams
	};
```
Then a map can be declared inside the Processor class inside `mypluginprocessor.h` to allocate the internal variables and init with defaults:
```c++
std::map<Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue> rParams;
```
> Can be typedef'd for convenience: 
>```c++
>	typedef std::map<Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue> rParamsMap;
>	...
>	rParamsMap rParams;
>```
Then initialized in the Processor constructor with default values:
```c++
 rParams = {
			{ReaShaderParamIds::uAudioGain, 1.f},
			{ReaShaderParamIds::uVideoParam, 0.5f}
		};
```

Simple `setState` and `getState` from the Processor:
```c++
	tresult PLUGIN_API ReaShaderProcessor::setState(IBStream* state)
	{
		if (!state)
			return kResultFalse;

		// called when we load a preset or project, the model has to be reloaded
		IBStreamer streamer(state, kLittleEndian);

		// make sure everything is read in order (in the exact way it was saved)

		for (auto const& [uParamId, value] : rParams) {
			float savedParam = 0.f;
			if (streamer.readFloat((float)savedParam) == false)
				return kResultFalse;
			rParams.at(uParamId) = savedParam;
		}

		return kResultOk;
	}

    tresult PLUGIN_API ReaShaderProcessor::getState(IBStream* state)
	{
		// here we need to save the model
		IBStreamer streamer(state, kLittleEndian);

		// make sure everything is saved in order
		for (auto const& [uParamId, value] : rParams) {
			streamer.writeFloat(value);
		}

		return kResultOk;
	}
```
As well as simple `setComponentState` from Controller (to update the UI):
```c++
tresult PLUGIN_API ReaShaderController::setComponentState(IBStream* state)
	{
		// Here you get the state of the component (Processor part)
		if (!state)
			return kResultFalse;

		// basically the same code in the processor

		IBStreamer streamer(state, kLittleEndian);

		for (int uParamId = 0; uParamId < uNumParams; uParamId++) {
			float savedParam = 0.f;
			if (streamer.readFloat((float)savedParam) == false)
				return kResultFalse;
			// in the processor: rParams.at(uParamId) = savedParam;
			setParamNormalized(uParamId, savedParam);
		}

		return kResultOk;
    }
```
Parameters are registered in the controller `initialize` method:
```c++

		parameters.addParameter(STR16("Gain"), STR16("dB"), 0, .5, Vst::ParameterInfo::kCanAutomate, ReaShaderParamIds::uAudioGain);
		parameters.addParameter(STR16("Video Param"), STR16("units"), 0, .5, Vst::ParameterInfo::kCanAutomate, ReaShaderParamIds::uVideoParam);
```

<br>

---

<br>

## FUnknown

<br>

_Convenient snippets and doc collected while wandering through the sdk files..._

<br>


From `funknown.h`:
```c++
/**	The basic interface of all interfaces.
\ingroup pluginBase

- The FUnknown::queryInterface method is used to retrieve pointers to other
  interfaces of the object.
- FUnknown::addRef and FUnknown::release manage the lifetime of the object.
  If no more references exist, the object is destroyed in memory.

Interfaces are identified by 16 byte Globally Unique Identifiers.
The SDK provides a class called FUID for this purpose.

\ref howtoClass
*/

...

	/** Query for a pointer to the specified interface.
	Returns kResultOk on success or kNoInterface if the object does not implement the interface.
	The object has to call addRef when returning an interface.
	\param _iid : (in) 16 Byte interface identifier (-> FUID)
	\param obj : (out) On return, *obj points to the requested interface */
	virtual tresult PLUGIN_API queryInterface (const TUID _iid, void** obj) = 0;
```

<br>

---

<br>

## Reaper aware VST

<br>

### <span style="color:lightgreen">API Connection</span>

<br>

>**ALWAYS CHECK IF THE POINTERS ARE NULL BEFORE UTILIZING THEM!**

<br>

#### **VST 2.4**

<br>

The context call must be made inside or after `effOpen` dispatcher event, otherwise the returned context will be null.
Careful also to pass a non-empty/correctly-initialized `m_effect` object, otherwise the returned context will be null anywhere.
Context call: 
```c++
// get host context
	void* ctx = (void*)audioMaster(&cEffect, 0xdeadbeef, 0xdeadf00e, 4, NULL, 0.0f);
```
Then it can be fed to api functions obtained with another audioMaster call, eg:
```c++
// get api function
 *(void**)&video_CreateVideoProcessor = (void*)audioMaster(&cEffect, 0xdeadbeef, 0xdeadf00d, 0, "video_CreateVideoProcessor", 0.0f);

 ...

// use api function
 m_videoproc = video_CreateVideoProcessor(ctx, IREAPERVideoProcessor::REAPER_VIDEO_PROCESSOR_VERSION);
 ```

<br>

#### **VST 3**

<br>

The audioMasterCallback does not exist anymore. Reaper API must be queried through the appropriate vst3 interface declared in `reaper_vst3_interfaces.h`.
To ensure that the Reaper Host Context is initialized, the call can be made in the Processor `setActive` method.

For example, after checking that the state is active:
```c++
tresult PLUGIN_API ReaShaderProcessor::setActive(TBool state){
		//--- called when the Plug-in is enable/disable (On/Off) -----

	if (state) {
        // REAPER API (here we are after effOpen equivalent in vst3) 

        // query reaper interface
        IReaperHostApplication* reaperApp;
        if (context->queryInterface(IReaperHostApplication_iid, (void**)&reaperApp) == kResultOk) {
            // utilize reaperApp as a host communication bridge
            
            // get host context
            void* ctx = reaperApp->getReaperParent(4);

            // get api function
            *(void**)&video_CreateVideoProcessor = (void*)reaperApp->getReaperApi("video_CreateVideoProcessor");

            // feed ctx to reaper api function
            m_videoproc = video_CreateVideoProcessor(ctx, IREAPERVideoProcessor::REAPER_VIDEO_PROCESSOR_VERSION);
        }
	}
	else {
        if(m_videoproc)
		    delete m_videoproc; // --> processing functions continue running and crashes (terminate or destructor do not work if put there)
	}
    ...
}
```

<br>

**IMPORTANT**: make sure to free Reaper API objects as soon as possible otherwise Reaper will crash on VST removal for various reasons.

<br>

---

<br>

### <span style="color:lightgreen">IREAPERVideoProcessor</span>

<br>

After creating the video processor from `video_CreateVideoProcessor`, the callbacks pointers need to be set.
<br>
They need to be static methods, otherwise there will be a hell of pointer-type conversion errors.
<br>
The problem is to pass data between the current Processor instance (the one who instantiated the IREAPERVideoProcessor) and the video processor.
<br>
First thing first, it is crucial to make the video processor point to the rParams (internal processor param state map), but also other data can be pass if userdata is made to point to an array of pointers.
<br>
Fortunately, the IREAPERVideoProcessor class specifies a `void* userdata` pointer that can be set to point to some data in the current Processor instance.

<br>

### <span style="color:lightgreen">RSData</span>

<br>

`RSData` is a helper class that does just that.

<br>

>Eg. we want to pass a `myColor` to the video processor (in addition to the rParams).

<br>

Declare the `RSData` object in the Processor, in addition to the rParamsMap:
```c++
	rParamsMap rParams;
	RSData* m_data;

	...

	int myColor {5}; // we want to pass this to the video processor
```
In the Processor constructor, initialize the `RSData` object with how many items needed to pass:
```c++
	m_data = new RSData(2);
```
Then push the item pointers (do not exceed the initial items size specified, `push` actually returs null in that case):
```c++
	m_data->push(&rParams);
	m_data->push(&myColor);
```
When the video processor is created, pass the `RSData` object instance:
```c++
	m_videoproc->userdata = m_data;
```

<br>

Now the items can be retrieved from the static callbacks (eg):
```c++
bool getVideoParam(IREAPERVideoProcessor* vproc, int idx, double* valueOut)
{
	// called from video thread, gets current state

	if (idx >= 0 && idx < ReaShader::uNumParams)
	{
		RSData* rsData = (RSData*)vproc->userdata; // cast as RSData instance

		// directly pass parameters to the video processor

		*valueOut = (*(rParamsMap*)rsData->get(0)).at(idx);
		// get the first item (rParams map) , cast as wanted type (in this case we know it's rParamsMap)
		
		return true;
	}
	return false;
}

IVideoFrame* processVideoFrame(IREAPERVideoProcessor* vproc, const double* parmlist, int nparms, double project_time, double frate, int force_format)
{
	...
	RSData* rsData = (RSData*)vproc->userdata; // cast as RSData instance
	...
	int thatColor = *(int*)rsData->get(1) // myColor was pushed as second item
	...
}
```

<br>

---

<br>

### <span style="color:lightgreen">IVideoFrame</span>

<br>

`IVideoFrame` is actually very similar to a `LICE_IBitmap`.
In fact, a `LICE_IBitmap` can be constructed from an `IVideoFrame`, provided that we render it with `RGBA` color format.

```c++
// LICE_WrapperBitmap(LICE_pixel *buf, int w, int h, int span, bool flipped)

LICE_WrapperBitmap* bitmap = new LICE_WrapperBitmap((LICE_pixel*)bits, w, h, w, false);
```

<br>

*Remarks*

- LICE defines its rowSpan as number of LICE_pixels per line 
(eg the number of 32 bit ints per line), whereas the IVideoFrame uses bytes per line. 
So `vf->get_rowspan()` will return 4 times the `bitmap->get_rowspan()`.
That's why in the bitmap constructor 1/4 of the vf roswpan should be used, effectively making it equal to `w`.

<br>

---

<br>

## VSTGUI

<br>

### Resources

<br>

On Windows only, the resources can be referenced from a `.rc` file that must be linked to the solution with a CMake directive.
\
However in UNIX systems the resources are not embedded and must be present in the `Resources` output folder, so you might as well go with this universal method.
\
If you use the `.rc` file, the resources are accessed by their ID, and that tag will not exist in the UNIX build.
\
\
Instead, ditch the `.rc` file and place the resources in the `Resources` directory, then either using the `CView(UIDescription)` constructor or the VSTEditor `.uidesc` file, reference resource files with their relative path from the `Resources` folder.