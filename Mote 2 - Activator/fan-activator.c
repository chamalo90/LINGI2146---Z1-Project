/*
* Fan activator + Observer + Coap Server for treshold
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "contiki.h"
#include "contiki-net.h"
#include "rest-engine.h" // for coap server
#include "er-coap-engine.h" // for coap observe client
#include "dev/cc2420.h" // for radio sensor
#include "dev/cc2420_const.h"

/*
* Include Sensors
*/
#include "dev/button-sensor.h"
#include "dev/leds.h"
#include "dev/radio-sensor.h"


#define DEBUG 1
#if DEBUG
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINT6ADDR(addr) PRINTF("[%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x]", ((uint8_t *)addr)[0], ((uint8_t *)addr)[1], ((uint8_t *)addr)[2], ((uint8_t *)addr)[3], ((uint8_t *)addr)[4], ((uint8_t *)addr)[5], ((uint8_t *)addr)[6], ((uint8_t *)addr)[7], ((uint8_t *)addr)[8], ((uint8_t *)addr)[9], ((uint8_t *)addr)[10], ((uint8_t *)addr)[11], ((uint8_t *)addr)[12], ((uint8_t *)addr)[13], ((uint8_t *)addr)[14], ((uint8_t *)addr)[15])
#define PRINTLLADDR(lladdr) PRINTF("[%02x:%02x:%02x:%02x:%02x:%02x]",(lladdr)->addr[0], (lladdr)->addr[1], (lladdr)->addr[2], (lladdr)->addr[3],(lladdr)->addr[4], (lladdr)->addr[5])
#else
#define PRINTF(...)
#define PRINT6ADDR(addr)
#define PRINTLLADDR(addr)
#endif


/* TODO: The server address is hard-coded */
#define SERVER_NODE(ipaddr)   uip_ip6addr(ipaddr, 0xaaaa, 0, 0, 0, 0xc30c, 0, 0, 0x00c3) 

#define LOCAL_PORT      UIP_HTONS(COAP_DEFAULT_PORT+1)
#define REMOTE_PORT     UIP_HTONS(COAP_DEFAULT_PORT)
#define HISTORY 4
#define DEFAULT_TRESHOLD 25.0f

struct temp_record {
    int temperature;
    unsigned long time;
};
static struct temp_record temperature[HISTORY];
static int temp_pos;
static int rssi;
static int treshold;
static int fan_frequency = 5;
static struct etimer activator_timer;


PROCESS(activator, "Fan Activator");
PROCESS(er_observe_client, "COAP Client Example");
PROCESS(rest_server_example, "Erbium Example Server");
AUTOSTART_PROCESSES(&er_observe_client, &rest_server_example, &activator);





/*----------------------------------------------------------------------------*/
/*
 * RSSI Scanner
 */
/*----------------------------------------------------------------------------*/

static void
do_rssi(void)
{
  rssi = radio_sensor.value(RADIO_SENSOR_LAST_VALUE) + 55;
  printf("RSSI:%d\n ", rssi);
}

static void get_temperature_and_time(char * json, struct temp_record * r)  
{
  // { "temperature":21, "time":2412 }
  //Find the first ':'  and point to the next char
  char * tmpchar = strchr(json, ':') + 1;
  //find the ',' that marks the end of the temp and replace it by \0 for null terminating string
  char * tmp = strchr(tmpchar, ',');
  *tmp = '\0';
  r->temperature = atoi(tmpchar); //convert it to int

  //Find the first ':' for 'time' and point to the next char
  tmpchar = strchr(tmp + 1, ':') + 1;
  tmp = strchr(tmpchar, ' ');
  *tmp = '\0';
  r->time = atol(tmpchar); //convert it to long
}

static int mean(void) 
{
  int i;
  int mean = 0;
  for(i = 0; i < HISTORY; i++) {
    mean += temperature[i].temperature;
  }
  if (temp_pos < HISTORY) {
    mean = mean / temp_pos;
  }
  else mean = mean / HISTORY;
  return mean;
}

/*----------------------------------------------------------------------------*/
/*
 * CoAP Observe Client
 */
/*----------------------------------------------------------------------------*/

static uip_ipaddr_t server_ipaddr[1]; /* holds the server ip address */
static coap_observee_t *obs;


#define TOGGLE_INTERVAL 5
 /* The path of the resource to observe */
#define OBS_RESOURCE_URI "temperature/push"

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
    struct temp_record * r = &temperature[(temp_pos++)%HISTORY];
    get_temperature_and_time((char *)payload, r);
    PRINTF("Readed Temp: %d, Readed Time %lu\n", r->temperature, r->time);
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

/*----------------------------------------------------------------------------*/
/*
 * The observer thread. It starts the observation of the remote
 * resource (temperature)
 */
PROCESS_THREAD(er_observe_client, ev, data)
{
  PROCESS_BEGIN();

  /* store server address in server_ipaddr */
  SERVER_NODE(server_ipaddr);
  /* receives all CoAP responses */
  coap_init_engine();
  temp_pos = 0;
  
  toggle_observation();

  while(1) {
    PROCESS_YIELD();
  }
  PROCESS_END();
}


/*----------------------------------------------------------------------------*/
/*
 * CoAP Server for changing the threshold
 */
/*----------------------------------------------------------------------------*/

static void res_post_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);

/* A simple actuator example. Toggles the red led */
RESOURCE(res_toggle,
         "title=\"Treshold\";rt=\"Control\"",
         NULL,
         res_post_handler,
         NULL,
         NULL);

static void
res_post_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  size_t len = 0;
  const char *tresh = NULL;
  if((len = REST.get_post_variable(request, "treshold", &tresh))) {
    treshold = atoi(tresh);
  }
}

PROCESS_THREAD(rest_server_example, ev, data)
{
  PROCESS_BEGIN();

  PRINTF("Starting Erbium Example Server\n");

  PRINTF("uIP buffer: %u\n", UIP_BUFSIZE);
  PRINTF("LL header: %u\n", UIP_LLH_LEN);
  PRINTF("IP+UDP header: %u\n", UIP_IPUDPH_LEN);
  PRINTF("REST max chunk: %u\n", REST_MAX_CHUNK_SIZE);

  /* Initialize the REST engine. */
  rest_init_engine();
  rest_activate_resource(&res_toggle, "treshold");

  while(1) {
    PROCESS_WAIT_EVENT();
  }        

  PROCESS_END();
}


/*----------------------------------------------------------------------------*/
/*
 * Fan Activator
 */
/*----------------------------------------------------------------------------*/

PROCESS_THREAD(activator, ev, data)
{
  PROCESS_BEGIN();
  treshold = DEFAULT_TRESHOLD;
  
  
  etimer_set(&activator_timer, CLOCK_SECOND / fan_frequency);
  static int state = 0;
  while(1) {
    PROCESS_WAIT_EVENT();
     if(etimer_expired(&activator_timer)) {
      int mean_value = mean();
      int delta = (mean_value - treshold) * (1 + 100/rssi);
      if (delta > 0) {
        fan_frequency = delta ;
        if (delta > 7) delta = 7;
      }
      else delta = 0;
      if (state == 1)  {
        leds_off(LEDS_ALL);
        state = 0;
      }
      else {
        leds_on(delta);
        state = 1;
      }
      PRINTF("Fan Frq: %d, Delta: %d, Threshold: %d, Mean: %d, RSSI: %d \n", fan_frequency, delta, treshold, mean_value, rssi);
      etimer_set(&activator_timer, CLOCK_SECOND / fan_frequency);
    }
  }       
  
  PROCESS_END();
}
