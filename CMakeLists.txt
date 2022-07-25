cmake_minimum_required(VERSION 3.14.0)

set(vst3sdk_SOURCE_DIR D:/Library/Coding/GitHub/_Reaper/vst-sdk-bundle/vst3-sdk/vst3sdk)
if(NOT vst3sdk_SOURCE_DIR)
    message(FATAL_ERROR "Path to VST3 SDK is empty!")
endif()

project(ReaShader
    # This is your plug-in version number. Change it here only.
    # Version number symbols usable in C++ can be found in
    # source/version.h and ${PROJECT_BINARY_DIR}/projectversion.h.
    VERSION 1.0.0.0 
    DESCRIPTION "ReaShader VST 3 Plug-in"
)

set(SMTG_VSTGUI_ROOT "${vst3sdk_SOURCE_DIR}")

add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)
smtg_enable_vst3_sdk()

smtg_add_vst3plugin(ReaShader
    source/version.h
    source/myplugincids.h
    source/mypluginprocessor.h
    source/mypluginprocessor.cpp
    source/myplugincontroller.h
    source/myplugincontroller.cpp
    source/mypluginentry.cpp
)

#- VSTGUI Wanted ----
if(SMTG_ADD_VSTGUI)
    target_sources(ReaShader
        PRIVATE
            resource/myplugineditor.uidesc
    )
    target_link_libraries(ReaShader
        PRIVATE
            vstgui_support
    )
    smtg_target_add_plugin_resources(ReaShader
        RESOURCES
            "resource/myplugineditor.uidesc"
    )
endif(SMTG_ADD_VSTGUI)
# -------------------

smtg_target_add_plugin_snapshots (ReaShader
    RESOURCES
        resource/E0F1D00DC00B511782AD083BE3690071_snapshot.png
        resource/E0F1D00DC00B511782AD083BE3690071_snapshot_2.0x.png
)

target_link_libraries(ReaShader
    PRIVATE
        sdk
)

smtg_target_configure_version_file(ReaShader)

if(SMTG_MAC)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 10.12)
    smtg_target_set_bundle(ReaShader
        BUNDLE_IDENTIFIER gvng.reashader
        COMPANY_NAME "Emanuele Messina"
    )
    smtg_target_set_debug_executable(ReaShader
        "/Applications/VST3PluginTestHost.app"
        "--pluginfolder;$(BUILT_PRODUCTS_DIR)"
    )
elseif(SMTG_WIN)
    target_sources(ReaShader PRIVATE 
        resource/win32resource.rc
    )
    if(MSVC)
        set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ReaShader)

        smtg_target_set_debug_executable(ReaShader
            "$(ProgramW6432)/Steinberg/VST3PluginTestHost/VST3PluginTestHost.exe"
            "--pluginfolder \"$(OutDir)/\""
        )
    endif()
endif(SMTG_MAC)
