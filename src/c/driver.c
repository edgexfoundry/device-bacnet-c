/*
 * Copyright (c) 2019
 * IoTech Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

/* Based on code from https://sourceforge.net/projects/bacnet/ */

/*************************************************************************
* Copyright (C) 2006 Steve Karg <skarg@users.sourceforge.net>
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*
*********************************************************************/

/* command line tool that sends a BACnet service, and displays the reply */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>       /* for time */
#include <iot/logger.h>
#include <bacapp.h>
#include "rs485.h"
#include "driver.h"

#include "bacdef.h"
#include "config.h"
#include "bactext.h"
#include "bacerror.h"
#include "iam.h"
#include "arf.h"
#include "tsm.h"
#include "address.h"
#include "npdu.h"
#include "apdu.h"
#include "device.h"
#include "net.h"
#include "datalink.h"
#include "whois.h"
#include "device_condition_map.h"
#include "return_data.h"
/* some demo stuff needed */
#include "filename.h"
#include "handlers.h"
#include "client.h"
#include "txbuf.h"
#include "dlenv.h"

/* Logging client for the file */
static iot_logger_t *lc;

/* Static linked list of read/write/discovery calls and their return data */
return_data_ll *returnDataHead;

/* Static linked list used for condition variables for Who-Is/I-Am responses */
static device_condition_map_ll *deviceCondtionMapHead;

/* Empty address table */
static address_entry_ll *addressEntryHead;

/* Error handler for BACnet requests */
static void MyErrorHandler (
  BACNET_ADDRESS *src,
  uint8_t invoke_id,
  BACNET_ERROR_CLASS error_class,
  BACNET_ERROR_CODE error_code)
{
  /* Find the return data struct matching the given invoke id */
  return_data_t *data = return_data_get (returnDataHead, invoke_id);
  if (data != NULL)
  {
    /* Check that the addresses match */
    if (address_match (&data->targetAddress, src))
    {
      /* Print the error code*/
      iot_log_error (lc, "BACnet Error: %s: %s",
                     bactext_error_class_name ((unsigned) error_class),
                     bactext_error_code_name ((unsigned) error_code));

      /* Set the error detected variable to be true */
      data->errorDetected = true;
    }
    pthread_mutex_lock (&data->mutex);
    pthread_cond_signal (&data->condition);
    pthread_mutex_unlock (&data->mutex);
  }
}

/* Abort handler for BACnet requests */
static void MyAbortHandler (
  BACNET_ADDRESS *src,
  uint8_t invoke_id,
  uint8_t abort_reason,
  bool server)
{
  (void) server;

  /* Find the return data struct matching the given invoke id */
  return_data_t *data = return_data_get (returnDataHead, invoke_id);

  if (data != NULL)
  {
    /* Check that the addresses match */
    if (address_match (&data->targetAddress, src))
    {
      /* Print the abort reason */
      iot_log_error (lc, "BACnet Abort: %s",
                     bactext_abort_reason_name ((int) abort_reason));

      /* Set the error detected variable to be true */
      data->errorDetected = true;
    }
    pthread_mutex_lock (&data->mutex);
    pthread_cond_signal (&data->condition);
    pthread_mutex_unlock (&data->mutex);
  }
}

/* Reject handler for BACnet requests */
static void MyRejectHandler (
  BACNET_ADDRESS *src,
  uint8_t invoke_id,
  uint8_t reject_reason)
{
  /* Find the return data struct matching the given invoke id */
  return_data_t *data = return_data_get (returnDataHead, invoke_id);
  if (data != NULL)
  {
    /* Check that the addresses match */
    if (address_match (&data->targetAddress, src))
    {
      /* Print the reject reason */
      iot_log_error (lc, "BACnet Reject: %s",
                     bactext_reject_reason_name ((int) reject_reason));

      /* Set the error detected variable to be true */
      data->errorDetected = true;
    }
    pthread_mutex_lock (&data->mutex);
    pthread_cond_signal (&data->condition);
    pthread_mutex_unlock (&data->mutex);
  }
}

/** Handler for a ReadProperty ACK.
 * @param service_request [in] The contents of the service request.
 * @param service_len [in] The length of the service_request.
 * @param src [in] BACNET_ADDRESS of the source of the message
 * @param service_data [in] The BACNET_CONFIRMED_SERVICE_DATA information
 *                          decoded from the APDU header of this message.
 */
static void My_Read_Property_Ack_Handler (
  uint8_t *service_request,
  uint16_t service_len,
  BACNET_ADDRESS *src,
  BACNET_CONFIRMED_SERVICE_ACK_DATA *service_data)
{
  BACNET_READ_PROPERTY_DATA data;

  /* Find the return data struct matching the given invoke id */
  return_data_t *ret = return_data_get (returnDataHead,
                                        service_data->invoke_id);
  /* If a return data struct was found */
  if (ret != NULL && ret->value == NULL)
  {
    pthread_mutex_lock (&ret->mutex);
    /* Check that the addresses match */
    if (src && address_match (&ret->targetAddress, src))
    {
      /* Decode the service request */
      int len =
        rp_ack_decode_service_request (service_request, service_len, &data);

      /* If the service length decoding was successful */
      if (len > 0)
      {
        /* Decode the application data */
        ret->value = malloc (sizeof (BACNET_APPLICATION_DATA_VALUE));
        bacapp_decode_application_data (data.application_data,
                                        (uint8_t) data.application_data_len,
                                        ret->value);
      }
    }
    pthread_cond_signal (&ret->condition);
    pthread_mutex_unlock (&ret->mutex);
  }
}

/* I-Am handler for BACnet requests */
static void my_i_am_handler (
  uint8_t *service_request,
  uint16_t service_len,
  BACNET_ADDRESS *src)
{
  int len = 0;
  uint32_t device_id = 0;
  unsigned max_apdu = 0;
  int segmentation = 0;
  uint16_t vendor_id = 0;
  (void) service_len;

  /* Decode the service request */
  len =
    iam_decode_service_request (service_request, &device_id, &max_apdu,
                                &segmentation, &vendor_id);
  if (len != -1)
  {
    /* If the decoding of the service request was successful */
    iot_log_debug (lc, "Processing I-Am Request from %lu",
                   (unsigned long) device_id);
    /* If address is in address table, e.g. have already been sent read/write request */
    device_condition_map_t *map = device_condition_map_get (deviceCondtionMapHead,
                                                            device_id);
    if (map != NULL)
    {
      /* If the address length is 0, the device should be bound to */
      if (map->address->mac_len == 0)
      {
        /* Bind to the device */
        address_add_binding (device_id, max_apdu, src);
      }
      pthread_mutex_lock (&map->mutex);
      pthread_cond_signal (&map->condition);
      pthread_mutex_unlock (&map->mutex);
    }
    else
    {
      /* Add the device to the address table */
      address_entry_set (addressEntryHead, device_id, max_apdu, src);
    }
    return;
  }
  iot_log_error (lc, "Received I-Am, but unable to decode it.\n");
}

/* Write Property handler for BACnet requests */
static void MyWritePropertySimpleAckHandler (
  BACNET_ADDRESS *src,
  uint8_t invoke_id)
{
  /* Find the return data struct matching the given invoke id */
  return_data_t *ret = return_data_get (returnDataHead, invoke_id);

  if (ret != NULL)
  {
    pthread_mutex_lock (&ret->mutex);
    /* Check that the addresses match */
    if (src && address_match (&ret->targetAddress, src))
    {
      iot_log_debug (lc, "WriteProperty Acknowledged!");
    }
    pthread_cond_signal (&ret->condition);
    pthread_mutex_unlock (&ret->mutex);
  }
}

/* Add read value to the end of a list of readings */
BACNET_APPLICATION_DATA_VALUE *
bacnet_read_application_data_value_add (BACNET_APPLICATION_DATA_VALUE *head,
                                        BACNET_APPLICATION_DATA_VALUE *result)
{
  result->next = NULL;

  if (head == NULL)
  {
    return result;
  }

  BACNET_APPLICATION_DATA_VALUE *current = head;
  while (current->next)
  {
    current = current->next;
  }

  current->next = result;

  return head;
}


/* Function for getting the device instance from a ip address */
uint32_t ip_to_instance (bacnet_driver *driver, char *deviceInstance)
{
  if (getenv ("BACNET_BBMD_ADDRESS") && getenv ("BACNET_BBMD_PORT"))
  {
    iot_log_error (driver->lc,
                   "IP addresses cannot be used as BACnet device instance when BBMD is active");
    return UINT32_MAX;
  }

  struct sockaddr_in sa;
  address_entry_ll *at;
  /* If deviceInstance argument is an IP address */
  if (inet_pton (AF_INET, deviceInstance, &sa.sin_addr))
  {
    /* If the IP address does not already exist */
    address_instance_map_t *aim = address_instance_map_get (driver->aim_ll,
                                                            deviceInstance);
    if (aim != NULL)
    {
      return aim->instance;
    }
    {
      /* Make a Who-Is call and store the result in and address table */
      at = bacnetWhoIs ();
      /* Traverse address table and match ip address with deviceInstance */
      address_entry_t *current_address = at->first;
      while (current_address)
      {
        /* Check if the current address is an IP address */
        if (current_address->address.len == 0)
        {
          char address[IP_STRING_LENGTH];
          char instance[IP_STRING_LENGTH];
          /* Copy IP address and device instance into address instance map */
          sprintf (address, "%d.%d.%d.%d",
                   current_address->address.mac[0],
                   current_address->address.mac[1],
                   current_address->address.mac[2],
                   current_address->address.mac[3]);
          sprintf (instance, "%u", current_address->device_id);
          /* Check if the address instance mapping already exists */
          if (address_instance_map_get (driver->aim_ll, instance) == NULL)
          {
            /* Add mapping to linked list */
            address_instance_map_set (driver->aim_ll, address, instance);
          }
        }
        address_entry_t *tmp = current_address->next;
        address_entry_remove (driver->lc, at, current_address->device_id);
        current_address = tmp;
      }
      aim = address_instance_map_get (driver->aim_ll, deviceInstance);
      if (aim)
      {
        return aim->instance;
      }
    }
  }
  return 0;
}




/* Add reading to the end of a list of readings */
BACNET_READ_ACCESS_DATA *
bacnet_read_access_data_add (BACNET_READ_ACCESS_DATA *head,
                             BACNET_OBJECT_TYPE type,
                             BACNET_PROPERTY_ID property, uint32_t instance,
                             uint32_t index)
{

  BACNET_READ_ACCESS_DATA *data = malloc (sizeof (BACNET_READ_ACCESS_DATA));
  memset (data, 0, sizeof (BACNET_READ_ACCESS_DATA));
  data->object_type = type;
  data->object_instance = instance;
  data->listOfProperties = malloc (sizeof (BACNET_PROPERTY_REFERENCE));
  memset (data->listOfProperties, 0, sizeof (BACNET_PROPERTY_REFERENCE));
  data->listOfProperties->propertyIdentifier = property;
  data->listOfProperties->propertyArrayIndex = index;
  data->next = NULL;

  if (head == NULL)
  {
    return data;
  }

  BACNET_READ_ACCESS_DATA *current = head;
  while (current->next)
  {
    current = current->next;
  }

  current->next = data;

  return head;
}

/* Add writing to a list of writings */
BACNET_WRITE_ACCESS_DATA *
bacnet_write_access_data_add (BACNET_WRITE_ACCESS_DATA *head,
                              BACNET_OBJECT_TYPE type,
                              BACNET_PROPERTY_ID property, uint32_t instance,
                              uint32_t index,
                              BACNET_APPLICATION_DATA_VALUE value,
                              uint8_t priority)
{

  BACNET_WRITE_ACCESS_DATA *data = malloc (sizeof (BACNET_WRITE_ACCESS_DATA));
  memset (data, 0, sizeof (BACNET_WRITE_ACCESS_DATA));
  data->object_type = type;
  data->object_instance = instance;
  data->listOfProperties = malloc (sizeof (BACNET_PROPERTY_VALUE));
  memset (data->listOfProperties, 0, sizeof (BACNET_PROPERTY_VALUE));
  data->listOfProperties->propertyIdentifier = property;
  data->listOfProperties->propertyArrayIndex = index;
  data->listOfProperties->value = value;
  data->listOfProperties->priority = priority;
  data->next = head;

  return data;
}

/* Populates the BACNET_READ_ACCESS_DATA structure to be used for reading */
bool
read_access_data_populate (BACNET_READ_ACCESS_DATA **head, uint32_t nreadings,
                           const devsdk_commandrequest *requests,
                           bacnet_driver *driver)
{
  /* Traverse the requested readings */
  for (uint32_t i = 0; i < nreadings; i++)
  {
    bacnet_attributes_t *attrs = (bacnet_attributes_t *)requests[i].resource->attrs;
    *head = bacnet_read_access_data_add (*head, attrs->type, attrs->property, attrs->instance, attrs->index);
  }
  return true;
}

void read_access_data_free (BACNET_READ_ACCESS_DATA *head)
{
  BACNET_READ_ACCESS_DATA *current_data = head;
  while (current_data != NULL)
  {
    head = current_data->next;
    free (current_data
            ->listOfProperties);
    free (current_data);
    current_data = head;
  }
}

void devsdk_commandresult_populate (devsdk_commandresult *readings,
                                          BACNET_APPLICATION_DATA_VALUE *read_results,
                                          uint32_t nreadings)
{
  BACNET_APPLICATION_DATA_VALUE *deviceReading = read_results;
  for (uint32_t i = 0; i < nreadings && deviceReading; i++)
  {
    /* Check the value of the returned data type, and set the edgex_device_commandresult
     * to the returned value and type
     */
    switch ((int) deviceReading->tag)
    {
      case BACNET_APPLICATION_TAG_BOOLEAN:
        readings[i].value = iot_data_alloc_bool (deviceReading->type.Boolean);
        break;
      case BACNET_APPLICATION_TAG_CHARACTER_STRING:
      {
        readings[i].value = iot_data_alloc_string (deviceReading->type.Character_String.value, IOT_DATA_COPY);
        break;
      }
      case BACNET_APPLICATION_TAG_UNSIGNED_INT:
        readings[i].value = iot_data_alloc_ui32 (deviceReading->type.Unsigned_Int);
        break;
      case BACNET_APPLICATION_TAG_SIGNED_INT:
        readings[i].value = iot_data_alloc_i32 (deviceReading->type.Signed_Int);
        break;
      case BACNET_APPLICATION_TAG_REAL:
        readings[i].value = iot_data_alloc_f32 (deviceReading->type.Real);
        break;
      case BACNET_APPLICATION_TAG_DOUBLE:
        readings[i].value = iot_data_alloc_f64 (deviceReading->type.Double);
        break;
      default:
        break;
    }
    BACNET_APPLICATION_DATA_VALUE *next_reading = deviceReading->next;
    free (deviceReading);
    deviceReading = next_reading;
  }
}

bool
write_access_data_populate (BACNET_WRITE_ACCESS_DATA **head, uint32_t nvalues,
                            const devsdk_commandrequest *requests,
                            const iot_data_t *values[],
                            bacnet_driver *driver)
{
  uint8_t priority = 1;
  /* Traverse the requested readings */
  for (uint32_t i = 0; i < nvalues; i++)
  {
    /* Get attributes from profile */
    bacnet_attributes_t *attrs = (bacnet_attributes_t *)requests[i].resource->attrs;

    BACNET_APPLICATION_DATA_VALUE value = {0};
    /* Convert the value to a string, and set application tag to be the correct type */
    switch (iot_data_type(values[i]))
    {
      case IOT_DATA_BOOL:
        iot_log_debug (driver->lc, "Bool");
        value.tag = BACNET_APPLICATION_TAG_BOOLEAN;
        value.type.Boolean = iot_data_bool (values[i]);
        break;
      case IOT_DATA_STRING:
        iot_log_debug (driver->lc, "String");
        value.tag = BACNET_APPLICATION_TAG_CHARACTER_STRING;
        characterstring_init_ansi (
          &value.type.Character_String,
          iot_data_string (values[i]));
        break;
      case IOT_DATA_UINT8:
        iot_log_debug (driver->lc, "Uint8");
        value.tag = BACNET_APPLICATION_TAG_UNSIGNED_INT;
        value.type.Unsigned_Int = iot_data_ui8 (values[i]);
        break;
      case IOT_DATA_UINT16:
        iot_log_debug (driver->lc, "Uint16");
        value.tag = BACNET_APPLICATION_TAG_UNSIGNED_INT;
        value.type.Unsigned_Int = iot_data_ui16 (values[i]);
        break;
      case IOT_DATA_UINT32:
        iot_log_debug (driver->lc, "Uint32");
        value.tag = BACNET_APPLICATION_TAG_UNSIGNED_INT;
        value.type.Unsigned_Int = iot_data_ui32 (values[i]);
        break;
      case IOT_DATA_UINT64:
        iot_log_debug (driver->lc, "Uint64 is not supported");
        break;
      case IOT_DATA_INT8:
        iot_log_debug (driver->lc, "Int8");
        value.tag = BACNET_APPLICATION_TAG_SIGNED_INT;
        value.type.Signed_Int = iot_data_i8 (values[i]);
        break;
      case IOT_DATA_INT16:
        iot_log_debug (driver->lc, "Int16");
        value.tag = BACNET_APPLICATION_TAG_SIGNED_INT;
        value.type.Signed_Int = iot_data_i16 (values[i]);
        break;
      case IOT_DATA_INT32:
        iot_log_debug (driver->lc, "Int32");
        value.tag = BACNET_APPLICATION_TAG_SIGNED_INT;
        value.type.Signed_Int = iot_data_i32 (values[i]);
        break;
      case IOT_DATA_INT64:
        iot_log_debug (driver->lc, "Int64 is not supported");
        break;
      case IOT_DATA_FLOAT32:
        iot_log_debug (driver->lc, "Float32");
        value.tag = BACNET_APPLICATION_TAG_REAL;
        value.type.Real = iot_data_f32 (values[i]);
        break;
      case IOT_DATA_FLOAT64:
        iot_log_debug (driver->lc, "Float64");
        value.tag = BACNET_APPLICATION_TAG_DOUBLE;
        value.type.Double = iot_data_f64 (values[i]);
        break;
      default:
        iot_log_error (driver->lc,
                       "The value type %d is not accepted", iot_data_type(values[i]));
        write_access_data_free (*head);
        return false;
    }
    *head = bacnet_write_access_data_add (*head,
                                         attrs->type, attrs->property, attrs->instance,
                                         attrs->index, value, priority);
  }
  return true;
}

void write_access_data_free (BACNET_WRITE_ACCESS_DATA *head)
{
  while (head != NULL)
  {
    BACNET_WRITE_ACCESS_DATA *current_data = head;
    head = head->next;
    free (current_data->listOfProperties);
    free (current_data);
  }
}

bool
get_device_properties (address_entry_t *device, uint16_t port, iot_logger_t *lc,
                       char **name, char **description, devsdk_strings *labels,
                       char **profile_name)
{
/* Get the device name for the discovered device */
  BACNET_APPLICATION_DATA_VALUE *name_value = bacnetReadProperty (
    device->device_id, OBJECT_DEVICE, UINT32_MAX, PROP_OBJECT_NAME,
    UINT32_MAX, port);

  if (!name_value)
  {
    iot_log_error (lc,
                   "Could not read name from device with device instance %d",
                   device->device_id);
    return false;
  }

  /* If the device name cannot be read, break */
  if (name_value->tag !=
      BACNET_APPLICATION_TAG_CHARACTER_STRING)
  {
    iot_log_error (lc, "Device name could not be read");
    return false;
  }
  *name = strdup (name_value->type.Character_String.value);
  iot_log_debug (lc, "Found device");
  iot_log_debug (lc, "Device name: %s", *name);

  /* Set the EdgeX device description */
  *description = malloc (strlen (*name) + strlen (DISCOVERY_DESCRIPTION) + 2);
  sprintf (*description, "%s %s",
           *name, DISCOVERY_DESCRIPTION);

  /* Get the device profile name by removing the device specific name identifier in the device name. */
  char *delimit = strdup (*name);
  *profile_name = strdup (strtok (delimit, "_"));
  iot_log_debug (lc, "Device Profile name should be: %s", *profile_name);

  /* Setup EdgeX labels. These are currently hardcoded */
  memset (labels, 0, sizeof (devsdk_strings));
  labels->str = "BACnet";

  free (delimit);
  free (name_value);
  return true;
}

void bacnet_protocol_populate (address_entry_t *device, iot_data_t *properties, bacnet_driver *driver)
{
  /* Get the device instance as string */
  char *deviceInstance = malloc (BACNET_MAX_INSTANCE_LENGTH);
  memset (deviceInstance, 0, BACNET_MAX_INSTANCE_LENGTH);
  sprintf (deviceInstance, "%u", device->device_id);

  /* Add the device instance to the list of protocol properties */
  iot_data_string_map_add (properties, "DeviceInstance", iot_data_alloc_string (deviceInstance, IOT_DATA_TAKE));

#ifdef BACDL_MSTP
  /* Add the MSTP Path to the list of protocol properties */
  iot_data_string_map_add (properties, "Path", iot_data_alloc_string (driver->default_device_path, IOT_DATA_COPY));
#else
  /* Get port of device as a string */
  uint16_t devicePort =
    (uint16_t) (device->address.mac[4] * 0x100u) +
    device->address.mac[5];
  char *portstring = malloc (MAX_PORT_LENGTH);
  memset (portstring, 0, MAX_PORT_LENGTH);
  sprintf (portstring, "%d", devicePort);

  /* Add the Port to the list of protocol properties */
  iot_data_string_map_add (properties, "Port", iot_data_alloc_string (portstring, IOT_DATA_TAKE));
#endif
}

static void *receive_data (void *running)
{
  BACNET_ADDRESS src = {0};
  uint8_t Rx_Buf[MAX_MPDU] = {0};
  unsigned timeout = 100;
  /* Run thread until device service stops */
  while (*(bool *) running)
  {
    /* Receive data */
    uint16_t pdu_len = datalink_receive (&src, &Rx_Buf[0], MAX_MPDU, timeout);

    /* If there is any data */
    if (pdu_len)
    {
      /* Handle the collected data */
      npdu_handler (&src, &Rx_Buf[0], pdu_len);
    }
  }
  return NULL;
}

/* Initialize BACnet handlers */
static void init_service_handlers (void)
{
  Device_Init (NULL);
  /* set the handler for all the services we don't implement
   It is required to send the proper reject message... */
  apdu_set_unrecognized_service_handler_handler
    (handler_unrecognized_service);
  /* handle the reply (request) coming back */
  apdu_set_unconfirmed_handler (SERVICE_UNCONFIRMED_I_AM, my_i_am_handler);
  /* we must implement read property - it's required! */
  apdu_set_confirmed_handler (SERVICE_CONFIRMED_READ_PROPERTY,
                              handler_read_property);
  apdu_set_confirmed_ack_handler (SERVICE_CONFIRMED_READ_PROPERTY,
                                  My_Read_Property_Ack_Handler);
  /* handle the ack coming back */
  apdu_set_confirmed_simple_ack_handler (SERVICE_CONFIRMED_WRITE_PROPERTY,
                                         MyWritePropertySimpleAckHandler);
  /* handle any errors coming back */
  apdu_set_error_handler (SERVICE_CONFIRMED_READ_PROPERTY, MyErrorHandler);
  apdu_set_error_handler (SERVICE_CONFIRMED_WRITE_PROPERTY, MyErrorHandler);
  apdu_set_abort_handler (MyAbortHandler);
  apdu_set_reject_handler (MyRejectHandler);
}

/* Initialize the BACnet driver */
int init_bacnet_driver (pthread_t *datalink_thread, bool *running,
                        iot_logger_t *logging_client)
{
  /* Initialize the service handlers */
  init_service_handlers ();
  /* Set the BACnet instance number of the device service the maximum BACnet instance */
  Device_Set_Object_Instance_Number (BACNET_MAX_INSTANCE);
  /* Initialize address binding table */
  address_init ();
  /* Setup device service info */
  if (dlenv_init () != 0)
  {
#ifdef BACDL_MSTP
    iot_log_error (lc, "Could not initialize %s", RS485_Interface ());
#endif
    return 1;
  }

  /* Setup logging */
  lc = logging_client;
  deviceCondtionMapHead = device_condition_map_alloc ();
  returnDataHead = return_data_alloc ();
  addressEntryHead = address_entry_alloc ();
  /* Create and run thread for getting data */
  pthread_create (datalink_thread, NULL, receive_data, (void *) running);
  return 0;
}

/* Deinitialize BACnet driver */
void deinit_bacnet_driver (pthread_t *datalink_thread, bool *running)
{
  /* Stop the loop in datalink thread */
  *running = false;

  /* Join datalink thread with current thread */
  pthread_join (*datalink_thread, NULL);

  /* Cleanup datalink */
  datalink_cleanup ();

  /* Free memory for returnData */
  address_entry_free (addressEntryHead);
  device_condition_map_free (deviceCondtionMapHead);
  return_data_free (returnDataHead);
}

/* Send Who-Is request to a device */
bool
find_and_bind (return_data_t *data, uint16_t port, uint32_t deviceInstance)
{
  BACNET_ADDRESS src = {0};
  time_t last_seconds = time (NULL);
  time_t current_seconds = 0;
  time_t timeout_seconds = (apdu_timeout () / 1000) * apdu_retries ();
  unsigned max_apdu = 0;
  struct timeval now;
  struct timespec timeout;

  /* Check for valid device instance */
  if (deviceInstance > BACNET_MAX_INSTANCE)
  {
    iot_log_error (lc, "device-instance=%u - it must be less than %u",
                   deviceInstance, BACNET_MAX_INSTANCE);
    return false;
  }

#ifdef BACDL_BIP
  /* Set BACnet port */
  bip_set_port (htons ((uint16_t) port));
#endif

  /* Try to bind */
  bool found = address_bind_request (deviceInstance, &max_apdu,
                                     &data->targetAddress);
  device_condition_map_t *map;
  /* Binding was successful */
  if (found == true)
  {
    return true;
  }

  device_condition_map_set (deviceCondtionMapHead, deviceInstance, &src);
  map = device_condition_map_get (deviceCondtionMapHead, deviceInstance);
  /* Send Who-Is call */
  pthread_mutex_lock (&map->mutex);
  Send_WhoIs (deviceInstance,
              deviceInstance);
  /* Get the current time */
  current_seconds = time (NULL);
  gettimeofday (&now, NULL);
  timeout.tv_sec = now.tv_sec + timeout_seconds;
  timeout.tv_nsec = 0;

  /* Wait for devices to respond */
  pthread_cond_timedwait (&map->condition, &map->mutex, &timeout);
  pthread_mutex_unlock (&map->mutex);
  device_condition_map_remove (deviceCondtionMapHead, deviceInstance);

  /* Break if an error has been detected */
  if (data->errorDetected)
  {
    return false;
  }

  /* Try to bind */
  found =
    address_bind_request (deviceInstance, &max_apdu,
                          &data->targetAddress);

  /* Bind request has returned successfully */
  if (found)
  {
    /* Make sure that a call has not already been executed with the return value struct */
    if (data->requestInvokeID == 0)
    {
      return true;
    }
    else if (tsm_invoke_id_free (data->requestInvokeID))
    {
      return false;
    }
    else if (tsm_invoke_id_failed (data->requestInvokeID))
    {
      iot_log_error (lc, "Error: TSM Timeout!");
      tsm_free_invoke_id (data->requestInvokeID);
      data->errorDetected = true;
      return false;
    }
  }
  else
  {
    /* Increment timer - return if timed out */
    time_t elapsed_seconds = (current_seconds - last_seconds);
    if (elapsed_seconds > timeout_seconds)
    {
      iot_log_error (lc, "Error: APDU Timeout!");
      data->errorDetected = true;
      return false;
    }
  }
  return false;
}

/* Wait for the data to be returned */
bool wait_for_data (return_data_t *data)
{

  /* Setup timers */
  time_t current_seconds = 0;
  time_t elapsed_seconds = 0;
  time_t timeout_seconds = (apdu_timeout () / 1000) * apdu_retries ();
  time_t last_seconds = 0;
  struct timeval now;
  struct timespec timeout;
  gettimeofday (&now, NULL);
  timeout.tv_sec = now.tv_sec + timeout_seconds;
  timeout.tv_nsec = 0;

  /* Wait until data is set */
  pthread_cond_timedwait (&data->condition, &data->mutex, &timeout);
  pthread_mutex_unlock (&data->mutex);
  if (data->errorDetected)
  {
    return false;
  }
  /* Increment timer - return if timed out */
  elapsed_seconds += (current_seconds - last_seconds);
  if (elapsed_seconds > timeout_seconds)
  {
    iot_log_error (lc, "Error: APDU Timeout!");
    data->errorDetected = true;
    return false;
  }
  return true;
}

/* Read Property BACnet call */
BACNET_APPLICATION_DATA_VALUE *bacnetReadProperty (
  uint32_t deviceInstance, int type, uint32_t instance, int property,
  uint32_t index, uint16_t port)
{
  /* Insert return_data structure with invoke id 0 and get pointer */
  return_data_t *data = return_data_set (returnDataHead, 0);
  /* Try to bind to device */
  if (!find_and_bind (data, port, deviceInstance))
  {
    return_data_remove_by_ptr (returnDataHead, data);
    return NULL;
  }
  /* Send read property request */
  pthread_mutex_lock (&data->mutex);
  pthread_mutex_lock (&returnDataHead->mutex);
  data->requestInvokeID =
    Send_Read_Property_Request (deviceInstance,
                                type,
                                instance,
                                property,
                                index);
  pthread_mutex_unlock (&returnDataHead->mutex);
  /* Wait for data to be set */
  wait_for_data (data);
  /* Get copy of value pointer */
  BACNET_APPLICATION_DATA_VALUE *ret = data->value;

  return_data_remove_by_ptr (returnDataHead, data);

  return ret;
}

/* Issue Who-Is BACnet call to all devices */
address_entry_ll *bacnetWhoIs ()
{
  time_t timeout_seconds = (apdu_timeout () / 1000) * apdu_retries ();
  BACNET_ADDRESS dest;
  static int32_t Target_Object_Instance_Min = -1;
  static int32_t Target_Object_Instance_Max = -1;
  struct timeval now;
  struct timespec timeout;
  gettimeofday (&now, NULL);
  timeout.tv_sec = now.tv_sec + timeout_seconds;
  timeout.tv_nsec = 0;

  /* Setup returnData to allow for error handling */
  return_data_t *data = return_data_set (returnDataHead, UINT8_MAX);
  data->errorDetected = false;

  /* Get address for broadcasting */
  datalink_get_broadcast_address (&dest);

#ifdef BACDL_BIP
  /* Setup port to be 0xBAC0 */
  bip_set_port (htons (0xBAC0));
#endif


  /* Send Who-Is request */
  pthread_mutex_lock (&data->mutex);
  Send_WhoIs_To_Network (&dest, Target_Object_Instance_Min,
                         Target_Object_Instance_Max);

  /* Wait for until timeout or error is set */
  pthread_cond_timedwait (&data->condition, &data->mutex, &timeout);
  pthread_mutex_unlock (&data->mutex);

  /* Free return data structure */
  return_data_remove_by_ptr (returnDataHead, data);

  /* Return address table containing discovered devices */
  return addressEntryHead;
}

/* Issue WriteProperty BACnet call */
int bacnetWriteProperty (
  uint32_t deviceInstance, int type, uint32_t instance, int property,
  uint32_t index, uint16_t port, uint8_t priority,
  BACNET_APPLICATION_DATA_VALUE *value)
{
  return_data_t *data = return_data_set (returnDataHead, 0);
  data->errorDetected = false;

  /* Bind to device */
  if (!find_and_bind (data, port, deviceInstance))
  {
    return_data_remove_by_ptr (returnDataHead, data);
    return 1;
  }

  /* Send Write Property request */
  pthread_mutex_lock (&data->mutex);
  pthread_mutex_lock (&returnDataHead->mutex);
  data->requestInvokeID =
    Send_Write_Property_Request (deviceInstance,
                                 type, instance,
                                 property,
                                 &value[0],
                                 priority,
                                 index);
  pthread_mutex_unlock (&returnDataHead->mutex);

  /* Wait for data to be set */
  if (!wait_for_data (data))
  {
    return 1;
  }

  /* Free the returned data */
  return_data_remove_by_ptr (returnDataHead, data);

  return 0;
}

void print_read_error(iot_logger_t *lc, BACNET_READ_ACCESS_DATA *data) {
  iot_log_error (lc, "Value could not be read for: ");
  iot_log_error (lc, "Type: %d", data->object_type);
  iot_log_error (lc, "Instance: %d", data->object_instance);
  iot_log_error (lc, "Property: %d",
                 data->listOfProperties->propertyIdentifier);
  iot_log_error (lc, "Index: %d",
                 data->listOfProperties->propertyArrayIndex);
}
