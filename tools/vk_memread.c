/* vk_memread.c (som-b27-perf, docs/re-xr-pipeline.md 115.4): is reading the hands' mapped vertex memory slow? Every
 * host-visible memory type of the first GPU: the skin's writes and hd_box's reads (2 eyes x 2 hands x 1360 vertices).
 * Build: gcc -O2 -I ../third_party/Vulkan-Headers-1.4.341/include -o vk_memread vk_memread.c -lvulkan */
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
typedef struct { float p[3], n[3], a; } V;
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
int main(void) {
    VkApplicationInfo ap = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo ic = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &ap};
    VkInstance in; if (vkCreateInstance(&ic, NULL, &in)) return 1;
    uint32_t n = 1; VkPhysicalDevice pd; vkEnumeratePhysicalDevices(in, &n, &pd);
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    float pr = 1; VkDeviceQueueCreateInfo q = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueCount = 1, .pQueuePriorities = &pr};
    VkDeviceCreateInfo dc = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &q};
    VkDevice d; if (vkCreateDevice(pd, &dc, NULL, &d)) return 2;
    size_t nv = 4 * 1360 + 200, bytes = nv * sizeof(V);
    for (uint32_t t = 0; t < mp.memoryTypeCount; t++) {
        VkMemoryPropertyFlags f = mp.memoryTypes[t].propertyFlags;
        if (!(f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) continue;
        VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = bytes, .memoryTypeIndex = t};
        VkDeviceMemory m; void *p; if (vkAllocateMemory(d, &ai, NULL, &m) || vkMapMemory(d, m, 0, VK_WHOLE_SIZE, 0, &p)) continue;
        V *v = p; double t0 = now();
        for (size_t i = 0; i < nv; i++) for (int k = 0; k < 7; k++) ((float *)&v[i])[k] = (float)(i + k); /* the skin's writes */
        double t1 = now(); float s = 0;
        for (int e = 0; e < 2; e++) for (size_t i = 0; i < 2 * 1360; i++) s += v[i].p[0] * 0.3f + v[i].p[1] * 0.5f + v[i].p[2]; /* hd_box */
        double t2 = now();
        printf("type %u flags 0x%x heap %u: write %.3f ms, hd_box-like read %.3f ms (%g)\n", t, f, mp.memoryTypes[t].heapIndex, (t1 - t0) * 1e3, (t2 - t1) * 1e3, s);
        vkUnmapMemory(d, m); vkFreeMemory(d, m, NULL);
    }
    vkDestroyDevice(d, NULL); vkDestroyInstance(in, NULL); return 0;
}
