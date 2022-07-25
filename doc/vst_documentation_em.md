# VST 3 Documentation
_by Emanuele Messina_

<br>

[vst_sdk_bundle](https://github.com/emanuelemessina/vst-sdk-bundle), which bundles several SDKs needed to build a VST, included the fixed reaper-sdk, is needed.

<br>

## Preliminary

<br>

### Solution starting properties

1. Output Directory: `$(MSBuildProjectDirectory)\VST3\$(Configuration)\$(PlatformTarget)`
2. Intermediate Directory: `$(OutDir)\intermediate`
3. Debug command: `$(ReaperDir)\reaper.exe` , Attach: `yes`
4. Additional include directories: `$(MSBuildProjectDirectory)` and vst3sdk path (ok if absolute)
5. Generate Program Database File: `$(MSBuildProjectDirectory)\WIN_PDB64\Debug\ReaShader.pdb`
6. Pre-Link Event:
```bat
setlocal
"C:\Program Files\CMake\bin\cmake.exe" -E make_directory $(OutDir)Contents\Resources
if %errorlevel% neq 0 goto :cmEnd
"C:\Program Files\CMake\bin\cmake.exe" -E copy_if_different D:/Library/Coding/GitHub/_Reaper/ReaShader/resource/myplugineditor.uidesc $(OutDir)Contents\Resources
if %errorlevel% neq 0 goto :cmEnd
"C:\Program Files\CMake\bin\cmake.exe" -E echo "[SMTG] Copied resource/myplugineditor.uidesc to $(OutDir)Contents\Resources"
if %errorlevel% neq 0 goto :cmEnd
:cmEnd
endlocal & call :cmErrorLevel %errorlevel% & goto :cmDone
:cmErrorLevel
exit /b %1
:cmDone
if %errorlevel% neq 0 goto :VCEnd
setlocal
"C:\Program Files\CMake\bin\cmake.exe" -E make_directory $(OutDir)\Contents\Resources\Snapshots
if %errorlevel% neq 0 goto :cmEnd
"C:\Program Files\CMake\bin\cmake.exe" -E copy_if_different D:/Library/Coding/GitHub/_Reaper/ReaShader/resource/E0F1D00DC00B511782AD083BE3690071_snapshot.png $(OutDir)\Contents\Resources\Snapshots
if %errorlevel% neq 0 goto :cmEnd
"C:\Program Files\CMake\bin\cmake.exe" -E echo "[SMTG] Copied resource/E0F1D00DC00B511782AD083BE3690071_snapshot.png to $(OutDir)\Contents\Resources\Snapshots"
if %errorlevel% neq 0 goto :cmEnd
:cmEnd
endlocal & call :cmErrorLevel %errorlevel% & goto :cmDone
:cmErrorLevel
exit /b %1
:cmDone
if %errorlevel% neq 0 goto :VCEnd
setlocal
"C:\Program Files\CMake\bin\cmake.exe" -E make_directory $(OutDir)\Contents\Resources\Snapshots
if %errorlevel% neq 0 goto :cmEnd
"C:\Program Files\CMake\bin\cmake.exe" -E copy_if_different D:/Library/Coding/GitHub/_Reaper/ReaShader/resource/E0F1D00DC00B511782AD083BE3690071_snapshot_2.0x.png $(OutDir)\Contents\Resources\Snapshots
if %errorlevel% neq 0 goto :cmEnd
"C:\Program Files\CMake\bin\cmake.exe" -E echo "[SMTG] Copied resource/E0F1D00DC00B511782AD083BE3690071_snapshot_2.0x.png to $(OutDir)\Contents\Resources\Snapshots"
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
cd D:\Library\Coding\GitHub\_Reaper\ReaShader\build\bin
if %errorlevel% neq 0 goto :cmEnd
D:
if %errorlevel% neq 0 goto :cmEnd
D:\Library\Coding\GitHub\_Reaper\ReaShader\build\bin\Debug\moduleinfotool.exe -create -version 1.0.0.0 -path $(OutDir)$(TargetFileName) -output $(OutDir)Contents\moduleinfo.json
if %errorlevel% neq 0 goto :cmEnd
:cmEnd
endlocal & call :cmErrorLevel %errorlevel% & goto :cmDone
:cmErrorLevel
exit /b %1
:cmDone
if %errorlevel% neq 0 goto :VCEnd
setlocal
cd D:\Library\Coding\GitHub\_Reaper\ReaShader\build\bin
if %errorlevel% neq 0 goto :cmEnd
D:
if %errorlevel% neq 0 goto :cmEnd
echo [SMTG] Validator started...
if %errorlevel% neq 0 goto :cmEnd
D:\Library\Coding\GitHub\_Reaper\ReaShader\build\bin\Debug\validator.exe $(OutDir)\ReaShader.vst3
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
:: # line commented out-># mklink /D C:\Users\emanuele\AppData\Local\Programs\Common\VST3\ReaShader.vst3 D:\Library\Coding\GitHub\_Reaper\ReaShader\build\VST3\Debug\ReaShader.vst3 
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

### TODOs

8. Organize subfolders in solution

<br>

## Project structure

<br>

### VST Info

- `version.h`: file and legal
- `mypluginentry.cpp`: plugin and developer

### Main files

- `myplugincontroller.cpp`: communication with the DAW, handling parameters and the UI.
- `mypluginprocessor.cpp`: processing and persistence.
- `win32resource.rc`: resources file.
- `myplugincids.h`: various ids (the parameter ids enum can be put here). 

### Parameters

Parameters enum can be declared in `myplugincids.h`:
```c++
// Params (with defaults)

	enum ReaShaderParamIds : Steinberg::Vst::ParamID {
		uAudioGain,
		uVideoParam,

		uNumParams
	};
```
Then a map can be declared in `mypluginprocessor.h` to allocate the internal variables and init with defaults:
```c++
std::map<Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue> rParams = {
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

## FUnknown

<br>

Convenient snippets and doc collected while wandering through the sdk files...

---
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

## Reaper aware VST

<br>

### API Connection

<br>

**ALWAYS CHECK IF THE POINTERS ARE NULL BEFORE UTILIZING THEM!**

<br>

#### VST 2.4

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

    ...
    
```

### As a video processor

#### IVideoFrame

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