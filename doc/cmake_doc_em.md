# CMake Documentation
_by Emanuele Messina_

<br>

## Variables

<br>

Declaration
```cmake
set(VAR_NAME value)
```

Access
```cmake
${VAR_NAME}
```

Environment variable access
```cmake
$ENV{ENV_VAR}
```

GUI Options
```cmake
set(VAR_NAME "default value" CACHE <PATH|FILEPATH...> "tooltip")
```

Variable in string
```cmake
"string: ${VAR_NAME}"
```

<br>

## Checks

<br>

File/directory existence
```cmake
if(NOT EXISTS "path/to")
    # ...
endif()
```

Variable definition
```cmake
if(DEFINED ${VAR_NAME}) # env var
    message(FATAL_ERROR "Vulkan SDK is not installed!")
endif()
```

<br>

## Message

<br>

```cmake
message(<FATAL_ERROR|WARNING...> "message")
```

<br>

## C++ Version

<br>

```cmake
set_property(TARGET ${TARGET_NAME} PROPERTY CXX_STANDARD ${CPP_ISO_NUMBER})
```

<br>

## Include directories

<br>

```cmake
include_directories (
   dir1 dir2 ...
)
```
<br>

## Source files

<br>

Source files will be added to project and shown in the IDE (like VS).
\
Only `.cpp` and `.h` files will be marked for compilation.
\
\
`file` will also define a variable (eg `SOURCE_FILES`) that contains the files specified, since functions like `source_group` will require the same files to be specified.
\
\
`GLOB_RECURSE` option is used to resolve the `*` wildcard recursively.

```cmake
file(<GLOB_RECURSE|GLOB> SOURCE_FILES
    "include/*.h"
    "*.cpp"
    "other/file.ext"
)
```

<br>

## Link libraries

<br>

Input directories
```cmake
target_link_directories(${TARGET_NAME} PRIVATE
    path1 path2 ...
)
```
Input files (libraries)
```cmake
target_link_libraries(${PROJECT_NAME} PRIVATE 
    file1 # same as : general file1
    debug file2 # included in Debug configuration
    optimized file3 # included in Release configuration
)
```
If `target_link_*` was called with `PRIVATE` for a certain target, all following calls must be made also with `PRIVATE`.

<br>

## Source groups

<br>

In VS source groups are filters. 
\
`TREE` is used to create a filter structure resembling the physical folder structure.
\
Next is the root folder containing all the files specified after `FILES`, for that it is necessary that all files reside not so far away from each other under a common source folder.
\
`${CMAKE_CURRENT_SOURCE_DIR}` can be used to refer to the current script location.
\
The files specified must be the same as those referenced by `add_<library|executable>`, which in turn were specified by `file`.

```cmake
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SOURCE_FILES})
```

<br>

## Preprocessor definitions

<br>

```cmake
add_compile_definitions( DEF1=VALUE ... )
```

<br>

## Windows

<br>

Add resource files to target
```cmake
target_sources(${PROJECT_NAME} PRIVATE 
    resource/win32resource.rc
)
```

<br>

### Visual Studio

<br>

Set the startup project
```cmake
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
```