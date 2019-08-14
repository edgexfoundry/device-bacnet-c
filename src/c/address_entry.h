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

typedef struct address_entry_t
{
  uint8_t flags;
  uint32_t device_id;
  unsigned max_apdu;
  BACNET_ADDRESS address;
  struct address_entry_t *next;
  struct address_entry_t *prev;
} address_entry_t;

/* Linked list of address entries */
typedef struct address_entry_ll
{
  address_entry_t *first;
  pthread_mutex_t mutex;
} address_entry_ll;

#define BAC_ADDRESS_MULT 1

address_entry_ll *address_entry_alloc (void);

void address_entry_free (address_entry_ll *list);

address_entry_t *
address_entry_get (address_entry_ll *list, uint32_t device_id);

address_entry_t *address_entry_set (address_entry_ll *list,
                                    uint32_t device_id,
                                    unsigned max_apdu,
                                    BACNET_ADDRESS *src);

void address_entry_remove (iot_logger_t *lc, address_entry_ll *list, uint32_t device_id);

address_entry_t *address_entry_pop (address_entry_ll *list);