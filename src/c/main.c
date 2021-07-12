/* BACnet implementation of an Edgex device service using C SDK */

/*
 * Copyright (c) 2019
 * IoTech Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "devsdk/devsdk.h"
#include "edgex/devices.h"

#include <unistd.h>
#include <signal.h>
#include <bacapp.h>
#include <device.h>
#include <bacnet.h>
#include <dlenv.h>
#include <regex.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <stdarg.h>
#include "rs485.h"
#include "math.h"
#include "driver.h"
#include "address_instance_map.h"

#define ERR_CHECK(x) if (x.code) { fprintf (stderr, "Error: %d: %s\n", x.code, x.reason); return x.code; }
#define SMALL_STACK 100000

typedef struct
{
  const char *string;
  const uint32_t type;
} stringValueMap;

typedef struct
{
  uint16_t port;
  uint32_t deviceInstance;
} bacnet_address_t;

/* --- Initialize ---- */
/* Initialize performs protocol-specific initialization for the device
 * service.
 */
static bool bacnet_init
  (
    void *impl,
    struct iot_logger_t *lc,
    const iot_data_t *config
  )
{
  bacnet_driver *driver = (bacnet_driver *) impl;
  driver->lc = lc;
#ifdef BACDL_MSTP
  driver->default_device_path = iot_data_string_map_get_string (config, "DefaultDevicePath");

  /* Fail if the interface does not exist on the system */
  if (access (driver->default_device_path, F_OK ) == -1 )
  {
    iot_log_error (driver->lc, "The default device path \"%s\" is not available", driver->default_device_path);
    return false;
  }

  /* Set the environment variable used by the BACnet stack to initialize the interface */
  setenv ("BACNET_IFACE", driver->default_device_path, 1);
#else

  /* Set environment variables for BBMD if requested */
  const char *addr = iot_data_string_map_get_string (config, "BBMD_ADDRESS");
  if (*addr)
  {
    setenv("BACNET_BBMD_ADDRESS", addr, 1);
  }
  const char *port = iot_data_string_map_get_string (config, "BBMD_PORT");
  if (*port)
  {
    setenv("BACNET_BBMD_PORT", port, 1);
  }
#endif

  driver->aim_ll = address_instance_map_alloc ();
  driver->running_thread = true;

  if (init_bacnet_driver (&driver->datalink_thread, &driver->running_thread, lc) != 0)
  {
    iot_log_error (driver->lc, "An error occurred while initializing the BACnet driver");
    deinit_bacnet_driver (&driver->datalink_thread, &driver->running_thread);
    return false;
  }
  iot_log_debug (driver->lc, "Init");
  return true;
}

static iot_data_t *bacnet_alloc_exception (char *fmt, ...)
{
  va_list args;
  va_start (args, fmt);
  int n = vsnprintf (NULL, 0, fmt, args);
  char *str = malloc (n);
  va_end (args);
  va_start (args, fmt);
  vsprintf (str, fmt, args);
  va_end (args);
  return iot_data_alloc_string (str, IOT_DATA_TAKE);
}

static uint32_t parseInt (const iot_data_t *map, const char *name, uint32_t dfl, iot_data_t **exc)
{
  const iot_data_t *elem = iot_data_string_map_get (map, name);
  if (elem == NULL)
  {
    return dfl;
  }
  if (iot_data_type (elem) != IOT_DATA_INT64)
  {
    if (*exc == NULL)
    {
      *exc = bacnet_alloc_exception ("Attribute '%s' must be integer", name);
    }
    return dfl;
  }
  return iot_data_i64 (elem);
}

#ifndef BACDL_MSTP
static uint32_t parseStringInt (const iot_data_t *map, const char *name, uint32_t dfl, iot_data_t **exc)
{
  const char *elem = iot_data_string_map_get_string (map, name);
  return elem ? strtol (elem, NULL, 0) : dfl;
}
#endif

static BACNET_PROPERTY_ID parseProperty (const iot_data_t *property, iot_data_t **exc)
{
  const stringValueMap bacnetPropertyMap[] =
  {
    {"present-value", PROP_PRESENT_VALUE},
    {"object-name",   PROP_OBJECT_NAME},
  };

  if (property == NULL)
  {
    return PROP_PRESENT_VALUE;
  }
  if (iot_data_type (property) == IOT_DATA_STRING)
  {
    const char *str = iot_data_string (property);
    for (int i = 0; i < (sizeof (bacnetPropertyMap) / sizeof (bacnetPropertyMap[0])); i++)
    {
      if (strcmp (str, bacnetPropertyMap[i].string) == 0)
      {
        return bacnetPropertyMap[i].type;
      }
    }
    if (*exc == NULL)
    {
      *exc = iot_data_alloc_string ("Unknown BACnet property name", IOT_DATA_REF);
    }
    return 0;
  }
  else
  {
    return iot_data_i64 (property);
  }
}

static BACNET_OBJECT_TYPE parseType (const iot_data_t *type, iot_data_t **exc)
{
  const stringValueMap bacnetTypeMap[] =
  {
    {"analog-input",  OBJECT_ANALOG_INPUT},
    {"analog-output", OBJECT_ANALOG_OUTPUT},
    {"analog-value",  OBJECT_ANALOG_VALUE},
    {"binary-input",  OBJECT_BINARY_INPUT},
    {"binary-output", OBJECT_BINARY_OUTPUT},
    {"binary-value",  OBJECT_BINARY_VALUE},
    {"device",        OBJECT_DEVICE}
  };

  if (type == NULL)
  {
    if (*exc == NULL)
    {
      *exc = iot_data_alloc_string ("Attribute 'type' is required", IOT_DATA_REF);
    }
    return 0;
  }
  if (iot_data_type (type) == IOT_DATA_STRING)
  {
    const char *str = iot_data_string (type);
    for (int i = 0; i < (sizeof (bacnetTypeMap) / sizeof (bacnetTypeMap[0])); i++)
    {
      if (strcmp (str, bacnetTypeMap[i].string) == 0)
      {
        return bacnetTypeMap[i].type;
      }
    }
    if (*exc == NULL)
    {
      *exc = iot_data_alloc_string ("Unknown BACnet type name", IOT_DATA_REF);
    }
    return 0;
  }
  else
  {
    return iot_data_i64 (type);
  }
}

static devsdk_resource_attr_t bacnet_getattributes (void *impl, const iot_data_t *device_attr, iot_data_t **exception)
{
  bacnet_attributes_t *attrs = (bacnet_attributes_t *) calloc (1, sizeof (bacnet_attributes_t));
  attrs->instance = parseInt (device_attr, "instance", BACNET_MAX_INSTANCE, exception);
  attrs->property = parseProperty (iot_data_string_map_get (device_attr, "property"), exception);
  attrs->type = parseType (iot_data_string_map_get (device_attr, "type"), exception);
  attrs->index = parseInt (device_attr, "index", 0xFFFFFFFF, exception);
  if (attrs->instance == BACNET_MAX_INSTANCE && *exception == NULL)
  {
    *exception = bacnet_alloc_exception ("Attribute 'instance' is required");
  }
  if (*exception)
  {
    free (attrs);
    return NULL;
  }
  else
  {
    return attrs;
  }
}

static void bacnet_freeattributes (void *impl, devsdk_resource_attr_t attrs)
{
  free (attrs);
}

#ifdef BACDL_MSTP

static const char *defaultPath = "(default)";

static devsdk_address_t bacnet_getaddress (void *impl, const devsdk_protocols *protocols, iot_data_t **exception)
{
  const char *result = defaultPath;
  const iot_data_t *props = devsdk_protocols_properties (protocols, "BACnet-MSTP");
  if (props)
  {
    result = iot_data_string_map_get_string (props, "Path");
  }
  return (devsdk_address_t)result;
}

static void bacnet_freeaddress (void *impl, devsdk_address_t address)
{
}

#else

static devsdk_address_t bacnet_getaddress (void *impl, const devsdk_protocols *protocols, iot_data_t **exception)
{
  const iot_data_t *props = devsdk_protocols_properties (protocols, "BACnet-IP");
  if (props)
  {
    uint32_t inst = parseStringInt (props, "DeviceInstance", UINT32_MAX, exception);
    uint16_t port = parseStringInt (props, "Port", 0xBAC0, exception);
    if (inst == UINT32_MAX && *exception == NULL)
    {
      *exception = iot_data_alloc_string ("DeviceInstance must be specified", IOT_DATA_REF);
    }
    if (*exception)
    {
      return NULL;
    }
    else
    {
      bacnet_address_t *result = malloc (sizeof (bacnet_address_t));
      result->deviceInstance = inst;
      result->port = port;
      return result;
    }
  }
  else
  {
    *exception = iot_data_alloc_string ("BACnet-IP protocol must be specified", IOT_DATA_REF);
    return NULL;
  }
}

static void bacnet_freeaddress (void *impl, devsdk_address_t address)
{
  free (address);
}

#endif

static bool get_supported_services (uint32_t device_id, uint16_t port, iot_data_t *properties)
{
  /* Get the supported BACnet services */
  BACNET_APPLICATION_DATA_VALUE *bacnet_services =
    bacnetReadProperty (device_id, OBJECT_DEVICE, UINT32_MAX,
                        PROP_PROTOCOL_SERVICES_SUPPORTED, UINT32_MAX, port);
  if (bacnet_services == NULL)
  {
    return false;
  }
  if (bitstring_bit (&bacnet_services->type.Bit_String,
                     SERVICE_SUPPORTED_READ_PROP_MULTIPLE))
  {
    iot_data_string_map_add (properties, "DS-RPM-B", iot_data_alloc_string ("true", IOT_DATA_REF));
  }
  if (bitstring_bit (&bacnet_services->type.Bit_String,
                     SERVICE_SUPPORTED_WRITE_PROPERTY))
  {
    iot_data_string_map_add (properties, "DS-WP-B", iot_data_alloc_string ("true", IOT_DATA_REF));
  }
  if (bitstring_bit (&bacnet_services->type.Bit_String,
                     SERVICE_SUPPORTED_WRITE_PROP_MULTIPLE))
  {
    iot_data_string_map_add (properties, "DS-WPM-B", iot_data_alloc_string ("true", IOT_DATA_REF));
  }

  free (bacnet_services);
  return true;
}

/* ---- Discovery ---- */
/* Device services which are capable of device discovery should implement it
 * in this callback. It is called in response to a request on the
 * device service's discovery REST   endpoint. New devices should be added using
 * the edgex_add_device () method
 */
static void bacnet_discover (void *impl)
{
  bacnet_driver *driver = (bacnet_driver *) impl;

  iot_log_debug (driver->lc, "Running BACnet Discovery");
#ifdef BACDL_MSTP
  /* Set default interface */
  RS485_Set_Interface ((char *)driver->default_device_path);
#endif
  /* Send Who-Is/I-Am call and put responsive devices into address_table */
  address_entry_ll *discover_table = bacnetWhoIs ();
  address_entry_t *discovered_device;
  /* Try to set up all devices who responded to Who-Is call */
  while ((discovered_device = address_entry_pop(discover_table)))
  {
    char *name = NULL;
    char *description = NULL;
    char *profile = NULL;
    devsdk_strings *labels = malloc (sizeof (devsdk_strings));
    memset (labels, 0, sizeof (devsdk_strings));
    iot_data_t *bacnet_protocol_properties = iot_data_alloc_map (IOT_DATA_STRING);
    iot_data_t *service_protocol_properties = iot_data_alloc_map (IOT_DATA_STRING);

    bacnet_protocol_populate (discovered_device, bacnet_protocol_properties, driver);

    /* Get device information */
    uint16_t port = (uint16_t) (discovered_device->address.mac[4] * 0x100u +
                                discovered_device->address.mac[5]);
    if (!get_device_properties (discovered_device, port, driver->lc, &name,
                                &description, labels, &profile))
    {
      free(labels);
      iot_data_free (bacnet_protocol_properties);
      continue;
    }
    if (!get_supported_services (discovered_device->device_id, port, service_protocol_properties))
    {
      free(name);
      free (description);

      free(labels);
      iot_data_free (bacnet_protocol_properties);
      continue;
    }

    devsdk_protocols *protocols = devsdk_protocols_new ("BACnetSupportedServices", service_protocol_properties, NULL);
#ifdef BACDL_MSTP
    protocols = devsdk_protocols_new ("BACnet-MSTP", bacnet_protocol_properties, protocols);
#else
    protocols = devsdk_protocols_new ("BACnet-IP", bacnet_protocol_properties, protocols);
#endif

    /* Setup EdgeX error variable, for the EdgeX error handler to use */
    devsdk_error error;
    error.code = 0;
    /* Add the device to EdgeX */
    edgex_add_device (driver->service, name, description, labels, profile, protocols, false, NULL, &error);
    if (error.code) {
      iot_log_error (driver->lc, "Error: %d: %s\n", error.code, error.reason);
    }
    /* Clean up memory */
    free (name);
    free (profile);
    free (description);
    free (labels);
    iot_data_free (service_protocol_properties);
    iot_data_free (bacnet_protocol_properties);
    devsdk_protocols_free (protocols);
    free(discovered_device);
  }
  iot_log_debug (driver->lc, "Finished BACnet Discovery");
}

/* ---- Get ---- */
/* Get triggers an asynchronous protocol specific GET operation.
 * The device to query is specified by the protocols. nreadings is
 * the number of values being requested and defines the size of the requests
 * and readings arrays. For each value, the commandrequest holds information
 * as to what is being requested. The implementation of this method should
 * query the device accordingly and write the resulting value into the
 * commandresult.
*/
static bool bacnet_get_handler
  (
    void *impl,
    const devsdk_device_t *device,
    uint32_t nreadings,
    const devsdk_commandrequest *requests,
    devsdk_commandresult *readings,
    const iot_data_t *options,
    iot_data_t **exception
  )
{
  bacnet_driver *driver = (bacnet_driver *) impl;
  bool ret_val = true;
  /* Log the name of the device */
  iot_log_debug (driver->lc, "GET on device: %s", device->name);
  /* Results */
  BACNET_APPLICATION_DATA_VALUE *read_results = NULL;
  /* Pointer to the data to be read */
  BACNET_READ_ACCESS_DATA *read_data = NULL;
  bacnet_address_t *addr = (bacnet_address_t *)device->address;
  bool success = read_access_data_populate (&read_data, nreadings, requests, driver);
  /* Return false if read_data could not be set up */
  if (!success)
  {
    iot_log_error (driver->lc, "Error populating read_data");
    *exception = iot_data_alloc_string ("Error populating read_data", IOT_DATA_REF);
    return false;
  }

  for (BACNET_READ_ACCESS_DATA *current_data = read_data; current_data; current_data = current_data->next)
  {

    BACNET_APPLICATION_DATA_VALUE *result = bacnetReadProperty (addr->deviceInstance,
                                                                current_data->object_type,
                                                                current_data->object_instance,
                                                                current_data->listOfProperties->propertyIdentifier,
                                                                current_data->listOfProperties->propertyArrayIndex,
                                                                addr->port);
    if (result)
    {
      read_results = bacnet_read_application_data_value_add (read_results,
                                                             result);
    }
    else
    {
      print_read_error (driver->lc, current_data);
      *exception = iot_data_alloc_string ("Error reading data", IOT_DATA_REF);
      ret_val = false;
      break;
    }
  }

  read_access_data_free (read_data);

  devsdk_commandresult_populate (readings, read_results, nreadings);

  return ret_val;
}

/* ---- Put ---- */
/* Put triggers an asynchronous protocol specific SET operation.
 * The device to set values on is specified by the protocols.
 * nvalues is the number of values to be set and defines the size of the
 * requests and values arrays. For each value, the commandresult holds the
 * value, and the commandrequest holds information as to where it is to be
 * written. The implementation of this method should effect the write to the
 * device.
*/
static bool bacnet_put_handler
  (
    void *impl,
    const devsdk_device_t *device,
    uint32_t nvalues,
    const devsdk_commandrequest *requests,
    const iot_data_t *values[],
    const iot_data_t *options,
    iot_data_t **exception
  )
{
  bacnet_driver *driver = (bacnet_driver *) impl;

  /* Log the name of the device */
  iot_log_debug (driver->lc, "PUT on device: %s", device->name);

  bacnet_address_t *addr = (bacnet_address_t *)device->address;
  int error = 0;
  /* Create pointer for the read_data structure */
  BACNET_WRITE_ACCESS_DATA *write_data = NULL;
  bool success;
  success = write_access_data_populate (&write_data, nvalues, requests,
                                           values, driver);
  /* Return false if write_data could not be set up */
  if (!success)
  {
    iot_log_error (driver->lc, "Error populating write_data");
    *exception = iot_data_alloc_string ("Error populating write_data", IOT_DATA_REF);
    return false;
  }
  /* Call the BACnet write property function */
  for (BACNET_WRITE_ACCESS_DATA *current_data = write_data; current_data; current_data = current_data->next)
  {

    error = bacnetWriteProperty (addr->deviceInstance,
                                 current_data->object_type,
                                 current_data->object_instance,
                                 current_data->listOfProperties->propertyIdentifier,
                                 current_data->listOfProperties->propertyArrayIndex,
                                 addr->port,
                                 current_data->listOfProperties->priority,
                                 &current_data->listOfProperties->value);
    if (error)
    {
      break;
    }
  }

  write_access_data_free (write_data);

  if (error != 0)
  {
    *exception = iot_data_alloc_string ("Error writing property", IOT_DATA_REF);
    return false;
  }

  return true;
}

/* ---- Stop ---- */
/* Stop performs any final actions before the device service is terminated */
/* Frees the address instance mapping linked list setup on device service
 * initialisation
 */
static void bacnet_stop (void *impl, bool force)
{
  bacnet_driver *driver = (bacnet_driver *) impl;

  address_instance_map_free (driver->aim_ll);

  deinit_bacnet_driver (&driver->datalink_thread, &driver->running_thread);

}

int main (int argc, char *argv[])
{
  sigset_t set;
  int sigret;
  iot_data_t *defaults = NULL;

  bacnet_driver *impl = malloc (sizeof (bacnet_driver));
  memset (impl, 0, sizeof (bacnet_driver));

  /* Set the stack for subsequently created threads to double the default */
  pthread_attr_t dflt;
  size_t ssize;

  int error = 0;
  error = pthread_attr_init (&dflt);
  if (error)
  {
    printf("Error initializing thread attribute\n");
    free (impl);
    return 1;
  }
  error = pthread_attr_getstacksize (&dflt, &ssize);
  if (error)
  {
    printf("Error getting stack size attribute");
    pthread_attr_destroy (&dflt);
    free (impl);
    return 1;
  }
  /* Double the current stack size if it too small, e.g. we are running
   * on Alpine Linux*/
  if (ssize < SMALL_STACK)
  {
    ssize *= 2;
    error = pthread_attr_setstacksize (&dflt, ssize);
    if (error)
    {
      printf("Error setting stack size attribute");
      pthread_attr_destroy (&dflt);
      free (impl);
      return 1;
    }
    error = pthread_setattr_default_np (&dflt);
    if (error)
    {
      printf("Error setting stack size attribute");
      pthread_attr_destroy (&dflt);
      free (impl);
      return 1;
    }
  }

  devsdk_error e;
  e.code = 0;

  /* Device Callbacks */
  devsdk_callbacks *bacnetImpls = devsdk_callbacks_init
  (
    bacnet_init,
    bacnet_get_handler,
    bacnet_put_handler,
    bacnet_stop,
    bacnet_getaddress,
    bacnet_freeaddress,
    bacnet_getattributes,
    bacnet_freeattributes
  );

  devsdk_callbacks_set_discovery (bacnetImpls, bacnet_discover, NULL);

  /* Initalise a new device service */
  impl->service = devsdk_service_new
    (
#ifdef BACDL_MSTP
      "device-bacnet-mstp",
#else
      "device-bacnet-ip",
#endif
      VERSION,
      impl,
      bacnetImpls,
      &argc,
      argv,
      &e
    );
  ERR_CHECK (e);

  int n = 1;
  while (n < argc)
  {
    if (strcmp (argv[n], "-h") == 0 || strcmp (argv[n], "--help") == 0)
    {
      printf ("Options:\n");
      printf ("  -h, --help\t\t\tShow this text\n");
      devsdk_usage ();
      goto exit;
    }
    else
    {
      printf ("%s: Unrecognized option %s\n", argv[0], argv[n]);
      goto exit;
    }
  }

  /* Setup default configuration */

  defaults = iot_data_alloc_map (IOT_DATA_STRING);
  iot_data_string_map_add (defaults, "BBMD_ADDRESS", iot_data_alloc_string ("", IOT_DATA_REF));
  iot_data_string_map_add (defaults, "BBMD_PORT", iot_data_alloc_string ("", IOT_DATA_REF));
  iot_data_string_map_add (defaults, "DefaultDevicePath", iot_data_alloc_string (DEFAULT_MSTP_PATH, IOT_DATA_REF));

  /* Start the device service*/
  devsdk_service_start (impl->service, defaults, &e);
  ERR_CHECK (e);

  /* Wait for interrupt */
  sigemptyset (&set);
  sigaddset (&set, SIGINT);
  sigwait (&set, &sigret);

  /* Stop the device service */
  devsdk_service_stop (impl->service, true, &e);
  ERR_CHECK (e);

  exit:
  iot_data_free (defaults);
  devsdk_service_free (impl->service);

  free (bacnetImpls);
  free (impl);
  return 0;
}
