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
#include "address_instance_map.h"

static address_instance_map_t *
address_instance_map_get_locked (address_instance_map_ll *list, char *address)
{
  address_instance_map_t *current = list->first;

  /* While the linked list is not empty*/
  while (current)
  {
    /* If the current element of the linked list is equal to the address argument*/
    if (current->address && strcmp (current->address, address) == 0)
    {
      break;
    }
    current = current->next;
  }
  /* Return NULL if not found */
  return current;
}


/* Create a new list */
address_instance_map_ll *address_instance_map_alloc (void)
{
  address_instance_map_ll *list = malloc (sizeof (address_instance_map_ll));
  list->first = NULL;
  pthread_mutex_init (&list->mutex, NULL);
  return list;
}

/* Remove all nodes from a linked list*/
void address_instance_map_free (address_instance_map_ll *list)
{

  address_instance_map_t *current = list->first;
  address_instance_map_t *next;

  while (current)
  {
    next = current->next;
    /* Free the link */
    free (current->address);
    free (current);
    current = next;
  }
  pthread_mutex_destroy (&list->mutex);
  free (list);
}

/* Function for finding the device instance corresponding to an IP address */
address_instance_map_t *
address_instance_map_get (address_instance_map_ll *list, char *address)
{
  if (!list)
  {
    return NULL;
  }
  pthread_mutex_lock (&list->mutex);
  address_instance_map_t *aim = address_instance_map_get_locked (list, address);
  pthread_mutex_unlock (&list->mutex);
  /* Return NULL if not found */
  return aim;
}

/* Function for adding new adress-instance pairs to an address instance linked list*/
void address_instance_map_set (address_instance_map_ll *list,
                               char *address, char *instance)
{
  address_instance_map_t *value;

  /* Allocate memory and copy values to new variable*/
  value = malloc (sizeof (address_instance_map_t));
  memset (value, 0, sizeof (address_instance_map_t));
  value->address = strdup (address);
  value->instance = (uint32_t) strtoul (instance, NULL, 10);
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
bool address_instance_map_remove (address_instance_map_ll *list, char *address)
{
  pthread_mutex_lock (&list->mutex);
  address_instance_map_t *aim = address_instance_map_get_locked (list, address);

  if (aim == NULL)
  {
    pthread_mutex_unlock (&list->mutex);
    return false;
  }
  /* If the current link is the first link */
  if (aim == list->first)
  {
    /* Update the first link to point to the next link */
    list->first = list->first->next;

    /* Set the next links previous pointer to NULL */
    if (aim->next)
    {
      aim->next->prev = NULL;
    }
  }
  else
  {
    /* Exclude the current link from the linked list */
    aim->prev->next = aim->next;
    if (aim->next)
    {
      aim->next->prev = aim->prev;
    }
  }
  pthread_mutex_unlock (&list->mutex);
  free (aim->address);
  free (aim);

  return true;
}