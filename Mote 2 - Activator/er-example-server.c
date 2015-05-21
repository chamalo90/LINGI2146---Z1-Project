/*
 * Copyright (c) 2013, Matthias Kovatsch
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 */

/**
 * \file
 *      Erbium (Er) REST Engine example (with CoAP-specific code)
 * \author
 *      Matthias Kovatsch <kovatsch@inf.ethz.ch>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "contiki.h"
#include "contiki-net.h"
#include "rest-engine.h"
#include "er-coap-engine.h"
#include "dev/cc2420.h"
#include "dev/cc2420_const.h"


/* Define which resources to include to meet memory constraints. */
#define REST_RES_LEDS 1
#define REST_RES_TOGGLE 1
#define REST_RES_BATTERY 1
#define REST_RES_PUSHING 1
#define REST_RES_RADIO 1 /* causes largest code size */


#if defined (PLATFORM_HAS_BUTTON)
#include "dev/button-sensor.h"
#endif
#if defined (PLATFORM_HAS_LEDS)
#include "dev/leds.h"
#endif
#if defined (PLATFORM_HAS_LIGHT)
#include "dev/light-sensor.h"
#endif
#if defined (PLATFORM_HAS_BATTERY)
#include "dev/battery-sensor.h"
#endif
#if defined (PLATFORM_HAS_SHT11)
#include "dev/sht11-sensor.h"
#endif
#include "dev/radio-sensor.h"


#define DEBUG 0
#if DEBUG
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINT6ADDR(addr) PRINTF("[%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x]", ((uint8_t *)addr)[0], ((uint8_t *)addr)[1], ((uint8_t *)addr)[2], ((uint8_t *)addr)[3], ((uint8_t *)addr)[4], ((uint8_t *)addr)[5], ((uint8_t *)addr)[6], ((uint8_t *)addr)[7], ((uint8_t *)addr)[8], ((uint8_t *)addr)[9], ((uint8_t *)addr)[10], ((uint8_t *)addr)[11], ((uint8_t *)addr)[12], ((uint8_t *)addr)[13], ((uint8_t *)addr)[14], ((uint8_t *)addr)[15])
#define PRINTLLADDR(lladdr) PRINTF("[%02x:%02x:%02x:%02x:%02x:%02x]",(lladdr)->addr[0], (lladdr)->addr[1], (lladdr)->addr[2], (lladdr)->addr[3],(lladdr)->addr[4], (lladdr)->addr[5])
#else
#define PRINTF(...)
#define PRINT6ADDR(addr)
#define PRINTLLADDR(addr)
#endif


/* TODO: This server address is hard-coded for Cooja. */
#define SERVER_NODE(ipaddr)   uip_ip6addr(ipaddr, 0xaaaa, 0, 0, 0, 0xc30c, 0, 0, 0x00c3) /* cooja2 */

#define LOCAL_PORT      UIP_HTONS(COAP_DEFAULT_PORT+1)
#define REMOTE_PORT     UIP_HTONS(COAP_DEFAULT_PORT)
#define HISTORY 16
#define DEFAULT_TRESHOLD 25.0f
static float temperature[HISTORY];
static int temp_pos;
static float treshold;
static struct etimer activator_timer;


PROCESS(activator, "Fan Activator");
PROCESS(er_observe_client, "COAP Client Example");
PROCESS(rest_server_example, "Erbium Example Server");
AUTOSTART_PROCESSES(&er_observe_client, &rest_server_example, &activator);


/*----------------------------------------------------------------------------*/
/*
 * CoAP Observe Client
 */
/*----------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------*/
static uip_ipaddr_t server_ipaddr[1]; /* holds the server ip address */
static coap_observee_t *obs;


#define TOGGLE_INTERVAL 5
 /* The path of the resource to observe */
#define OBS_RESOURCE_URI "temperature/push"


/*----------------------------------------------------------------------------*/
/*
 * RSSI Scanner
 */
/*----------------------------------------------------------------------------*/

static void
do_rssi(void)
{
  printf("RSSI:");
  //set_frq(RF_CHANNEL);
  printf("%d ", radio_sensor.value(RADIO_SENSOR_LAST_VALUE) + 55);
  printf("\n");
}

/*----------------------------------------------------------------------------*/
/*
 * Handle the response to the observe request and the following notifications
 */
static void
notification_callback(coap_observee_t *obs, void *notification,
                      coap_notification_flag_t flag)
{
  int len = 0;
  const uint8_t *payload = NULL;

  printf("Notification handler\n");
  printf("Observee URI: %s\n", obs->url);
  if(notification) {
    len = coap_get_payload(notification, &payload);
  }
  switch(flag) {
  case NOTIFICATION_OK:
    printf("NOTIFICATION OK: %*s\n", len, (char *)payload);
    do_rssi();
    break;
  case OBSERVE_OK: /* server accepeted observation request */
    printf("OBSERVE_OK: %*s\n", len, (char *)payload);
    break;
  case OBSERVE_NOT_SUPPORTED:
    printf("OBSERVE_NOT_SUPPORTED: %*s\n", len, (char *)payload);
    obs = NULL;
    break;
  case ERROR_RESPONSE_CODE:
    printf("ERROR_RESPONSE_CODE: %*s\n", len, (char *)payload);
    obs = NULL;
    break;
  case NO_REPLY_FROM_SERVER:
    printf("NO_REPLY_FROM_SERVER: "
           "removing observe registration with token %x%x\n",
           obs->token[0], obs->token[1]);
    obs = NULL;
    break;
  }
}
/*----------------------------------------------------------------------------*/
/*
 * Toggle the observation of the remote resource
 */
void
toggle_observation(void)
{
  if(obs) {
    printf("Stopping observation\n");
    coap_obs_remove_observee(obs);
    obs = NULL;
  } else {
    printf("Starting observation\n");
    obs = coap_obs_request_registration(server_ipaddr, REMOTE_PORT,
                                        OBS_RESOURCE_URI, notification_callback, NULL);
  }
}

static float mean(void) 
{
  int i;
  float mean = 0.0f;
  for(i = 0; i < HISTORY; i++) {
    float t = temperature[HISTORY];
    if(t != 0.0f) {
      mean += temperature[HISTORY];
    }
  }
  mean = mean / HISTORY;
  return mean;
}
/*----------------------------------------------------------------------------*/
/*
 * The main (proto-)thread. It starts/stops the observation of the remote
 * resource every time the timer elapses or the button (if available) is
 * pressed
 */
PROCESS_THREAD(er_observe_client, ev, data)
{
  PROCESS_BEGIN();

  static struct etimer et;

  /* store server address in server_ipaddr */
  SERVER_NODE(server_ipaddr);
  /* receives all CoAP messages */
  coap_init_engine();
  temp_pos = 0;
  /* init timer and button (if available) */
  etimer_set(&et, TOGGLE_INTERVAL * CLOCK_SECOND);
  toggle_observation();
  /* toggle observation every time the timer elapses or the button is pressed */
  while(1) {
    PROCESS_YIELD();
   /* if(etimer_expired(&et)) {
      printf("--Toggle timer--\n");
      toggle_observation();
      printf("\n--Done--\n");
      etimer_reset(&et);
    }*/
  }
  PROCESS_END();
}


/*----------------------------------------------------------------------------*/
/*
 * CoAP Server
 */
/*----------------------------------------------------------------------------*/

static void res_post_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);

/* A simple actuator example. Toggles the red led */
RESOURCE(res_toggle,
         "title=\"Red LED\";rt=\"Control\"",
         NULL,
         res_post_handler,
         NULL,
         NULL);

static void
res_post_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  leds_toggle(LEDS_RED);
}

PROCESS_THREAD(rest_server_example, ev, data)
{
  PROCESS_BEGIN();

  PRINTF("Starting Erbium Example Server\n");

#ifdef RF_CHANNEL
  PRINTF("RF channel: %u\n", RF_CHANNEL);
#endif
#ifdef IEEE802154_PANID
  PRINTF("PAN ID: 0x%04X\n", IEEE802154_PANID);
#endif

  PRINTF("uIP buffer: %u\n", UIP_BUFSIZE);
  PRINTF("LL header: %u\n", UIP_LLH_LEN);
  PRINTF("IP+UDP header: %u\n", UIP_IPUDPH_LEN);
  PRINTF("REST max chunk: %u\n", REST_MAX_CHUNK_SIZE);

  /* Initialize the REST engine. */
  rest_init_engine();
  rest_activate_resource(&res_toggle, "actuators/toggle");
/* Define application-specific events here. */
  while(1) {
    PROCESS_WAIT_EVENT();
  }        

  PROCESS_END();
}


/*---------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/*
 * Fan Activator
 */
/*----------------------------------------------------------------------------*/

PROCESS_THREAD(activator, ev, data)
{
  PROCESS_BEGIN();

  treshold = DEFAULT_TRESHOLD;
  
  static int f = 5;
  etimer_set(&activator_timer, CLOCK_SECOND / f);

  while(1) {
    PROCESS_WAIT_EVENT();
     if(etimer_expired(&activator_timer)) {
      leds_toggle(LEDS_BLUE);
      etimer_set(&activator_timer, CLOCK_SECOND / f);
    }
  }       
  
  PROCESS_END();
}
