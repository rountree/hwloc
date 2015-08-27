/*
 * Copyright © 2011 Université Bordeaux
 * Copyright © 2012-2015 Inria.  All rights reserved.
 * See COPYING in top-level directory.
 */

#include <private/autogen/config.h>
#include <hwloc.h>
#include <hwloc/plugins.h>
#include <hwloc/cudart.h>

/* private headers allowed for convenience because this plugin is built within hwloc */
#include <private/misc.h>
#include <private/debug.h>

#include <cuda_runtime_api.h>

static unsigned hwloc_cuda_cores_per_MP(int major, int minor)
{
  /* based on CUDA C Programming Guide, Annex G */
  switch (major) {
    case 1:
      switch (minor) {
        case 0:
        case 1:
        case 2:
        case 3: return 8;
      }
      break;
    case 2:
      switch (minor) {
        case 0: return 32;
        case 1: return 48;
      }
      break;
    case 3:
      return 192;
    case 5:
      return 128;
  }
  hwloc_debug("unknown compute capability %u.%u, disabling core display.\n", major, minor);
  return 0;
}

static int
hwloc_cuda_discover(struct hwloc_backend *backend)
{
  struct hwloc_topology *topology = backend->topology;
  cudaError_t cures;
  int nb, i;

  if (!(hwloc_topology_get_flags(topology) & (HWLOC_TOPOLOGY_FLAG_IO_DEVICES|HWLOC_TOPOLOGY_FLAG_WHOLE_IO)))
    return 0;

  if (!hwloc_topology_is_thissystem(topology)) {
    hwloc_debug("%s", "\nno CUDA detection (not thissystem)\n");
    return 0;
  }

  cures = cudaGetDeviceCount(&nb);
  if (cures)
    return -1;

  for (i = 0; i < nb; i++) {
    int domain, bus, dev;
    char cuda_name[32];
    char number[32];
    struct cudaDeviceProp prop;
    hwloc_obj_t cuda_device, parent;
    unsigned cores;

    cuda_device = hwloc_alloc_setup_object(HWLOC_OBJ_OS_DEVICE, -1);
    snprintf(cuda_name, sizeof(cuda_name), "cuda%d", i);
    cuda_device->name = strdup(cuda_name);
    cuda_device->depth = (unsigned) HWLOC_TYPE_DEPTH_UNKNOWN;
    cuda_device->attr->osdev.type = HWLOC_OBJ_OSDEV_COPROC;

    hwloc_obj_add_info(cuda_device, "CoProcType", "CUDA");
    hwloc_obj_add_info(cuda_device, "Backend", "CUDA");
    hwloc_obj_add_info(cuda_device, "GPUVendor", "NVIDIA Corporation");

    cures = cudaGetDeviceProperties(&prop, i);
    if (!cures)
      hwloc_obj_add_info(cuda_device, "GPUModel", prop.name);

    snprintf(number, sizeof(number), "%llu", ((unsigned long long) prop.totalGlobalMem) >> 10);
    hwloc_obj_add_info(cuda_device, "CUDAGlobalMemorySize", number);

    snprintf(number, sizeof(number), "%llu", ((unsigned long long) prop.l2CacheSize) >> 10);
    hwloc_obj_add_info(cuda_device, "CUDAL2CacheSize", number);

    snprintf(number, sizeof(number), "%d", prop.multiProcessorCount);
    hwloc_obj_add_info(cuda_device, "CUDAMultiProcessors", number);

    cores = hwloc_cuda_cores_per_MP(prop.major, prop.minor);
    if (cores) {
      snprintf(number, sizeof(number), "%u", cores);
      hwloc_obj_add_info(cuda_device, "CUDACoresPerMP", number);
    }

    snprintf(number, sizeof(number), "%llu", ((unsigned long long) prop.sharedMemPerBlock) >> 10);
    hwloc_obj_add_info(cuda_device, "CUDASharedMemorySizePerMP", number);

    parent = NULL;
    if (hwloc_cudart_get_device_pci_ids(NULL /* topology unused */, i, &domain, &bus, &dev) == 0) {
      parent = hwloc_pci_belowroot_find_by_busid(topology, domain, bus, dev, 0);
      if (!parent)
	parent = hwloc_pci_find_busid_parent(topology, domain, bus, dev, 0);
    }
    if (!parent)
      parent = hwloc_get_root_obj(topology);

    hwloc_insert_object_by_parent(topology, parent, cuda_device);
  }

  return 0;
}

static struct hwloc_backend *
hwloc_cuda_component_instantiate(struct hwloc_disc_component *component,
                                 const void *_data1 __hwloc_attribute_unused,
                                 const void *_data2 __hwloc_attribute_unused,
                                 const void *_data3 __hwloc_attribute_unused)
{
  struct hwloc_backend *backend;

  /* thissystem may not be fully initialized yet, we'll check flags in discover() */

  backend = hwloc_backend_alloc(component);
  if (!backend)
    return NULL;
  /* the first callback will initialize those */
  backend->discover = hwloc_cuda_discover;
  return backend;
}

static struct hwloc_disc_component hwloc_cuda_disc_component = {
  HWLOC_DISC_COMPONENT_TYPE_MISC,
  "cuda",
  HWLOC_DISC_COMPONENT_TYPE_GLOBAL,
  hwloc_cuda_component_instantiate,
  10, /* after pci */
  NULL
};

static int
hwloc_cuda_component_init(unsigned long flags)
{
  if (flags)
    return -1;
  if (hwloc_plugin_check_namespace("cuda", "hwloc_backend_alloc") < 0)
    return -1;
  return 0;
}

#ifdef HWLOC_INSIDE_PLUGIN
HWLOC_DECLSPEC extern const struct hwloc_component hwloc_cuda_component;
#endif

const struct hwloc_component hwloc_cuda_component = {
  HWLOC_COMPONENT_ABI,
  hwloc_cuda_component_init, NULL,
  HWLOC_COMPONENT_TYPE_DISC,
  0,
  &hwloc_cuda_disc_component
};
