/*
 * Copyright (c) 2019
 * IoTech Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <stddef.h>
#include <malloc.h>
#include <memory.h>
#include <stdlib.h>
#include <iot/os.h>
#include <iot/logger.h>
#include "address_entry.h"

/* Check if two BACnet addresses match */
static bool bacnet_address_matches (
  BACNET_ADDRESS *a1,
  BACNET_ADDRESS *a2)
{
  /* Return false if the net is not the same */
  if (a1->net != a2->net)
  {
    return false;
  }

  /* Return false if the length is not the same */
  if (a1->len != a2->len)
  {
    return false;
  }

  /* Iterate through the address array, and compare each element */
  for (int i = 0; i < a1->len; i++)
  {
    /* If and element of the array is not the same, return false */
    if (a1->adr[i] != a2->adr[i])
    {
      return false;
    }
  }

  /* Return true if all the previous check passed */
  return true;
}

static address_entry_t *address_entry_get_locked(address_entry_ll *list, uint32_t device_id) {

  address_entry_t *current = list->first;

  /* While the linked list is not empty*/
  while (current)
  {
    /* If the current element of the linked list is equal to the device ID*/
    if (current->device_id == device_id)
    {
      break;
    }
    current = current->next;
  }
  return current;
}


/* Create a new list */
address_entry_ll *address_entry_alloc (void)
{
  address_entry_ll *list = malloc (sizeof (address_entry_ll));
  list->first = NULL;
  pthread_mutex_init (&list->mutex, NULL);
  return list;
}

/* Remove all nodes from a linked list*/
void address_entry_free (address_entry_ll *list)
{
  address_entry_t *current = list->first;
  address_entry_t *next;

  while (current)
  {
    next = current->next;
    /* Free the link */
    free (current);
    current = next;
  }
  pthread_mutex_destroy (&list->mutex);
  free (list);
}

/* Function for finding an address_entry given a device ID */
address_entry_t *
address_entry_get (address_entry_ll *list, uint32_t device_id)
{
  pthread_mutex_lock (&list->mutex);
  address_entry_t *entry = address_entry_get_locked (list, device_id);
  pthread_mutex_unlock (&list->mutex);
  /* Return NULL if not found */
  return entry;
}

/* Function for adding new address_entry to a linked list*/
address_entry_t *address_entry_set (address_entry_ll *list,
                                    uint32_t device_id,
                                    unsigned max_apdu,
                                    BACNET_ADDRESS *src)
{
  pthread_mutex_lock (&list->mutex);
  address_entry_t *entry = address_entry_get_locked (list, device_id);
  uint8_t flags = 0;

  /* Check if the current element BACnet address or ID matches the argument BACnet address or ID */
  if (entry)
  {
    if (bacnet_address_matches (&entry->address, src) || device_id == entry->device_id)
    {
      /* Return if the element already exists */
      pthread_mutex_unlock (&list->mutex);
      return NULL;
    }
  }
  /* Set the flags to indicate multiple BACnet addresses */
  flags |= BAC_ADDRESS_MULT;

  address_entry_t *value;

  /* Allocate memory and copy values to new variable*/
  value = malloc (sizeof (address_entry_t));
  memset (value, 0, sizeof (address_entry_t));
  value->flags = flags;
  value->device_id = device_id;
  value->max_apdu = max_apdu;
  value->address = *src;
  value->prev = NULL;
  value->next = list->first;

  if (list->first != NULL)
  {
    list->first->prev = value;
  }

  /* Point the link head to the new link */
  list->first = value;
  pthread_mutex_unlock (&list->mutex);
  return value;
}

/* Free and remove a single link from the linked list */
void address_entry_remove (iot_logger_t *lc, address_entry_ll *list, uint32_t device_id)
{
  pthread_mutex_lock (&list->mutex);
  address_entry_t *entry = address_entry_get_locked (list, device_id);
  if (entry == NULL)
  {
    pthread_mutex_unlock (&list->mutex);
    iot_log_debug (lc, "Could not remove address_entry from list");
    return;
  }
  /* If the current link is the first link */
  if (entry == list->first)
  {
    /* Update the first link to point to the next link */
    list->first = list->first->next;

    /* Set the next links previous pointer to NULL */
    if (entry->next)
    {
      entry->next->prev = NULL;
    }
  }
  else
  {
    /* Exclude the current link from the linked list */
    entry->prev->next = entry->next;
    if (entry->next)
    {
      entry->next->prev = entry->prev;
    }
  }

  pthread_mutex_unlock (&list->mutex);
  free (entry);
}

address_entry_t *address_entry_pop (address_entry_ll *list) {
  pthread_mutex_lock (&list->mutex);
  address_entry_t *entry = list->first;
  if (entry)
  {
    /* Update the first link to point to the next link */
    list->first = list->first->next;

    /* Set the next links previous pointer to NULL */
    if (entry->next)
    {
      entry->next->prev = NULL;
    }
  }
  pthread_mutex_unlock (&list->mutex);
  return entry;
}