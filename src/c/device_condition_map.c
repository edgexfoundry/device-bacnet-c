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
#include <bacdef.h>
#include "device_condition_map.h"

static device_condition_map_t *
device_condition_map_get_locked (device_condition_map_ll *list, uint32_t device_id)
{
  device_condition_map_t *current = list->first;

  /* While the linked list is not empty*/
  while (current)
  {
    /* If the current element of the linked list is equal to the device ID */
    if (current->address && current->device_id == device_id)
    {
      break;
    }
    current = current->next;
  }
  return current;
}

/* Create a new list */
device_condition_map_ll *device_condition_map_alloc (void)
{
  device_condition_map_ll *list = malloc (sizeof (device_condition_map_ll));
  list->first = NULL;
  pthread_mutex_init (&list->mutex, NULL);
  return list;
}

/* Remove all nodes from a linked list*/
void device_condition_map_free (device_condition_map_ll *list)
{

  device_condition_map_t *current = list->first;
  device_condition_map_t *next;

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

/* Function for finding the device_condition_map corresponding to a device id */
device_condition_map_t *
device_condition_map_get (device_condition_map_ll *list, uint32_t device_id)
{
  if (!list)
  {
    return NULL;
  }
  pthread_mutex_lock (&list->mutex);
  device_condition_map_t *dcm = device_condition_map_get_locked (list, device_id);
  pthread_mutex_unlock (&list->mutex);
  return dcm;
}

/* Function for adding new address-id pairs to an device_condition_map list*/
void device_condition_map_set (device_condition_map_ll *list,
                               uint32_t device_id, BACNET_ADDRESS *address)
{
  device_condition_map_t *value;

  /* Allocate memory and copy values to new variable*/
  value = malloc (sizeof (device_condition_map_t));
  memset (value, 0, sizeof (device_condition_map_t));
  /* Add the given device ID */
  value->device_id = device_id;

  /* Initialize condition variable */
  pthread_cond_init (&value->condition, NULL);

  /* Initialize mutex used by condition variable */
  pthread_mutex_init (&value->mutex, NULL);

  /* Add the device address to avoid multiple lookups */
  value->address = address;
  value->prev = NULL;
  pthread_mutex_lock (&list->mutex);
  value->next = list->first;

  if (list->first != NULL)
  {
    list->first->prev = value;
  }

  /* Point the link head to the new link */
  list->first = value;
  pthread_mutex_unlock (&list->mutex);
}

/* Free and remove a single link from the linked list */
bool
device_condition_map_remove (device_condition_map_ll *list, uint32_t device_id)
{
  pthread_mutex_lock (&list->mutex);
  device_condition_map_t *dcm = device_condition_map_get_locked (list, device_id);

  if (dcm == NULL)
  {
    pthread_mutex_unlock (&list->mutex);
    return false;
  }
  /* If the current link is the first link */
  if (dcm == list->first)
  {
    /* Update the first link to point to the next link */
    list->first = list->first->next;

    /* Set the next links previous pointer to NULL */
    if (dcm->next)
    {
      dcm->next->prev = NULL;
    }
  }
  else
  {
    /* Exclude the current link from the linked list */
    dcm->prev->next = dcm->next;
    if (dcm->next)
    {
      dcm->next->prev = dcm->prev;
    }
  }
  pthread_mutex_unlock (&list->mutex);
  free (dcm);

  return true;
}