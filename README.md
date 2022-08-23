# ReaShader

_a VST Video Processor for Reaper_

<br>

## Dependencies

<br>

Directory structure

```
+ reashader
+ vst-sdk-bundle
```

Clone the vst-sdk-bundle repo next to reashader repo.
<br>
The repo contains the fixed libraries/sdk to make the vst work.


<br>

---

<br>



## vkt

The vkt namespace contains the custom vk tools to simplify the vulkan process.
<br>
- All vkt classes push their destructors to the deletion queue provided upon construction so do not delete them manually.

- All `Vk` objects are initialized with `VK_NULL_HANDLE`

<br>

### headers flow

`vktools -> vktcommandqueue -> vktdevices -> vktsync , vktbuffers -> vktimages -> vktdescriptors -> vktrendering`


<br>

---

<br>

## Credits

<br>

### Author
    
    Emanuele Messina

<br>

#### Open source libraries

    WDL by Cockos
    Vulkan Memory Allocator by GPUOpen

#### Third Party

    VST by Steinberg Media Technologies GmbH
    Vulkan by Khronos Group

#### Thanks to
    
- Sascha Willems : [Github](https://github.com/SaschaWillems/Vulkan)
- Victor Blanco : [Vulkan Guide](https://vkguide.dev/)
