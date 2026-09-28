//------------------------------------------------------------------------
// Copyright(c) 2022 Emanuele Messina.
//------------------------------------------------------------------------

#include "myplugincids.h"
#include "myplugincontroller.h"
#include "mypluginprocessor.h"

#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"

#ifdef RELEASE
#define stringPluginName "ReaShader"
#else
#define stringPluginName "ReaShader (DEBUG)"
#endif

using namespace Steinberg::Vst;
using namespace ReaShader;

//------------------------------------------------------------------------
//  VST Plug-in Entry
//------------------------------------------------------------------------
// Windows: do not forget to include a .def file in your project to export
// GetPluginFactory function!
//------------------------------------------------------------------------

BEGIN_FACTORY_DEF("Emanuele Messina", "https://www.linkedin.com/in/emanuelemessina-em/",
				  "mailto:emanuelemessina.em@gmail.com")

//---First Plug-in included in this factory-------
// its kVstAudioEffectClass component
DEF_CLASS2(INLINE_UID_FROM_FUID(kReaShaderProcessorUID),
		   PClassInfo::kManyInstances, // cardinality
		   kVstAudioEffectClass,	   // the component category (do not changed this)
		   stringPluginName,		   // here the Plug-in name (to be changed)
		   Vst::kDistributable,	  // means that component and controller could be distributed on different computers
		   ReaShaderVST3Category, // Subcategory for this Plug-in (to be changed)
		   FULL_VERSION_STR,	  // Plug-in version (to be changed)
		   kVstVersionString,	  // the VST 3 SDK version (do not changed this, use always this define)
		   MyPluginProcessor::createInstance) // function pointer called when this component should be instantiated

// its kVstComponentControllerClass component
DEF_CLASS2(INLINE_UID_FROM_FUID(kReaShaderControllerUID),
		   PClassInfo::kManyInstances,		   // cardinality
		   kVstComponentControllerClass,	   // the Controller category (do not changed this)
		   stringPluginName "Controller",	   // controller name (could be the same than component name)
		   0,								   // not used here
		   "",								   // not used here
		   FULL_VERSION_STR,				   // Plug-in version (to be changed)
		   kVstVersionString,				   // the VST 3 SDK version (do not changed this, use always this define)
		   MyPluginController::createInstance) // function pointer called when this component should be instantiated

//----for others Plug-ins contained in this factory, put like for the first Plug-in different DEF_CLASS2---

END_FACTORY