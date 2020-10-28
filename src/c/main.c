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
#include "rs485.h"
#include "math.h"
#include "driver.h"
#include "address_instance_map.h"

#define ERR_CHECK(x) if (x.code) { fprintf (stderr, "Error: %d: %s\n", x.code, x.reason); return x.code; }
#define SMALL_STACK 100000

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

static void bacnet_reconfigure (void *impl, const iot_data_t *config)
{
  bacnet_driver *driver = (bacnet_driver *) impl;
  iot_log_error (driver->lc, "BACnet [Driver] configuration cannot be updated while running. Please restart the service.");
}

static bool get_supported_services (uint32_t device_id, uint16_t port,
                             devsdk_nvpairs **properties)
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
    *properties = devsdk_nvpairs_new ("DS-RPM-B", "true", *properties);
  }
  if (bitstring_bit (&bacnet_services->type.Bit_String,
                     SERVICE_SUPPORTED_WRITE_PROPERTY))
  {
    *properties = devsdk_nvpairs_new ("DS-WP-B", "true", *properties);
  }
  if (bitstring_bit (&bacnet_services->type.Bit_String,
                     SERVICE_SUPPORTED_WRITE_PROP_MULTIPLE))
  {
    *properties = devsdk_nvpairs_new ("DS-WPM-B", "true", *properties);
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
    devsdk_nvpairs *bacnet_protocol_properties = NULL;
    devsdk_nvpairs *service_protocol_properties = NULL;

    bacnet_protocol_populate (discovered_device, &bacnet_protocol_properties, driver);

    /* Get device information */
    uint16_t port = (uint16_t) (discovered_device->address.mac[4] * 0x100u +
                                discovered_device->address.mac[5]);
    if (!get_device_properties (discovered_device, port, driver->lc, &name,
                                &description, labels, &profile))
    {
      free(labels);
      devsdk_nvpairs_free (bacnet_protocol_properties);
      continue;
    }
    if (!get_supported_services (discovered_device->device_id, port, &service_protocol_properties))
    {
      free(name);
      free (description);

      free(labels);
      devsdk_nvpairs_free (bacnet_protocol_properties);
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
    char *result = edgex_add_device (driver->service, name,
                                            description,
                                            labels,
                                            profile, protocols, false, NULL,
                                            &error);
    if (error.code) {
      iot_log_error (driver->lc, "Error: %d: %s\n", error.code, error.reason);
    }
    /* Clean up memory */
    free (result);
    free (name);
    free (profile);
    free (description);
    free (labels);
    devsdk_nvpairs_free (service_protocol_properties);
    devsdk_nvpairs_free (bacnet_protocol_properties);
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
    const char *devname,
    const devsdk_protocols *protocols,
    uint32_t nreadings,
    const devsdk_commandrequest *requests,
    devsdk_commandresult *readings,
    const devsdk_nvpairs *qparams,
    iot_data_t **exception
  )
{
  bacnet_driver *driver = (bacnet_driver *) impl;
  bool ret_val = true;
  /* Log the name of the device */
  iot_log_debug (driver->lc, "GET on device: %s", devname);
  /* Results */
  BACNET_APPLICATION_DATA_VALUE *read_results = NULL;
  /* Pointer to the data to be read */
  BACNET_READ_ACCESS_DATA *read_data = NULL;
  uint32_t deviceInstance = UINT32_MAX;
  uint16_t port = 0xBAC0;
  get_protocol_properties (protocols, driver, &port, &deviceInstance);
  if (deviceInstance == UINT32_MAX)
  {
    iot_log_error (driver->lc, "Error getting protocol values");
    *exception = iot_data_alloc_string ("Error getting protocol values", IOT_DATA_REF);
    return false;
  }
  bool success;
  success = read_access_data_populate (&read_data, nreadings, requests, driver);
  /* Return false if read_data could not be set up */
  if (!success)
  {
    iot_log_error (driver->lc, "Error populating read_data");
    *exception = iot_data_alloc_string ("Error populating read_data", IOT_DATA_REF);
    return false;
  }

  for (BACNET_READ_ACCESS_DATA *current_data = read_data; current_data; current_data = current_data->next)
  {

    BACNET_APPLICATION_DATA_VALUE *result = bacnetReadProperty (deviceInstance,
                                                                current_data->object_type,
                                                                current_data->object_instance,
                                                                current_data->listOfProperties->propertyIdentifier,
                                                                current_data->listOfProperties->propertyArrayIndex,
                                                                port);
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
    const char *devname,
    const devsdk_protocols *protocols,
    uint32_t nvalues,
    const devsdk_commandrequest *requests,
    const iot_data_t *values[],
    iot_data_t **exception
  )
{
  bacnet_driver *driver = (bacnet_driver *) impl;

  /* Log the name of the device */
  iot_log_debug (driver->lc, "PUT on device: %s", devname);

  uint32_t deviceInstance = UINT32_MAX;
  uint16_t port = 0xBAC0;
  int error = 0;
  get_protocol_properties (protocols, driver, &port, &deviceInstance);
  if (deviceInstance == UINT32_MAX)
  {
    iot_log_error (driver->lc, "Error getting protocol values");
    *exception = iot_data_alloc_string ("Error getting protocol values", IOT_DATA_REF);
    return false;
  }
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

    error = bacnetWriteProperty (deviceInstance,
                                 current_data->object_type,
                                 current_data->object_instance,
                                 current_data->listOfProperties->propertyIdentifier,
                                 current_data->listOfProperties->propertyArrayIndex,
                                 port,
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
  devsdk_callbacks bacnetImpls =
    {
      bacnet_init,         /* Initialize */
      bacnet_reconfigure,  /* Reconfigure */
      bacnet_discover,     /* Discovery */
      bacnet_get_handler,  /* Get */
      bacnet_put_handler,  /* Put */
      bacnet_stop          /* Stop */
    };

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

  free (impl);
  return 0;
}
