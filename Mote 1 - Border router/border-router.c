/*
 * Border router + coap/rest/push server
 */

#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"
#include "net/uip-ds6.h"
#include "net/rpl/rpl.h"

#include "net/netstack.h"
#include "dev/button-sensor.h"
#include "dev/tmp102.h"
#include "dev/slip.h"
#include "dev/leds.h"

#include "rest-engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define DEBUG 0 //DEBUG_NONE
#include "net/uip-debug.h"


#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINT6ADDR(addr) PRINTF("%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x", ((uint8_t *)addr)[0], ((uint8_t *)addr)[1], ((uint8_t *)addr)[2], ((uint8_t *)addr)[3], ((uint8_t *)addr)[4], ((uint8_t *)addr)[5], ((uint8_t *)addr)[6], ((uint8_t *)addr)[7], ((uint8_t *)addr)[8], ((uint8_t *)addr)[9], ((uint8_t *)addr)[10], ((uint8_t *)addr)[11], ((uint8_t *)addr)[12], ((uint8_t *)addr)[13], ((uint8_t *)addr)[14], ((uint8_t *)addr)[15])
#else
#define PRINTF(...)
#define PRINT6ADDR(addr)
#endif

#define DELTA_USB_TEMP 2
#define DELTA_FIX_TEMP 3.76
static uip_ipaddr_t prefix;
static uint8_t prefix_set;



/* Declaration of our processes */

PROCESS(border_router_process, "Border router process");
PROCESS(coap_rest_push_server, "CoAP Rest PUSH Server");
AUTOSTART_PROCESSES(&border_router_process, &coap_rest_push_server);


/////////////////////////////////////////////////
//                BORDER ROUTER                //
/////////////////////////////////////////////////

// Note: mostly inspired by border-router-example

void
request_prefix(void)
{
  /* mess up uip_buf with a dirty request... */
  uip_buf[0] = '?';
  uip_buf[1] = 'P';
  uip_len = 2;
  slip_send();
  uip_len = 0;
}

void
set_prefix_64(uip_ipaddr_t *prefix_64)
{
  rpl_dag_t *dag;
  uip_ipaddr_t ipaddr;
  memcpy(&prefix, prefix_64, 16);
  memcpy(&ipaddr, prefix_64, 16);
  prefix_set = 1;
  uip_ds6_set_addr_iid(&ipaddr, &uip_lladdr);
  uip_ds6_addr_add(&ipaddr, 0, ADDR_AUTOCONF);

  dag = rpl_set_root(RPL_DEFAULT_INSTANCE, &ipaddr);
  if(dag != NULL) {
    rpl_set_prefix(dag, &prefix, 64);
    PRINTF("created a new RPL dag\n");
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(border_router_process, ev, data)
{
  static struct etimer et;

  PROCESS_BEGIN();

/* While waiting for the prefix to be sent through the SLIP connection, the future
 * border router can join an existing DAG as a parent or child, or acquire a default
 * router that will later take precedence over the SLIP fallback interface.
 * Prevent that by turning the radio off until we are initialized as a DAG root.
 */
  prefix_set = 0;
  NETSTACK_MAC.off(0);

  PROCESS_PAUSE();

  SENSORS_ACTIVATE(button_sensor);

  PRINTF("RPL-Border router started\n");

  /* Request prefix until it has been received */
  while(!prefix_set) {
    etimer_set(&et, CLOCK_SECOND);
    request_prefix();
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
  }

  /* Now turn the radio on, but disable radio duty cycling.
   * Since we are the DAG root, reception delays would constrain mesh throughbut.
   */
  NETSTACK_MAC.off(1);

  while(1) {
    PROCESS_YIELD();
    if (ev == sensors_event && data == &button_sensor) {
      PRINTF("Initiating global repair\n");
      rpl_repair_root(RPL_DEFAULT_INSTANCE);
    }
  }

  PROCESS_END();
}




/////////////////////////////////////////////////
//                  CoAP REST                  //
/////////////////////////////////////////////////

static void temperature_handler(void* request, void* response, uint8_t *buffer,
                                uint16_t preferred_size, int32_t *offset);
static void temperature_periodic_handler();
/*
 * Periodic resource: each 5 secondes, the temperature is get and sent via a
 * REST request.
 */
PERIODIC_RESOURCE(res_temperature,
   "title=\"temp\";obs",
   temperature_handler,
   NULL,
   NULL,
   NULL,
   5 * CLOCK_SECOND,
   temperature_periodic_handler);

/*
 * Prepare a REST answer with temperature and time
 */
static void
temperature_handler(void* request, void* response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  /* Header: JSON + max age */
  REST.set_header_content_type(response, REST.type.APPLICATION_JSON);
  REST.set_header_max_age(response, res_temperature.periodic->period / CLOCK_SECOND);

  /* Content: temparature + timestamp */
  int8_t temparature = (int8_t) (tmp102_read_temp_x100() / 100  - DELTA_USB_TEMP - DELTA_FIX_TEMP);
  unsigned long timestamp = clock_seconds();
  int size = snprintf((char *)buffer, preferred_size,
                      "{ \"temperature\":%d, \"time\":%lu }", temparature, timestamp);

  /* Payload */
  REST.set_response_payload(response, buffer, size);
}

/*
 * Called by the REST manager process: send data
 */
static void
temperature_periodic_handler()
{
  REST.notify_subscribers(&res_temperature);
}


PROCESS_THREAD(coap_rest_push_server, ev, data)
{
  PROCESS_BEGIN();

  PRINTF("COAP REST Push Server\n");

  /* Initialize temperature sensor. */
  tmp102_init();

  /* Initialize our REST engine. */
  rest_init_engine();

  /* Activate resources: just temperature */
  rest_activate_resource(&res_temperature, "temperature/push");

  PROCESS_END();
}
