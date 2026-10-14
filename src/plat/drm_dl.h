// drm_dl.h: libdrm loaded at run time (dlopen), so the binary needs no libdrm to link: plat_kms.c calls the usual
// names, which are pointers filled by drm_load(). Include after xf86drm.h / xf86drmMode.h.
#pragma once
#include <dlfcn.h>

static __typeof__(drmDropMaster) *p_drmDropMaster;
static __typeof__(drmHandleEvent) *p_drmHandleEvent;
static __typeof__(drmIoctl) *p_drmIoctl;
static __typeof__(drmModeAddFB2) *p_drmModeAddFB2;
static __typeof__(drmModeAtomicAddProperty) *p_drmModeAtomicAddProperty;
static __typeof__(drmModeAtomicAlloc) *p_drmModeAtomicAlloc;
static __typeof__(drmModeAtomicCommit) *p_drmModeAtomicCommit;
static __typeof__(drmModeAtomicFree) *p_drmModeAtomicFree;
static __typeof__(drmModeCreatePropertyBlob) *p_drmModeCreatePropertyBlob;
static __typeof__(drmModeFreeConnector) *p_drmModeFreeConnector;
static __typeof__(drmModeFreeEncoder) *p_drmModeFreeEncoder;
static __typeof__(drmModeFreeObjectProperties) *p_drmModeFreeObjectProperties;
static __typeof__(drmModeFreePlane) *p_drmModeFreePlane;
static __typeof__(drmModeFreePlaneResources) *p_drmModeFreePlaneResources;
static __typeof__(drmModeFreeProperty) *p_drmModeFreeProperty;
static __typeof__(drmModeFreeResources) *p_drmModeFreeResources;
static __typeof__(drmModeGetConnector) *p_drmModeGetConnector;
static __typeof__(drmModeGetEncoder) *p_drmModeGetEncoder;
static __typeof__(drmModeGetPlane) *p_drmModeGetPlane;
static __typeof__(drmModeGetPlaneResources) *p_drmModeGetPlaneResources;
static __typeof__(drmModeGetProperty) *p_drmModeGetProperty;
static __typeof__(drmModeGetResources) *p_drmModeGetResources;
static __typeof__(drmModeObjectGetProperties) *p_drmModeObjectGetProperties;
static __typeof__(drmModeRmFB) *p_drmModeRmFB;
static __typeof__(drmSetClientCap) *p_drmSetClientCap;
static __typeof__(drmSetMaster) *p_drmSetMaster;

static int drm_load(void) {
    static int done; if (done) return done > 0 ? 0 : -1;
    void *h = dlopen("libdrm.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!h) { done = -1; return -1; }
#define L(n) if (!(*(void **)&p_##n = dlsym(h, #n))) { done = -1; return -1; }
    L(drmDropMaster)
    L(drmHandleEvent)
    L(drmIoctl)
    L(drmModeAddFB2)
    L(drmModeAtomicAddProperty)
    L(drmModeAtomicAlloc)
    L(drmModeAtomicCommit)
    L(drmModeAtomicFree)
    L(drmModeCreatePropertyBlob)
    L(drmModeFreeConnector)
    L(drmModeFreeEncoder)
    L(drmModeFreeObjectProperties)
    L(drmModeFreePlane)
    L(drmModeFreePlaneResources)
    L(drmModeFreeProperty)
    L(drmModeFreeResources)
    L(drmModeGetConnector)
    L(drmModeGetEncoder)
    L(drmModeGetPlane)
    L(drmModeGetPlaneResources)
    L(drmModeGetProperty)
    L(drmModeGetResources)
    L(drmModeObjectGetProperties)
    L(drmModeRmFB)
    L(drmSetClientCap)
    L(drmSetMaster)
#undef L
    done = 1; return 0;
}

#define drmDropMaster p_drmDropMaster
#define drmHandleEvent p_drmHandleEvent
#define drmIoctl p_drmIoctl
#define drmModeAddFB2 p_drmModeAddFB2
#define drmModeAtomicAddProperty p_drmModeAtomicAddProperty
#define drmModeAtomicAlloc p_drmModeAtomicAlloc
#define drmModeAtomicCommit p_drmModeAtomicCommit
#define drmModeAtomicFree p_drmModeAtomicFree
#define drmModeCreatePropertyBlob p_drmModeCreatePropertyBlob
#define drmModeFreeConnector p_drmModeFreeConnector
#define drmModeFreeEncoder p_drmModeFreeEncoder
#define drmModeFreeObjectProperties p_drmModeFreeObjectProperties
#define drmModeFreePlane p_drmModeFreePlane
#define drmModeFreePlaneResources p_drmModeFreePlaneResources
#define drmModeFreeProperty p_drmModeFreeProperty
#define drmModeFreeResources p_drmModeFreeResources
#define drmModeGetConnector p_drmModeGetConnector
#define drmModeGetEncoder p_drmModeGetEncoder
#define drmModeGetPlane p_drmModeGetPlane
#define drmModeGetPlaneResources p_drmModeGetPlaneResources
#define drmModeGetProperty p_drmModeGetProperty
#define drmModeGetResources p_drmModeGetResources
#define drmModeObjectGetProperties p_drmModeObjectGetProperties
#define drmModeRmFB p_drmModeRmFB
#define drmSetClientCap p_drmSetClientCap
#define drmSetMaster p_drmSetMaster
