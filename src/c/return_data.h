/*
 * Copyright (c) 2019
 * IoTech Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <stdint.h>
#include <bacdef.h>
#include <bacapp.h>
#include <rpm.h>

#ifndef DEVICE_BACNET_C_RETURN_DATA_H
#define DEVICE_BACNET_C_RETURN_DATA_H

typedef struct return_data_t
{
  /* The value to be returned to EdgeX */
  BACNET_APPLICATION_DATA_VALUE *value;
  /* The Request Invoke ID of the message */
  uint8_t requestInvokeID;
  /* The Address of the Target Device */
  BACNET_ADDRESS targetAddress;
  /* Error Bool */
  bool errorDetected;
  /* Condition variable to test if a response have been received */
  pthread_cond_t condition;
  /* Mutex used by the condition variable */
  pthread_mutex_t mutex;
  struct return_data_t *next;
  struct return_data_t *prev;
} return_data_t;

/* Linked list of return_data structures */
typedef struct return_data_ll
{
  return_data_t *first;
  pthread_mutex_t mutex;
} return_data_ll;

return_data_ll *return_data_alloc (void);

void return_data_free (return_data_ll *list);

return_data_t *
return_data_get (return_data_ll *list, uint8_t invoke_id);

return_data_t *return_data_set (return_data_ll *list,
                                uint8_t invoke_id);

bool return_data_remove_by_ptr (return_data_ll *list, return_data_t *data);

#endif //DEVICE_BACNET_C_RETURN_DATA_H
