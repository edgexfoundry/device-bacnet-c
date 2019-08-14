/*
 * Copyright (c) 2019
 * IoTech Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <stdint.h>

/* Structure for device_condition_map */
typedef struct device_condition_map_t
{
  /* Device ID */
  uint32_t device_id;
  /* Condition variable to test if a response have been received */
  pthread_cond_t condition;
  /* Mutex used by the condition variable */
  pthread_mutex_t mutex;
  /* BACnet address of the device */
  BACNET_ADDRESS *address;
  /* Previous element in the list */
  struct device_condition_map_t *prev;
  /* Next element in the list */
  struct device_condition_map_t *next;
} device_condition_map_t;

/* Linked list of device_condition_map */
typedef struct device_condition_map_ll
{
  device_condition_map_t *first;
  pthread_mutex_t mutex;
} device_condition_map_ll;

device_condition_map_ll *device_condition_map_alloc (void);

void device_condition_map_free (device_condition_map_ll *list);

device_condition_map_t *
device_condition_map_get (device_condition_map_ll *list, uint32_t device_id);

void device_condition_map_set (device_condition_map_ll *list,
                               uint32_t device_id, BACNET_ADDRESS *address);

bool
device_condition_map_remove (device_condition_map_ll *list, uint32_t device_id);