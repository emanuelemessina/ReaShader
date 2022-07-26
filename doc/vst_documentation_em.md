# VST 3 Documentation
_by Emanuele Messina_

<br>

[vst_sdk_bundle](https://github.com/emanuelemessina/vst-sdk-bundle), which bundles several SDKs needed to build a VST, included the fixed reaper-sdk, is needed.

<br>

## **Preliminary**

<br>

### <span style="color:lightgreen">Solution custom property sheet</span>

<br>

Define the following macros in a custom property sheet:

- TargetBuildDir : `$(MSBuildProjectDirectory)\VST3\$(Configuration)\$(PlatformTarget)` (output files main directory)
- VST3BundleOutDir : `$(TargetBuildDir)\$(ProjectName).vst3` (folder where all the compiled vst3 files will reside, this is the folder to be deployed)

<br>

### <span style="color:lightgreen">Solution properties overrides</span>

<br>

1. Output Directory: `$(VST3BundleOutDir)\win-$(PlatformTarget)` (Folder of the compiled .vst3 file. The compiled .vst3 file needs to reside in a subfolder next to Resources and moduleinfo.json)
2. Intermediate Directory: `$(TargetBuildDir)\intermediate` (intermediate files, will be next to the deploy folder)
3. Debug command: `$(ReaperDir)\reaper.exe` , Attach: `yes`
4. Additional include directories: `$(MSBuildProjectDirectory)` and vst3sdk path (ok if absolute)
5. Generate Program Database File: `$(MSBuildProjectDirectory)\WIN_PDB64\$(Configuration)\ReaShader.pdb`
5. Import Library: `$(MSBuildProjectDirectory)\lib\$(Configuration)\ReaShader.lib`
5. Resources Additional Include Directories: same as C++ Additional Include Directories
5. Preprocessor Definitions
	- DEBUG:
		```
		%(PreprocessorDefinitions)
		WIN32
		_DEBUG
		_WINDOWS
		SMTG_RENAME_ASSERT=1
		DEVELOPMENT=1
		_UNICODE
		VSTGUI_LIVE_EDITING=1
		VSTGUI_ENABLE_DEPRECATED_METHODS=1
		SMTG_MODULE_IS_BUNDLE=1
		CMAKE_INTDIR=\"Debug\"
		$(TargetName)_EXPORTS
		```
	- RELEASE:
		```
		%(PreprocessorDefinitions)
		WIN32
		_WINDOWS
		NDEBUG
		SMTG_RENAME_ASSERT=1
		RELEASE=1
		_UNICODE
		VSTGUI_ENABLE_DEPRECATED_METHODS=1
		SMTG_MODULE_IS_BUNDLE=1
		CMAKE_INTDIR=\"Release\"
		$(TargetName)_EXPORTS
		```
6. Pre-Link Event:
```bat
setlocal
"C:\Program Files\CMake\bin\cmake.exe" -E make_directory $(VST3BundleOutDir)\Resources
if %errorlevel% neq 0 goto :cmEnd
"C:\Program Files\CMake\bin\cmake.exe" -E copy_if_different $(MSBuildProjectDirectory)\..\resource\myplugineditor.uidesc $(VST3BundleOutDir)\Resources
if %errorlevel% neq 0 goto :cmEnd
"C:\Program Files\CMake\bin\cmake.exe" -E echo "[SMTG] Copied resource/myplugineditor.uidesc to $(VST3BundleOutDir)\Resources"
if %errorlevel% neq 0 goto :cmEnd
:cmEnd
endlocal & call :cmErrorLevel %errorlevel% & goto :cmDone
:cmErrorLevel
exit /b %1
:cmDone
if %errorlevel% neq 0 goto :VCEnd
setlocal
"C:\Program Files\CMake\bin\cmake.exe" -E make_directory $(VST3BundleOutDir)\Resources\Snapshots
if %errorlevel% neq 0 goto :cmEnd
"C:\Program Files\CMake\bin\cmake.exe" -E copy_if_different $(MSBuildProjectDirectory)\..\resource\E0F1D00DC00B511782AD083BE3690071_snapshot.png $(VST3BundleOutDir)\Resources\Snapshots
if %errorlevel% neq 0 goto :cmEnd
"C:\Program Files\CMake\bin\cmake.exe" -E echo "[SMTG] Copied resource/E0F1D00DC00B511782AD083BE3690071_snapshot.png to $(VST3BundleOutDir)\Resources\Snapshots"
if %errorlevel% neq 0 goto :cmEnd
:cmEnd
endlocal & call :cmErrorLevel %errorlevel% & goto :cmDone
:cmErrorLevel
exit /b %1
:cmDone
if %errorlevel% neq 0 goto :VCEnd
setlocal
"C:\Program Files\CMake\bin\cmake.exe" -E make_directory $(VST3BundleOutDir)\Resources\Snapshots
if %errorlevel% neq 0 goto :cmEnd
"C:\Program Files\CMake\bin\cmake.exe" -E copy_if_different  $(MSBuildProjectDirectory)\..\resource\E0F1D00DC00B511782AD083BE3690071_snapshot_2.0x.png $(VST3BundleOutDir)\Resources\Snapshots
if %errorlevel% neq 0 goto :cmEnd
"C:\Program Files\CMake\bin\cmake.exe" -E echo "[SMTG] Copied resource/E0F1D00DC00B511782AD083BE3690071_snapshot_2.0x.png to $(VST3BundleOutDir)\Resources\Snapshots"
if %errorlevel% neq 0 goto :cmEnd
:cmEnd
endlocal & call :cmErrorLevel %errorlevel% & goto :cmDone
:cmErrorLevel
exit /b %1
:cmDone
if %errorlevel% neq 0 goto :VCEnd
```
7. Post-Build Event:
```bat
setlocal
cd $(MSBuildProjectDirectory)\bin
if %errorlevel% neq 0 goto :cmEnd
D:
if %errorlevel% neq 0 goto :cmEnd
 $(MSBuildProjectDirectory)\bin\Debug\moduleinfotool.exe -create -version 1.0.0.0 -path $(TargetPath) -output $(VST3BundleOutDir)\moduleinfo.json
if %errorlevel% neq 0 goto :cmEnd
:cmEnd
endlocal & call :cmErrorLevel %errorlevel% & goto :cmDone
:cmErrorLevel
exit /b %1
:cmDone
if %errorlevel% neq 0 goto :VCEnd
setlocal
cd $(MSBuildProjectDirectory)\bin
if %errorlevel% neq 0 goto :cmEnd
D:
if %errorlevel% neq 0 goto :cmEnd
echo [SMTG] Validator started...
if %errorlevel% neq 0 goto :cmEnd
$(MSBuildProjectDirectory)\bin\Debug\validator.exe $(TargetPath)
if %errorlevel% neq 0 goto :cmEnd
echo [SMTG] Validator finished.
if %errorlevel% neq 0 goto :cmEnd
:cmEnd
endlocal & call :cmErrorLevel %errorlevel% & goto :cmDone
:cmErrorLevel
exit /b %1
:cmDone
if %errorlevel% neq 0 goto :VCEnd
setlocal
echo [SMTG] Delete previous link...
if %errorlevel% neq 0 goto :cmEnd
:: # line commented out-># rmdir C:\Users\emanuele\AppData\Local\Programs\Common\VST3\ReaShader.vst3 & del C:\Users\emanuele\AppData\Local\Programs\Common\VST3\ReaShader.vst3
if %errorlevel% neq 0 goto :cmEnd
echo [SMTG] Creation of the new link...
if %errorlevel% neq 0 goto :cmEnd
:: # line commented out-># mklink /D C:\Users\emanuele\AppData\Local\Programs\Common\VST3\ReaShader.vst3 $(VST3BundleOutDir)
if %errorlevel% neq 0 goto :cmEnd
echo [SMTG] Finished.
if %errorlevel% neq 0 goto :cmEnd
:cmEnd
endlocal & call :cmErrorLevel %errorlevel% & goto :cmDone
:cmErrorLevel
exit /b %1
:cmDone
if %errorlevel% neq 0 goto :VCEnd
```

### <span style="color:lightgreen">TODOs</span>

8. Organize subfolders in solution

<br>

---

<br>

## **Project structure**

<br>

### <span style="color:lightgreen">VST Info</span>

- `version.h`: file and legal
- `mypluginentry.cpp`: plugin and developer
	- plugin display name (eg. can be changed depending on release/debug)
- `CMakeLists.txt`: version number and mac bundle

### <span style="color:lightgreen">Main files</span>

- `myplugincontroller.cpp`: communication with the DAW, handling parameters and the UI.
- `mypluginprocessor.cpp`: processing and persistence.
- `win32resource.rc`: resources file.
- `myplugincids.h`: various ids (the parameter ids enum can be put here). 

<br>

### <span style="color:lightgreen">Parameters</span>

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
std::map<Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue> rParams
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

## **FUnknown**

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

## **Reaper aware VST**

<br>

### <span style="color:lightgreen">API Connection</span>

<br>

**ALWAYS CHECK IF THE POINTERS ARE NULL BEFORE UTILIZING THEM!**

<br>

#### VST 2.4

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

#### VST3

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
		delete m_videoproc; // --> not sure about memory leak otherwise (terminate or destructor do not work if put there), also makes validator fail
	}
    ...
}
```

<br>

**IMPORTANT**: make sure to free Reaper API objects as soon as possible otherwise Reaper will crash on VST removal.

*Remarks*: `validator.exe` crashes with `delete m_videoproc` for some reason. Comment the validator out in the post-build event command.

<br>

---

<br>

## **IREAPERVideoProcessor</span>**

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