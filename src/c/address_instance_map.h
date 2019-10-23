/*
 * Copyright (c) 2019
 * IoTech Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <stdint.h>

#ifndef DEVICE_BACNET_C_ADDRESS_INSTANCE_MAP_H
#define DEVICE_BACNET_C_ADDRESS_INSTANCE_MAP_H

/* Structure containing a device address and its corresponding device instance */
typedef struct address_instance_map_t
{
  char *address;
  uint32_t instance;
  struct address_instance_map_t *next;
  struct address_instance_map_t *prev;
} address_instance_map_t;

/* Linked list of address instance mappings */
typedef struct address_instance_map_ll
{
  address_instance_map_t *first;
  pthread_mutex_t mutex;
} address_instance_map_ll;

address_instance_map_ll *address_instance_map_alloc (void);

void address_instance_map_free (address_instance_map_ll *list);

address_instance_map_t *
address_instance_map_get (address_instance_map_ll *list, char *address);

void address_instance_map_set (address_instance_map_ll *list,
                               char *address, char *instance);

bool address_instance_map_remove (address_instance_map_ll *list, char *address);

#endif //DEVICE_BACNET_C_ADDRESS_INSTANCE_MAP_H
