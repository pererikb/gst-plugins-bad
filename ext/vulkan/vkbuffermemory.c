/*
 * GStreamer
 * Copyright (C) 2016 Matthew Waters <matthew@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "vkbuffermemory.h"

/**
 * SECTION:vkbuffermemory
 * @short_description: memory subclass for Vulkan buffer memory
 * @see_also: #GstMemory, #GstAllocator
 *
 * GstVulkanBufferMemory is a #GstMemory subclass providing support for the
 * mapping of Vulkan device memory.
 */

#define GST_CAT_DEFUALT GST_CAT_VULKAN_BUFFER_MEMORY
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFUALT);

static GstAllocator *_vulkan_buffer_memory_allocator;

#define GST_VK_BUFFER_CREATE_INFO_INIT GST_VK_STRUCT_8
#define GST_VK_BUFFER_CREATE_INFO(info, pNext, flags, size, usage, sharingMode, queueFamilyIndexCount, pQueueFamilyIndices ) \
  G_STMT_START { \
    VkBufferCreateInfo tmp = GST_VK_BUFFER_CREATE_INFO_INIT (VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, pNext, flags, size, usage, sharingMode, queueFamilyIndexCount, pQueueFamilyIndices); \
    (info) = tmp; \
  } G_STMT_END

static gboolean
_create_info_from_args (VkBufferCreateInfo * info, gsize size,
    VkBufferUsageFlags usage)
{
  /* FIXME: validate these */
  GST_VK_BUFFER_CREATE_INFO (*info, NULL, 0, size, usage,
      VK_SHARING_MODE_EXCLUSIVE, 0, NULL);

  return TRUE;
}

static void
_vk_buffer_mem_init (GstVulkanBufferMemory * mem, GstAllocator * allocator,
    GstMemory * parent, GstVulkanDevice * device, VkBufferUsageFlags usage,
    GstAllocationParams * params, gsize size, gpointer user_data,
    GDestroyNotify notify)
{
  gsize align = gst_memory_alignment, offset = 0, maxsize = size;
  GstMemoryFlags flags = 0;

  if (params) {
    flags = params->flags;
    align |= params->align;
    offset = params->prefix;
    maxsize += params->prefix + params->padding + align;
  }

  gst_memory_init (GST_MEMORY_CAST (mem), flags, allocator, parent, maxsize,
      align, offset, size);

  mem->device = gst_object_ref (device);
  mem->wrapped = FALSE;
  mem->notify = notify;
  mem->user_data = user_data;

  g_mutex_init (&mem->lock);

  GST_CAT_DEBUG (GST_CAT_VULKAN_BUFFER_MEMORY,
      "new Vulkan Buffer memory:%p size:%" G_GSIZE_FORMAT, mem, maxsize);
}

static GstVulkanBufferMemory *
_vk_buffer_mem_new_alloc (GstAllocator * allocator, GstMemory * parent,
    GstVulkanDevice * device, VkFormat format, gsize size,
    VkBufferUsageFlags usage, VkMemoryPropertyFlags mem_prop_flags,
    gpointer user_data, GDestroyNotify notify)
{
  GstVulkanBufferMemory *mem = NULL;
  GstAllocationParams params = { 0, };
  VkBufferCreateInfo buffer_info;
  GError *error = NULL;
  VkBuffer buffer;
  VkResult err;

  if (!_create_info_from_args (&buffer_info, size, usage)) {
    GST_CAT_ERROR (GST_CAT_VULKAN_BUFFER_MEMORY, "Incorrect buffer parameters");
    goto error;
  }

  err = vkCreateBuffer (device->device, &buffer_info, NULL, &buffer);
  if (gst_vulkan_error_to_g_error (err, &error, "vkCreateBuffer") < 0)
    goto vk_error;

  mem = g_new0 (GstVulkanBufferMemory, 1);
  vkGetBufferMemoryRequirements (device->device, buffer, &mem->requirements);

  params.align = mem->requirements.alignment;
  _vk_buffer_mem_init (mem, allocator, parent, device, usage, &params,
      mem->requirements.size, user_data, notify);
  mem->buffer = buffer;

  return mem;

vk_error:
  {
    GST_CAT_ERROR (GST_CAT_VULKAN_BUFFER_MEMORY,
        "Failed to allocate buffer memory %s", error->message);
    g_clear_error (&error);
    goto error;
  }

error:
  {
    if (mem)
      gst_memory_unref ((GstMemory *) mem);
    return NULL;
  }
}

static GstVulkanBufferMemory *
_vk_buffer_mem_new_wrapped (GstAllocator * allocator, GstMemory * parent,
    GstVulkanDevice * device, VkBuffer buffer, VkFormat format,
    VkBufferUsageFlags usage, gpointer user_data, GDestroyNotify notify)
{
  GstVulkanBufferMemory *mem = g_new0 (GstVulkanBufferMemory, 1);
  GstAllocationParams params = { 0, };

  mem->buffer = buffer;

  vkGetBufferMemoryRequirements (device->device, mem->buffer,
      &mem->requirements);

  /* no device memory so no mapping */
  params.flags = GST_MEMORY_FLAG_NOT_MAPPABLE | GST_MEMORY_FLAG_READONLY;
  _vk_buffer_mem_init (mem, allocator, parent, device, usage, &params,
      mem->requirements.size, user_data, notify);
  mem->wrapped = TRUE;

  return mem;
}

static gpointer
_vk_buffer_mem_map_full (GstVulkanBufferMemory * mem, GstMapInfo * info,
    gsize size)
{
  GstMapInfo *vk_map_info;

  /* FIXME: possible barrier needed */
  g_mutex_lock (&mem->lock);

  if (!mem->vk_mem) {
    g_mutex_unlock (&mem->lock);
    return NULL;
  }

  vk_map_info = g_new0 (GstMapInfo, 1);
  info->user_data[0] = vk_map_info;
  if (!gst_memory_map ((GstMemory *) mem->vk_mem, vk_map_info, info->flags)) {
    g_free (vk_map_info);
    g_mutex_unlock (&mem->lock);
    return NULL;
  }
  g_mutex_unlock (&mem->lock);

  return vk_map_info->data;
}

static void
_vk_buffer_mem_unmap_full (GstVulkanBufferMemory * mem, GstMapInfo * info)
{
  g_mutex_lock (&mem->lock);
  gst_memory_unmap ((GstMemory *) mem->vk_mem, info->user_data[0]);
  g_mutex_unlock (&mem->lock);

  g_free (info->user_data[0]);
}

static GstMemory *
_vk_buffer_mem_copy (GstVulkanBufferMemory * src, gssize offset, gssize size)
{
  return NULL;
}

static GstMemory *
_vk_buffer_mem_share (GstVulkanBufferMemory * mem, gssize offset, gssize size)
{
  return NULL;
}

static gboolean
_vk_buffer_mem_is_span (GstVulkanBufferMemory * mem1,
    GstVulkanBufferMemory * mem2, gsize * offset)
{
  return FALSE;
}

static GstMemory *
_vk_buffer_mem_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  g_critical ("Subclass should override GstAllocatorClass::alloc() function");

  return NULL;
}

static void
_vk_buffer_mem_free (GstAllocator * allocator, GstMemory * memory)
{
  GstVulkanBufferMemory *mem = (GstVulkanBufferMemory *) memory;

  GST_CAT_TRACE (GST_CAT_VULKAN_BUFFER_MEMORY, "freeing buffer memory:%p "
      "id:%" G_GUINT64_FORMAT, mem, (guint64) mem->buffer);

  if (mem->buffer && !mem->wrapped)
    vkDestroyBuffer (mem->device->device, mem->buffer, NULL);

  if (mem->vk_mem)
    gst_memory_unref ((GstMemory *) mem->vk_mem);

  if (mem->notify)
    mem->notify (mem->user_data);

  gst_object_unref (mem->device);
}

/**
 * gst_vulkan_buffer_memory_alloc:
 * @device:a #GstVulkanDevice
 * @memory_type_index: the Vulkan memory type index
 * @params: a #GstAllocationParams
 * @size: the size to allocate
 *
 * Allocated a new #GstVulkanBufferMemory.
 *
 * Returns: a #GstMemory object backed by a vulkan device memory
 */
GstMemory *
gst_vulkan_buffer_memory_alloc (GstVulkanDevice * device, VkFormat format,
    gsize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags mem_prop_flags)
{
  GstVulkanBufferMemory *mem;

  mem = _vk_buffer_mem_new_alloc (_vulkan_buffer_memory_allocator, NULL, device,
      format, size, usage, mem_prop_flags, NULL, NULL);

  return (GstMemory *) mem;
}

GstMemory *
gst_vulkan_buffer_memory_alloc_bind (GstVulkanDevice * device, VkFormat format,
    gsize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags mem_prop_flags)
{
  GstAllocationParams params = { 0, };
  GstVulkanBufferMemory *mem;
  GstVulkanMemory *dev_mem;
  guint32 type_idx;

  mem =
      (GstVulkanBufferMemory *) gst_vulkan_buffer_memory_alloc (device, format,
      size, usage, mem_prop_flags);
  if (!mem)
    return NULL;

  if (!gst_vulkan_memory_find_memory_type_index_with_type_properties (device,
          mem->requirements.memoryTypeBits, mem_prop_flags, &type_idx)) {
    gst_memory_unref (GST_MEMORY_CAST (mem));
    return NULL;
  }

  /* XXX: assumes alignment is a power of 2 */
  params.align = mem->requirements.alignment - 1;
  dev_mem = (GstVulkanMemory *) gst_vulkan_memory_alloc (device, type_idx,
      &params, mem->requirements.size, mem_prop_flags);
  if (!dev_mem) {
    gst_memory_unref (GST_MEMORY_CAST (mem));
    return NULL;
  }

  if (!gst_vulkan_buffer_memory_bind (mem, dev_mem)) {
    gst_memory_unref (GST_MEMORY_CAST (dev_mem));
    gst_memory_unref (GST_MEMORY_CAST (mem));
    return NULL;
  }
  gst_memory_unref (GST_MEMORY_CAST (dev_mem));

  return (GstMemory *) mem;
}

GstMemory *
gst_vulkan_buffer_memory_wrapped (GstVulkanDevice * device, VkBuffer buffer,
    VkFormat format, VkBufferUsageFlags usage, gpointer user_data,
    GDestroyNotify notify)
{
  GstVulkanBufferMemory *mem;

  mem =
      _vk_buffer_mem_new_wrapped (_vulkan_buffer_memory_allocator, NULL, device,
      buffer, format, usage, user_data, notify);

  return (GstMemory *) mem;
}

gboolean
gst_vulkan_buffer_memory_bind (GstVulkanBufferMemory * buf_mem,
    GstVulkanMemory * memory)
{
  gsize maxsize;

  g_return_val_if_fail (gst_is_vulkan_buffer_memory (GST_MEMORY_CAST (buf_mem)),
      FALSE);
  g_return_val_if_fail (gst_is_vulkan_memory (GST_MEMORY_CAST (memory)), FALSE);

  /* will we overrun the allocated data */
  gst_memory_get_sizes (GST_MEMORY_CAST (memory), NULL, &maxsize);
  g_return_val_if_fail (memory->vk_offset + buf_mem->requirements.size <=
      maxsize, FALSE);

  g_mutex_lock (&buf_mem->lock);

  /* "Once a buffer or image is bound to a region of a memory object, it must
   * not be rebound or unbound." */
  if (buf_mem->vk_mem == memory) {
    g_mutex_unlock (&buf_mem->lock);
    return TRUE;
  }

  if (buf_mem->vk_mem) {
    g_mutex_unlock (&buf_mem->lock);
    return FALSE;
  }

  vkBindBufferMemory (buf_mem->device->device, buf_mem->buffer, memory->mem_ptr,
      memory->vk_offset);

  buf_mem->vk_mem =
      (GstVulkanMemory *) gst_memory_ref (GST_MEMORY_CAST (memory));
  g_mutex_unlock (&buf_mem->lock);

  return TRUE;
}

G_DEFINE_TYPE (GstVulkanBufferMemoryAllocator,
    gst_vulkan_buffer_memory_allocator, GST_TYPE_ALLOCATOR);

static void
    gst_vulkan_buffer_memory_allocator_class_init
    (GstVulkanBufferMemoryAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = _vk_buffer_mem_alloc;
  allocator_class->free = _vk_buffer_mem_free;
}

static void
gst_vulkan_buffer_memory_allocator_init (GstVulkanBufferMemoryAllocator *
    allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_VULKAN_BUFFER_MEMORY_ALLOCATOR_NAME;
  alloc->mem_map_full = (GstMemoryMapFullFunction) _vk_buffer_mem_map_full;
  alloc->mem_unmap_full =
      (GstMemoryUnmapFullFunction) _vk_buffer_mem_unmap_full;
  alloc->mem_copy = (GstMemoryCopyFunction) _vk_buffer_mem_copy;
  alloc->mem_share = (GstMemoryShareFunction) _vk_buffer_mem_share;
  alloc->mem_is_span = (GstMemoryIsSpanFunction) _vk_buffer_mem_is_span;
}

/**
 * gst_vulkan_buffer_memory_init_once:
 *
 * Initializes the Vulkan memory allocator. It is safe to call this function
 * multiple times.  This must be called before any other #GstVulkanBufferMemory operation.
 */
void
gst_vulkan_buffer_memory_init_once (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (GST_CAT_VULKAN_BUFFER_MEMORY, "vulkanbuffermemory",
        0, "Vulkan Buffer Memory");

    _vulkan_buffer_memory_allocator =
        g_object_new (gst_vulkan_buffer_memory_allocator_get_type (), NULL);

    gst_allocator_register (GST_VULKAN_BUFFER_MEMORY_ALLOCATOR_NAME,
        gst_object_ref (_vulkan_buffer_memory_allocator));
    g_once_init_leave (&_init, 1);
  }
}

/**
 * gst_is_vulkan_buffer_memory:
 * @mem:a #GstMemory
 * 
 * Returns: whether the memory at @mem is a #GstVulkanBufferMemory
 */
gboolean
gst_is_vulkan_buffer_memory (GstMemory * mem)
{
  return mem != NULL && mem->allocator != NULL &&
      g_type_is_a (G_OBJECT_TYPE (mem->allocator),
      GST_TYPE_VULKAN_BUFFER_MEMORY_ALLOCATOR);
}