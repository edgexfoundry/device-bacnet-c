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
#include <edgex/devsdk.h>
#include "iot/logger.h"
#include "address_instance_map.h"

typedef struct bacnet_driver
{
  iot_logger_t *lc;
  edgex_device_service *service;
  address_instance_map_ll *aim_ll;
  pthread_t datalink_thread;
  bool running_thread;
  char *default_device_path;
} bacnet_driver;

typedef struct stringValueMap
{
  const char *string;
  const uint32_t type;
} stringValueMap;

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

uint16_t parseType (char *type);

uint32_t parseProperty (char *property);

uint32_t parseIndex (char *indexString);

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

void get_protocol_properties (const edgex_protocols *protocols,
                              bacnet_driver *driver, uint16_t *port,
                              uint32_t *deviceInstance);

bool
read_access_data_populate (BACNET_READ_ACCESS_DATA **head, uint32_t nreadings,
                           const edgex_device_commandrequest *requests,
                           bacnet_driver *driver);

void
get_attributes (const edgex_device_commandrequest request, uint32_t *instance,
                BACNET_PROPERTY_ID *property, BACNET_OBJECT_TYPE *type,
                uint32_t *index);

void read_access_data_free (BACNET_READ_ACCESS_DATA *head);

void edgex_device_commandresult_populate (edgex_device_commandresult *readings,
                                          BACNET_APPLICATION_DATA_VALUE *read_results,
                                          uint32_t nreadings, iot_logger_t *lc);

bool
write_access_data_populate (BACNET_WRITE_ACCESS_DATA **head, uint32_t nvalues,
                            const edgex_device_commandrequest *requests,
                            const edgex_device_commandresult *values,
                            bacnet_driver *driver);

void write_access_data_free (BACNET_WRITE_ACCESS_DATA *head);

bool
get_device_properties (address_entry_t *device, uint16_t port, iot_logger_t *lc,
                       char **name, char **description, edgex_strings *labels,
                       char **profile_name);

void
bacnet_protocol_populate (address_entry_t *device, edgex_protocols *protocol,
                          bacnet_driver *driver);

void bacnet_protocols_free (edgex_protocols *head);

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