
/*---------------------------------------------------------------------------*/
#include "contiki.h"
#include "net/routing/routing.h"
#include "mqtt.h"
#include "net/ipv6/uip.h"
#include "net/ipv6/uip-icmp6.h"
#include "net/ipv6/sicslowpan.h"
#include "sys/etimer.h"
#include "sys/ctimer.h"
#include "lib/sensors.h"
#include "dev/button-hal.h"
#include "dev/leds.h"
#include "os/sys/log.h"
#include "mqtt-client.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

/*---------------------------------------------------------------------------*/
// Define log module and log level
#define LOG_MODULE "mqtt-client"
#ifdef MQTT_CLIENT_CONF_LOG_LEVEL
#define LOG_LEVEL MQTT_CLIENT_CONF_LOG_LEVEL
#else
#define LOG_LEVEL LOG_LEVEL_DBG
#endif

/*---------------------------------------------------------------------------*/
/* MQTT broker address. */
#define MQTT_CLIENT_BROKER_IP_ADDR "fd00::1"

static const char *broker_ip = MQTT_CLIENT_BROKER_IP_ADDR;

// Defaukt config values
#define DEFAULT_BROKER_PORT         1883
#define DEFAULT_PUBLISH_INTERVAL    (30 * CLOCK_SECOND)



/*---------------------------------------------------------------------------*/
// Various states for MQTT connection handling 
static uint8_t state;

#define STATE_INIT    		0 // initial state
#define STATE_NET_OK    	1 // Network is initialized
#define STATE_CONNECTING      	2 // Connecting to MQTT broker
#define STATE_CONNECTED       	3 // Connection successful
#define STATE_SUBSCRIBED      	4 // TTopics of interest subscribed
#define STATE_DISCONNECTED    	5//  Disconnected from MQTT broker


/*---------------------------------------------------------------------------*/

//Declare the three protothreads: one for monitoring CVD-related data and publishing it,
//the others are for handling button presses and actuatoring simulation 
PROCESS_NAME(CVD_monitoring);
PROCESS_NAME(actuator_simulation);
PROCESS_NAME(push_button);
// Automatically start the defined processes
AUTOSTART_PROCESSES(&CVD_monitoring, &actuator_simulation, &push_button);

/*---------------------------------------------------------------------------*/

/* Maximum TCP segment size for outgoing segments of our socket */
#define MAX_TCP_SEGMENT_SIZE    32 // Maximum TCP segment size for outgoing data
#define CONFIG_IP_ADDR_STR_LEN   64 // Maximum length of an IP address string
#define JSON_BUFFER_SIZE 256
/*---------------------------------------------------------------------------*/


/*
 * Buffers for Client ID and Topics.
 * Make sure they are large enough to hold the entire respective string
 */
#define BUFFER_SIZE 64
// Buffers to store MQTT client ID, publication topic, and subscription topic
static char client_id[BUFFER_SIZE];
static char pub_topic[BUFFER_SIZE];
static char sub_topic[BUFFER_SIZE];

// Periodic timer to check the state of the MQTT client
#define STATE_MACHINE_PERIODIC     (CLOCK_SECOND * 1) // Check every 5 seconds
static struct etimer periodic_timer;

/*---------------------------------------------------------------------------*/

/*
 * The main MQTT buffers.
 * possible to increase if we start publishing more data.
 */
#define APP_BUFFER_SIZE 512

// Buffer to hold the data to be published via MQTT
static char app_buffer[APP_BUFFER_SIZE];

/*---------------------------------------------------------------------------*/

static struct mqtt_message *msg_ptr = 0; // Pointer to received MQTT messages
static struct mqtt_connection conn; // MQTT connection structure
mqtt_status_t status; // Variable to store MQTT operation statuses
char broker_address[CONFIG_IP_ADDR_STR_LEN];// Buffer to store the broker's address

/*---------------------------------------------------------------------------*/
//CVD monitoring process
PROCESS(CVD_monitoring, "CVD_Monitoring");

/*---------------------------------------------------------------------------*/


//sets the Emergency alert ON/OFF
static bool Emergency_Alert_ON = false;
static bool button_pressed = false;

/** The following functions are used for simulating sensor data for a CVD monitoring
 *  They generate random values within a specified range to mimic real-world sensor readings.
 *  The generated data is used to test and demonstrate the functionality of the application 
 *  without relying on actual sensor hardware.
 */
 
//Normal heart rate ranges 60-100 bpm mmHg
int simulate_heart_rate() {
     //Randomly generate heart rate with a broader ranges 40-125 bpm 
    return (int)(rand() % (125 - 40) + 40);
}

//Normal blood pressure ranges 90-120 mmHg
int simulate_blood_pressure() {
   //Randomly generate heart rate with a broader ranges 85-135 mmHg
    return (int)(rand() % (135 - 85) + 85);
}

// Check if the button is pressed
int is_button_pressed() {
    if (button_pressed) {
        return 1; 
    } else { 
        return 0; 
    }
}

/*---------------------------------------------------------------------------*/


/* MQTT publish handler, to handle incoming message from MQTT Broker
* It recieves message and checks if the recieved topic is "Emergency_Alert"
* if the Topic is "Emergency_Alert" the it checks whethere the poyload is "ON" or "OFF"
* and activate and deactivate the Emergency Alert accordingly 
*/
static void pub_handler(const char *topic, uint16_t topic_len, const uint8_t *chunk,
            uint16_t chunk_len)
{
  printf("Pub Handler: topic='%s' (len=%u), chunk_len=%u\n", topic, topic_len, chunk_len);
  // Check if the received topic is "Emergency_Alert"
  if(strcmp(topic, "Emergency_Alert") == 0) {
		if(strcmp((const char*) chunk, "ON") == 0) {
		   Emergency_Alert_ON = true; // Activate the emergency aler
                   printf("Emergency_Alert switched ON\n");
		}
		else if(strcmp((const char*) chunk, "OFF") == 0) {
	           Emergency_Alert_ON = false;
                   printf("Emergency_Alert switched OFF\n");
		}
	}
	else {
		printf("Topic not valid!\n");
	}
}
/*---------------------------------------------------------------------------*/

// MQTT event handler
static void
mqtt_event(struct mqtt_connection *m, mqtt_event_t event, void *data)
{
  switch(event) {
  case MQTT_EVENT_CONNECTED: {
    printf("Application has a MQTT connection\n");

    state = STATE_CONNECTED;
    break;
  }
  case MQTT_EVENT_DISCONNECTED: {
    printf("MQTT Disconnect. Reason %u\n", *((mqtt_event_t *)data));

    state = STATE_DISCONNECTED;
    process_poll(&CVD_monitoring);
    break;
  }
  case MQTT_EVENT_PUBLISH: {
    msg_ptr = data;

    pub_handler(msg_ptr->topic, strlen(msg_ptr->topic),
                msg_ptr->payload_chunk, msg_ptr->payload_length);
    break;
  }
  case MQTT_EVENT_SUBACK: {
#if MQTT_311
    mqtt_suback_event_t *suback_event = (mqtt_suback_event_t *)data;

    if(suback_event->success) {
      printf("Application is subscribed to topic successfully\n");
    } else {
      printf("Application failed to subscribe to topic (ret code %x)\n", suback_event->return_code);
    }
#else
    printf("Application is subscribed to topic successfully\n");
#endif
    break;
  }
  case MQTT_EVENT_UNSUBACK: {
    printf("Application is unsubscribed to topic successfully\n");
    break;
  }
  case MQTT_EVENT_PUBACK: {
    printf("Publishing complete.\n");
    break;
  }
  default:
    printf("Application got a unhandled MQTT event: %i\n", event);
    break;
  }
}

static bool
have_connectivity(void)
{
  if(uip_ds6_get_global(ADDR_PREFERRED) == NULL ||
     uip_ds6_defrt_choose() == NULL) {
    return false;
  }
  return true;
}


/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/


/** CVD Monitoring Process
 *
 * This process manages the functionality of a Cardiovascular Disease (CVD) 
 * monitoring application within a constrained embedded environment.
 * It handles network connectivity, MQTT communication, sensor data simulation, 
 * and emergency alert subscriptions.
 *
 */
PROCESS_THREAD(CVD_monitoring, ev, data)
{

  PROCESS_BEGIN();
  
  printf("MQTT Client Process\n");

  // Initialize the ClientID as MAC address
  snprintf(client_id, BUFFER_SIZE, "%02x%02x%02x%02x%02x%02x",
                     linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
                     linkaddr_node_addr.u8[2], linkaddr_node_addr.u8[5],
                     linkaddr_node_addr.u8[6], linkaddr_node_addr.u8[7]);

  // Broker registration					 
  mqtt_register(&conn, &CVD_monitoring, client_id, mqtt_event,
                  MAX_TCP_SEGMENT_SIZE);
				  
  				  
  // Initial state of the connection state machine.
  state=STATE_INIT;
				    
  // Initialize periodic timer to check the status 
  etimer_set(&periodic_timer, STATE_MACHINE_PERIODIC);

  /* Main loop */
  while(1) {

    PROCESS_YIELD();

    if((ev == PROCESS_EVENT_TIMER && data == &periodic_timer) || 
	      ev == PROCESS_EVENT_POLL){
			  			  
		  // Initial state: check for network connectivity.
		  if(state==STATE_INIT){
			 if(have_connectivity()==true)  
				 state = STATE_NET_OK;
		  } 
		  
		  // Network connectivity established, attempt to connect to the MQTT broker.
		  if(state == STATE_NET_OK){
			  // Connect to MQTT server
			  printf("Connecting!\n");
			  
			  memcpy(broker_address, broker_ip, strlen(broker_ip));
			  
			  mqtt_connect(&conn, broker_address, DEFAULT_BROKER_PORT,
						   (DEFAULT_PUBLISH_INTERVAL * 3) / CLOCK_SECOND,
						   MQTT_CLEAN_SESSION_ON);
			  state = STATE_CONNECTING;
		  }
		  
		  // Successfully connected to the broker, subscribe to the emergency alert topic.
		  if(state==STATE_CONNECTED){
		  
			  // Subscribe to a topic
			  strcpy(sub_topic,"Emergency_Alert");

			  status = mqtt_subscribe(&conn, NULL, sub_topic, MQTT_QOS_LEVEL_0);

			  printf("Subscribing!\n");
			  if(status == MQTT_STATUS_OUT_QUEUE_FULL) {
				LOG_ERR("Tried to subscribe but command queue was full!\n");
				PROCESS_EXIT();
			  }
			  
			  state = STATE_SUBSCRIBED;
		  }
		// Subscribed to the topic, simulate sensor data and publish it  			  
		if(state == STATE_SUBSCRIBED){
			/* 
			 * Publish simulated sensors data to the "Heart/Data" MQTT topic.
			 * This section simulates sensor readings, formats them into a JSON payload,
			 * and publishes the data to the specified topic.
			 */		  
		    int patientId = 001;
		    // Retrieve the device's MAC address for client identification.
		    uint8_t* mac = linkaddr_node_addr.u8; 
    			snprintf(client_id, BUFFER_SIZE, "%02x%02x%02x%02x%02x%02x", 
            		         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            	// Simulate blood pressure and heart rate readings and button status         
		    int button = is_button_pressed();
		    int blood_pressure = simulate_blood_pressure();
		    int heart_rate = simulate_heart_rate();
		  
		    
		     sprintf(pub_topic, "%s", "Heart/Data");
		// Format the sensor data into a JSON payload	
		sprintf(app_buffer, 
			"{\"patientId\":%d,"
			"\"client_id\":\"%s\"," // Added quotes around %s for client_id
			"\"heart_rate\":%d,"
			"\"blood_pressure\":%d,"
			"\"button\":%d}" 
			, patientId, client_id, heart_rate, blood_pressure, button);
			
			// Publish the JSON payload to the specified MQTT topic.	   	
			mqtt_publish(&conn, NULL, pub_topic, (uint8_t *)app_buffer,
               strlen(app_buffer), MQTT_QOS_LEVEL_0, MQTT_RETAIN_OFF);
		    
		
		} else if ( state == STATE_DISCONNECTED ){
		   LOG_ERR("Disconnected form MQTT broker\n");	
		   
		   // Recover from error
		   mqtt_disconnect(&conn);
                   /* If disconnection occurs the state is changed to STATE_INIT in this way a new connection attempt starts */
		   state = STATE_INIT;
		}
		
		// Set the periodic timer to trigger the next state machine iteration.
		etimer_set(&periodic_timer, STATE_MACHINE_PERIODIC);
      
    }

  }

  PROCESS_END();
}


/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
/*
 * Actuator Simulation Process
 * 
 * This process simulates actuator behavior, specifically using LEDs to 
 * visually represent system states and events. It primarily focuses on:
 *   - Registration status indication: Blinking a yellow LED while 
 *     attempting to connect and subscribe to the MQTT broker.
 *   - Emergency alert visualization: Blinking a red LED when an emergency 
 *     alert message is received via MQTT.
 */
PROCESS(actuator_simulation, "Leds for simulation");
//various timers to control the actuating simulation
static struct etimer registration_led_timer;
static struct etimer emergency_led_timer;
static struct etimer led_on_timer;
static struct etimer red_led_timer;
static bool red_led_on = false;


PROCESS_THREAD(actuator_simulation, ev, data)
{

	PROCESS_BEGIN();
	
	
	etimer_set(&registration_led_timer, 1*CLOCK_SECOND);

	//yellow led blinking if connection lost between the border router and the collector
	while(state != STATE_SUBSCRIBED){
		PROCESS_YIELD();
		if (ev == PROCESS_EVENT_TIMER){
			if(etimer_expired(&registration_led_timer)){
				leds_single_toggle(LEDS_YELLOW);
				etimer_restart(&registration_led_timer);
			}
		}
	}

       etimer_set(&led_on_timer, 2*CLOCK_SECOND);
	leds_single_off(LEDS_YELLOW);
	
         /* 
	 * Emergency Alert Visualization:
	 * Blink the red LED when an emergency alert (Emergency_Alert_ON) is triggered.
	 */
	 
    // if the Emergency_Alart is ON, red leds is blinking
    etimer_set(&emergency_led_timer, CLOCK_SECOND * 5); 

    while (1) {
        PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_TIMER);

        if (etimer_expired(&emergency_led_timer)) {
            if (Emergency_Alert_ON && !red_led_on) {
              
                leds_toggle(LEDS_RED);
                etimer_set(&red_led_timer, CLOCK_SECOND * 10);
                red_led_on = true;
                Emergency_Alert_ON = false;
            } 
            else if (!Emergency_Alert_ON) {
            
                leds_off(LEDS_RED);
                red_led_on = false;
            }

            etimer_reset(&emergency_led_timer);
        }

        if (etimer_expired(&red_led_timer) && red_led_on) {
            leds_off(LEDS_RED);
            red_led_on = false;

            
        }
    }
	
       PROCESS_END();
     }
	

/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/

/*
 * Push Button Monitoring Process
 *
 * This process monitors the state of an emergency button and handles button 
 * press events.
 */
PROCESS(push_button, "Push Button");

PROCESS_THREAD(push_button, ev, data)
{
    static struct etimer btn_rest_timer;

    PROCESS_BEGIN();

    while (1) {
        PROCESS_WAIT_EVENT();

        // Check if the received event is a button press event
        if (ev == button_hal_press_event) {
            button_pressed = true;
            LOG_INFO("Button pressed\n");
            etimer_set(&btn_rest_timer, CLOCK_SECOND * 10);
        }
        else if (ev == PROCESS_EVENT_TIMER && etimer_expired(&btn_rest_timer)) {
            button_pressed = false;
            LOG_INFO("Button state reset to false after 10 seconds\n");
        }
    }

    PROCESS_END();
}


