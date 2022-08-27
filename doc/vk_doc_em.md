# Vulkan Documentation
_by Emanuele Messina_

<br>

## **VMA**

_Vulkan Memory Allocator_

<br>

### <span style="color:lightgreen">Buffers Destruction</span>

<br>
Always free and flush (in this order!) the allocation after destroying the buffer.

```c++
vmaDestroyBuffer(vmaAllocator, buffer.buffer, buffer.allocation);
vmaFreeMemory(vmaAllocator, buffer.allocation);
vmaFlushAllocation(vmaAllocator, buffer.allocation, 0, VK_WHOLE_SIZE);
```