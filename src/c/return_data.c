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
#include "return_data.h"

static return_data_t *
return_data_get_locked (return_data_ll *list, uint8_t invoke_id)
{
  return_data_t *current = list->first;

  /* While the linked list is not empty*/
  while (current)
  {
    /* If the current element of the linked list is equal to the incoke ID*/
    if (current->requestInvokeID == invoke_id)
    {
      break;
    }
    current = current->next;
  }
  /* Return NULL if not found */
  return current;
}

static bool return_data_remove_by_ptr_locked (return_data_ll *list, return_data_t *data)
{
  if (data == NULL)
  {
    return false;
  }
  /* If the current link is the first link */
  if (data == list->first)
  {
    /* Update the first link to point to the next link */
    list->first = list->first->next;

    /* Set the next links previous pointer to NULL */
    if (data->next)
    {
      data->next->prev = NULL;
    }
  }
  else
  {
    /* Exclude the current link from the linked list */
    data->prev->next = data->next;
    if (data->next)
    {
      data->next->prev = data->prev;
    }
  }

  free (data);

  return true;
}

/* Create a new list */
return_data_ll *return_data_alloc (void)
{
  return_data_ll *list = malloc (sizeof (return_data_ll));
  list->first = NULL;
  pthread_mutex_init (&list->mutex, NULL);
  return list;
}

/* Remove all nodes from a linked list*/
void return_data_free (return_data_ll *list)
{

  return_data_t *current = list->first;
  return_data_t *next;

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

/* Function for finding the return_data structure for a invoke ID */
return_data_t *
return_data_get (return_data_ll *list, uint8_t invoke_id)
{
  if (!list)
  {
    return NULL;
  }
  pthread_mutex_lock (&list->mutex);
  return_data_t *entry = return_data_get_locked (list, invoke_id);
  pthread_mutex_unlock (&list->mutex);
  /* Return NULL if not found */
  return entry;
}

/* Function for adding new return_data structure linked list*/
return_data_t *return_data_set (return_data_ll *list,
                                uint8_t invoke_id)
{
  pthread_mutex_lock (&list->mutex);
  return_data_t *value;

  /* Allocate memory and copy values to new variable*/
  value = malloc (sizeof (return_data_t));
  memset (value, 0, sizeof (return_data_t));
  /* Add the given invoke ID */
  value->requestInvokeID = invoke_id;
  /* Allocate memory for return value*/
  memset (&value->value, 0, sizeof (BACNET_READ_ACCESS_DATA));
  /* Initialize condition variable, to indicate that the return value has not yet been set */
  pthread_cond_init (&value->condition, NULL);
  /* Initialize mutex used by condition variable */
  pthread_mutex_init (&value->mutex, NULL);
  /* Set the error detected flag to false */
  value->errorDetected = false;
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
bool return_data_remove (return_data_ll *list, uint8_t invoke_id)
{
  pthread_mutex_lock (&list->mutex);
  return_data_t *data = return_data_get_locked (list, invoke_id);
  bool ret = return_data_remove_by_ptr_locked (list, data);
  pthread_mutex_unlock (&list->mutex);

  return ret;
}

/* Free and remove a single link from the linked list */
bool return_data_remove_by_ptr (return_data_ll *list, return_data_t *data)
{

  pthread_mutex_lock (&list->mutex);
  bool ret = return_data_remove_by_ptr_locked (list, data);
  pthread_mutex_unlock (&list->mutex);

  return ret;
}
