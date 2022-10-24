/*
 * Copyright (c) 2019
 * IoTech Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <apdu.h>
#include "address_entry.h"
#include "return_data.h"
#include <rpm.h>
#include <wpm.h>
#include <devsdk/devsdk.h>
#include <edgex/edgex-base.h>
#include "iot/logger.h"
#include "address_instance_map.h"

typedef struct bacnet_driver
{
  iot_logger_t *lc;
  devsdk_service_t *service;
  address_instance_map_ll *aim_ll;
  pthread_t datalink_thread;
  bool running_thread;
  const char *default_device_path;
} bacnet_driver;

typedef struct
{
  uint32_t instance;
  BACNET_PROPERTY_ID property;
  BACNET_OBJECT_TYPE type;
  uint32_t index;
} bacnet_attributes_t;

int bacnetWriteProperty (
  uint32_t deviceInstance, int type, uint32_t instance, int property,
  uint32_t index, uint16_t port, uint8_t priority,
  BACNET_APPLICATION_DATA_VALUE *value);

address_entry_ll *bacnetWhoIs (void);

BACNET_APPLICATION_DATA_VALUE *bacnetReadProperty (
  uint32_t deviceInstance, int type, uint32_t instance, int property,
  uint32_t index, uint16_t port);

int init_bacnet_driver (pthread_t *datalink_thread, bool *running,
                        iot_logger_t *logging_client);

void deinit_bacnet_driver (pthread_t *datalink_thread, bool *running);

BACNET_APPLICATION_DATA_VALUE *
bacnet_read_application_data_value_add (BACNET_APPLICATION_DATA_VALUE *head,
                                        BACNET_APPLICATION_DATA_VALUE *result);

bool
find_and_bind (return_data_t *data, uint16_t port, uint32_t deviceInstance);

bool wait_for_data (return_data_t *data);

uint32_t ip_to_instance (bacnet_driver *driver, char *deviceInstance);

BACNET_READ_ACCESS_DATA *
bacnet_read_access_data_add (BACNET_READ_ACCESS_DATA *head,
                             BACNET_OBJECT_TYPE type,
                             BACNET_PROPERTY_ID property, uint32_t instance,
                             uint32_t index);

BACNET_WRITE_ACCESS_DATA *
bacnet_write_access_data_add (BACNET_WRITE_ACCESS_DATA *head,
                              BACNET_OBJECT_TYPE type,
                              BACNET_PROPERTY_ID property, uint32_t instance,
                              uint32_t index,
                              BACNET_APPLICATION_DATA_VALUE value,
                              uint8_t priority);

void get_protocol_properties (const devsdk_protocols *protocols,
                              bacnet_driver *driver, uint16_t *port,
                              uint32_t *deviceInstance);

bool
read_access_data_populate (BACNET_READ_ACCESS_DATA **head, uint32_t nreadings,
                           const devsdk_commandrequest *requests,
                           bacnet_driver *driver);

void read_access_data_free (BACNET_READ_ACCESS_DATA *head);

void devsdk_commandresult_populate (devsdk_commandresult *readings,
                                          BACNET_APPLICATION_DATA_VALUE *read_results,
                                          uint32_t nreadings);

bool
write_access_data_populate (BACNET_WRITE_ACCESS_DATA **head, uint32_t nvalues,
                            const devsdk_commandrequest *requests,
                            const iot_data_t *values[],
                            bacnet_driver *driver);

void write_access_data_free (BACNET_WRITE_ACCESS_DATA *head);

bool
get_device_properties (address_entry_t *device, uint16_t port, iot_logger_t *lc,
                       char **name, char **description, devsdk_strings *labels,
                       char **profile_name);

void bacnet_protocol_populate (address_entry_t *device, iot_data_t *properties, bacnet_driver *driver);

void print_read_error(iot_logger_t *lc, BACNET_READ_ACCESS_DATA *data);

#ifndef MAX_PROPERTY_VALUES
#define MAX_PROPERTY_VALUES 64
#endif

#define IP_STRING_LENGTH 17
#define DISCOVERY_DESCRIPTION "automatically discovered using EdgeX discovery"
#define BACNET_MAX_INSTANCE_LENGTH 11
#define MAX_PORT_LENGTH 6
#define DEFAULT_MSTP_PATH "/dev/ttyUSB0"

extern return_data_ll *returnDataHead;
