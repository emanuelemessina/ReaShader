# ReaShader Development

<br>

## vkt

<br>

The vkt namespace contains the custom vk tools to simplify the vulkan process.

<br>

- All `vkt` classes push their destructors to the deletion queue provided upon construction so do not delete them manually.

- All `Vk` objects are initialized with `VK_NULL_HANDLE`

<br>

### headers flow

`vktools -> vktcommandqueue -> vktdevices -> vktsync , vktbuffers -> vktimages -> vktdescriptors -> vktrendering`

<br>

---

<br>