/* BACnet implementation of an Edgex device service using C SDK */

/*
 * Copyright (c) 2019
 * IoTech Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "edgex/devsdk.h"
#include "edgex/device-mgmt.h"

#include <unistd.h>
#include <signal.h>
#include <bacapp.h>
#include <device.h>
#include <bacnet.h>
#include <dlenv.h>
#include <regex.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include "rs485.h"
#include "math.h"
#include "driver.h"
#include <semaphore.h>
#include "address_instance_map.h"

#define ERR_CHECK(x) if (x.code) { fprintf (stderr, "Error: %d: %s\n", x.code, x.reason); return x.code; }
#define SMALL_STACK 100000

static sem_t template_sem;

static void inthandler (int i)
{
  sem_post (&template_sem);
}

/* --- Initialize ---- */
/* Initialize performs protocol-specific initialization for the device
 * service.
 */
static bool bacnet_init
  (
    void *impl,
    struct iot_logger_t *lc,
    const edgex_nvpairs *config
  )
{
  bacnet_driver *driver = (bacnet_driver *) impl;
  driver->lc = lc;
  driver->aim_ll = address_instance_map_alloc ();
  driver->running_thread = true;
#ifdef BACDL_MSTP
  driver->default_device_path = NULL;
#endif
  /* Get the default device path from the TOML configuration file */
  for (const edgex_nvpairs *p = config; p; p = p->next)
  {
#ifdef BACDL_MSTP
    if (strcmp (p->name, "DefaultDevicePath") == 0)
    {
      driver->default_device_path = strdup (p->value);
    }
#else
    if (strcmp (p->name, "BBMD_ADDRESS") == 0)
    {
      setenv("BACNET_BBMD_ADDRESS", p->value, 0);
    }
    else if (strcmp (p->name, "BBMD_PORT") == 0)
    {
      setenv("BACNET_BBMD_PORT", p->value, 0);
    }
#endif
  }
#ifdef BACDL_MSTP
  if (driver->default_device_path == NULL) {
    driver->default_device_path = strdup (DEFAULT_MSTP_PATH);
  }
  /* Set the environment variable used by the BACnet stack to initialize the interface */
  setenv ("BACNET_IFACE", driver->default_device_path, 1);
#endif
  if (init_bacnet_driver (&driver->datalink_thread, &driver->running_thread,
                          lc) !=
      0)
  {
    iot_log_error (driver->lc,
                   "An error occurred while initializing the BACnet driver");
    deinit_bacnet_driver (&driver->datalink_thread, &driver->running_thread);
    return false;
  }
  iot_log_debug (driver->lc, "Init");
  return true;
}

/* ---- Discovery ---- */
/* Device services which are capable of device discovery should implement it
 * in this callback. It is called in response to a request on the
 * device service's discovery REST   endpoint. New devices should be added using
 * the edgex_device_add_device() method
 */
static void bacnet_discover (void *impl)
{
  bacnet_driver *driver = (bacnet_driver *) impl;

  iot_log_debug (driver->lc, "Running BACnet Discovery");
#ifdef BACDL_MSTP
  /* Set default interface */
  RS485_Set_Interface (driver->default_device_path);
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
    edgex_strings *labels = malloc (sizeof (edgex_strings));
    memset (labels, 0, sizeof (edgex_strings));
    /* Set up protocol */
    edgex_protocols *protocols = malloc (sizeof (edgex_protocols));
    memset (protocols, 0, sizeof (edgex_protocols));

    bacnet_protocol_populate (discovered_device, protocols, driver);

    /* Get device information */
    uint16_t port = (uint16_t) (discovered_device->address.mac[4] * 0x100u +
                                discovered_device->address.mac[5]);
    if (!get_device_properties (discovered_device, port, driver->lc, &name,
                                &description, labels, &profile))
    {
      free(labels);
      bacnet_protocols_free (protocols);
      /* Go to next device */
      address_entry_t *tmp = discovered_device->next;
      address_entry_remove (driver->lc, discover_table, discovered_device->device_id);
      discovered_device = tmp;
      continue;
    }

    /* Setup EdgeX error variable, for the EdgeX error handler to use */
    edgex_error error;
    error.code = 0;
    /* Add the device to EdgeX */
    char *result = edgex_device_add_device (driver->service, name,
                                            description,
                                            labels,
                                            profile, protocols, NULL,
                                            &error);
    if (error.code) {
      iot_log_error (driver->lc, "Error: %d: %s\n", error.code, error.reason);
    }
    /* Clean up memory */
    bacnet_protocols_free (protocols);
    free (result);
    free (name);
    free (profile);
    free (description);
    free (labels);
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
    const edgex_protocols *protocols,
    uint32_t nreadings,
    const edgex_device_commandrequest *requests,
    edgex_device_commandresult *readings
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
  uint16_t port = UINT16_MAX;
  get_protocol_properties (protocols, driver, &port, &deviceInstance);
  if (deviceInstance == UINT32_MAX || port == UINT16_MAX)
  {
    iot_log_error (driver->lc, "Error getting protocol values");
    return false;
  }
  bool success;
  success = read_access_data_populate (&read_data, nreadings, requests, driver);
  /* Return false if read_data could not be set up */
  if (!success)
    return false;

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
      ret_val = false;
      break;
    }
  }

  read_access_data_free (read_data);

  edgex_device_commandresult_populate (readings, read_results, nreadings,
                                       driver->lc);

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
    const edgex_protocols *protocols,
    uint32_t nvalues,
    const edgex_device_commandrequest *requests,
    const edgex_device_commandresult *values
  )
{
  bacnet_driver *driver = (bacnet_driver *) impl;

  /* Log the name of the device */
  iot_log_debug (driver->lc, "PUT on device: %s", devname);

  uint32_t deviceInstance = UINT32_MAX;
  uint16_t port = UINT16_MAX;
  int error = 0;
  get_protocol_properties (protocols, driver, &port, &deviceInstance);
  if (deviceInstance == UINT32_MAX || port == UINT16_MAX)
  {
    iot_log_error (driver->lc, "Error getting protocol values");
    return false;
  }
  /* Create pointer for the read_data structure */
  BACNET_WRITE_ACCESS_DATA *write_data = NULL;
  bool success;
  success = write_access_data_populate (&write_data, nvalues, requests,
                                           values, driver);
  /* Return false if write_data could not be set up */
  if (!success)
    return false;
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
    return false;
  }

  return true;
}

/* ---- Disconnect ---- */
/* Disconnect handles protocol-specific cleanup when a device is removed. */
static bool bacnet_disconnect (void *impl, edgex_protocols *device)
{
#if 0
  bacnet_driver *driver = (bacnet_driver *) impl;
  struct sockaddr_in sa;

  /* If deviceInstance argument is an IP address */
  if (inet_pton (AF_INET, device->address, &sa.sin_addr))
  {
    address_instance_map *current = driver->ai_map_ll->first;
    while (current)
    {
      if (strcmp (current->address, device->address) == 0)
      {
        free (current->address);
        free (current->instance);
        free (current);
      }
      current = current->next;
    }
  }
#endif
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

#ifdef BACDL_MSTP
  free (driver->default_device_path);
#endif
}


static void usage (void)
{
  printf ("Options: \n");
  printf ("   -h, --help                 : Show this text\n");
  printf ("   -n, --name <name>          : Set the device service name\n");
  printf ("   -r [url], --registry [url] : Use the registry service\n");
  printf ("   -p, --profile <name>       : Set the profile name\n");
  printf ("   -c, --confdir <dir>        : Set the configuration directory\n");
}

static bool testArg (int argc, char *argv[], int *pos, const char *pshort, const char *plong, char **var)
{
  if (strcmp (argv[*pos], pshort) == 0 || strcmp (argv[*pos], plong) == 0)
  {
    if (*pos < argc - 1)
    {
      (*pos)++;
      *var = argv[*pos];
      (*pos)++;
      return true;
    }
    else
    {
      printf ("Option %s requires an argument\n", argv[*pos]);
      exit (0);
    }
  }
  char *eq = strchr (argv[*pos], '=');
  if (eq)
  {
    if (strncmp (argv[*pos], pshort, eq - argv[*pos]) == 0 || strncmp (argv[*pos], plong, eq - argv[*pos]) == 0)
    {
      if (strlen (++eq))
      {
        *var = eq;
        (*pos)++;
        return true;
      }
      else
      {
        printf ("Option %s requires an argument\n", argv[*pos]);
        exit (0);
      }
    }
  }
  return false;
}

int main (int argc, char *argv[])
{

  char *profile = "";
#ifdef BACDL_BIP
  char *confdir = "res/ip";
#else
  char *confdir = "res/mstp";
#endif
  char *svcname = "device-bacnet";
  char *regURL = getenv ("EDGEX_REGISTRY");

  int n = 1;
  while (n < argc)
  {
    if (strcmp (argv[n], "-h") == 0 || strcmp (argv[n], "--help") == 0)
    {
      usage ();
      return 0;
    }
    if (testArg (argc, argv, &n, "-r", "--registry", &regURL))
    {
      continue;
    }
    if (testArg (argc, argv, &n, "-n", "--name", &svcname))
    {
      continue;
    }
    if (testArg (argc, argv, &n, "-p", "--profile", &profile))
    {
      continue;
    }
    if (testArg (argc, argv, &n, "-c", "--confdir", &confdir))
    {
      continue;
    }
    printf ("Unknown option %s\n", argv[n]);
    usage ();
    return 0;
  }

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

  edgex_error e;
  e.code = 0;

  /* Device Callbacks */
  edgex_device_callbacks bacnetImpls =
    {
      bacnet_init,         /* Initialize */
      bacnet_discover,     /* Discovery */
      bacnet_get_handler,  /* Get */
      bacnet_put_handler,  /* Put */
      bacnet_disconnect,   /* Disconnect */
      bacnet_stop          /* Stop */
    };

  /* Initalise a new device service */
  impl->service = edgex_device_service_new
    (
      svcname,
      VERSION,
      impl,
      bacnetImpls,
      &e
    );
  ERR_CHECK (e);
  /* Start the device service*/
  edgex_device_service_start (impl->service, regURL, profile, confdir, &e);
  ERR_CHECK (e);

  signal (SIGINT, inthandler);
  sem_wait (&template_sem);

  /* Stop the device service */
  edgex_device_service_stop (impl->service, true, &e);
  ERR_CHECK (e);
  edgex_device_service_free (impl->service);

  free (impl);
  sem_destroy (&template_sem);
  return 0;
}
