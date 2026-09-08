//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include <linux/dma-mapping.h>
#include "zion-enclave.h"
/* idr for enclave UID to struct enclave */
DEFINE_IDR(idr_enclave);
DEFINE_MUTEX(idr_enclave_lock);

#define ENCLAVE_IDR_MIN 0x1000
#define ENCLAVE_IDR_MAX 0xffff

/* Final release, including partially initialized EPM and UTM objects. */
static void enclave_free(struct enclave* enclave)
{
  struct epm* epm;
  struct utm* utm;
  if (enclave == NULL)
    return;

  epm = enclave->epm;
  utm = enclave->utm;

  if (epm)
  {
    epm_destroy(epm);
    kfree(epm);
  }
  if (utm)
  {
    utm_destroy(utm);
    kfree(utm);
  }
  kfree(enclave);
}

void enclave_get(struct enclave *enclave)
{
  refcount_inc(&enclave->refs);
}

void enclave_put(struct enclave *enclave)
{
  if (enclave && refcount_dec_and_test(&enclave->refs))
    enclave_free(enclave);
}

struct enclave* create_enclave(unsigned long min_pages)
{
  struct enclave* enclave;

  enclave = kzalloc(sizeof(struct enclave), GFP_KERNEL);
  if (!enclave){
    zion_err("failed to allocate enclave struct\n");
    return NULL;
  }

  enclave->eid = -1;
  refcount_set(&enclave->refs, 1);

  enclave->epm = kzalloc(sizeof(struct epm), GFP_KERNEL);
  enclave->is_init = true;
  if (!enclave->epm)
  {
    zion_err("failed to allocate epm\n");
    goto error_put;
  }

  if(epm_init(enclave->epm, min_pages)) {
    zion_err("failed to initialize epm\n");
    goto error_put;
  }
  return enclave;

 error_put:
  enclave_put(enclave);
  return NULL;
}

int enclave_idr_alloc(struct enclave* enclave)
{
  int ueid;

  mutex_lock(&idr_enclave_lock);
  ueid = idr_alloc(&idr_enclave, enclave, ENCLAVE_IDR_MIN, ENCLAVE_IDR_MAX, GFP_KERNEL);
  if (ueid >= ENCLAVE_IDR_MIN)
    enclave_get(enclave); /* IDR ownership reference. */
  mutex_unlock(&idr_enclave_lock);

  return ueid;
}

struct enclave* enclave_idr_remove(unsigned int ueid)
{
  struct enclave* enclave;
  mutex_lock(&idr_enclave_lock);
  enclave = idr_remove(&idr_enclave, ueid);
  mutex_unlock(&idr_enclave_lock);
  return enclave;
}

struct enclave* enclave_get_by_id(unsigned int ueid)
{
  struct enclave* enclave;
  mutex_lock(&idr_enclave_lock);
  enclave = idr_find(&idr_enclave, ueid);
  if (enclave)
    enclave_get(enclave);
  mutex_unlock(&idr_enclave_lock);
  return enclave;
}
