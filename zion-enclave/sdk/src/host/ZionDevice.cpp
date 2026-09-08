//******************************************************************************
// Copyright (c) 2020, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include "ZionDevice.hpp"

#include <sys/mman.h>

namespace Zion {

ZionDevice::ZionDevice() { eid = -1; }

Error
ZionDevice::create(uint64_t minPages) {
  struct zion_ioctl_create_enclave encl;
  encl.min_pages = minPages;

  if (ioctl(fd, ZION_IOC_CREATE_ENCLAVE, &encl)) {
    perror("ioctl error");
    eid = -1;
    return Error::IoctlErrorCreate;
  }

  eid      = encl.eid;
  physAddr = encl.epm_paddr;

  return Error::Success;
}

uintptr_t
ZionDevice::initUTM(size_t size) {
  struct zion_ioctl_create_enclave encl;
  encl.eid      = eid;
  encl.utm_size = size;
  if (ioctl(fd, ZION_IOC_UTM_INIT, &encl)) {
    return 0;
  }

  return encl.utm_paddr;
}

Error
ZionDevice::finalize(
    uintptr_t runtimePhysAddr, uintptr_t eappPhysAddr, uintptr_t freePhysAddr,
    uintptr_t freeRequested) {
  struct zion_ioctl_create_enclave encl;
  encl.eid            = eid;
  encl.runtime_paddr  = runtimePhysAddr;
  encl.user_paddr     = eappPhysAddr;
  encl.free_paddr     = freePhysAddr;
  encl.free_requested = freeRequested;

  if (ioctl(fd, ZION_IOC_FINALIZE_ENCLAVE, &encl)) {
    perror("ioctl error");
    return Error::IoctlErrorFinalize;
  }
  return Error::Success;
}

Error
ZionDevice::destroy() {
  struct zion_ioctl_create_enclave encl;
  encl.eid = eid;

  /* if the enclave has never created */
  if (eid < 0) {
    return Error::Success;
  }

  if (ioctl(fd, ZION_IOC_DESTROY_ENCLAVE, &encl)) {
    perror("ioctl error");
    return Error::IoctlErrorDestroy;
  }

  /* Make destroy idempotent for Enclave's explicit destroy plus destructor.
   * The userspace ID is no longer valid after the driver releases it. */
  eid = -1;
  return Error::Success;
}

Error
ZionDevice::__run(bool resume, uintptr_t* ret) {
  struct zion_ioctl_run_enclave encl;
  encl.eid = eid;

  Error error;
  uint64_t request;

  if (resume) {
    error   = Error::IoctlErrorResume;
    request = ZION_IOC_RESUME_ENCLAVE;
  } else {
    error   = Error::IoctlErrorRun;
    request = ZION_IOC_RUN_ENCLAVE;
  }

  if (ioctl(fd, request, &encl)) {
    return error;
  }

  switch (encl.error) {
    case SBI_ERR_SM_ENCLAVE_EDGE_CALL_HOST:
      return Error::EdgeCallHost;
    case SBI_ERR_SM_ENCLAVE_INTERRUPTED:
      return Error::EnclaveInterrupted;
    case SBI_ERR_SM_ENCLAVE_SUCCESS:
      if (ret) {
        *ret = encl.value;
      }
      return Error::Success;
    default:
      ERROR(
          "Unknown SBI error (%d) returned by %s_enclave\n", encl.error,
          resume ? "resume" : "run");
      return error;
  }
}

Error
ZionDevice::run(uintptr_t* ret) {
  return __run(false, ret);
}

Error
ZionDevice::resume(uintptr_t* ret) {
  return __run(true, ret);
}

void*
ZionDevice::map(uintptr_t addr, size_t size) {
  assert(fd >= 0);
  void* ret;
  ret = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, addr);
  assert(ret != MAP_FAILED);
  return ret;
}

bool
ZionDevice::initDevice(Params params) { // TODO: why does this need params
  /* open device driver */
  fd = open(ZION_DEV_PATH, O_RDWR);
  if (fd < 0) {
    PERROR("cannot open device file");
    return false;
  }
  return true;
}

Error
MockZionDevice::create(uint64_t minPages) {
  eid = -1;
  return Error::Success;
}

uintptr_t
MockZionDevice::initUTM(size_t size) {
  return 0;
}

Error
MockZionDevice::finalize(
    uintptr_t runtimePhysAddr, uintptr_t eappPhysAddr, uintptr_t freePhysAddr,
    uintptr_t freeRequested) {
  return Error::Success;
}

Error
MockZionDevice::destroy() {
  return Error::Success;
}

Error
MockZionDevice::run(uintptr_t* ret) {
  return Error::Success;
}

Error
MockZionDevice::resume(uintptr_t* ret) {
  return Error::Success;
}

bool
MockZionDevice::initDevice(Params params) {
  return true;
}

void*
MockZionDevice::map(uintptr_t addr, size_t size) {
  sharedBuffer = malloc(size);
  return sharedBuffer;
}

MockZionDevice::~MockZionDevice() {
  if (sharedBuffer) free(sharedBuffer);
}

}  // namespace Zion
