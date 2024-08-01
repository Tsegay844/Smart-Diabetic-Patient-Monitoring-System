#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "contiki.h"
#include "coap-engine.h"
#include "sys/etimer.h"
#include "os/dev/leds.h"
#include "coap-blocking-api.h"
#include "os/dev/button-hal.h"
#include "node-id.h"
#include "net/ipv6/simple-udp.h"
#include "net/ipv6/uip.h"
#include "net/ipv6/uip-ds6.h"
#include "net/ipv6/uip-debug.h"
#include "routing/routing.h"
#include "global_variables.h"
#include "sys/log.h"

//Global variables to manage the state of the actuators and glucose level
bool alert_activate = false;
bool insulin_activate = false;
bool glucagon_activate = false;
int glucose_level =90;

// Definition of server endpoint and resource type
#define SERVER_EP "coap://[fd00::1]:5683"
#define RESOURCE_TYPE "Glucose_monitoring"

// Log configuration
#define LOG_MODULE "glucose-monitoring"
#define LOG_LEVEL LOG_LEVEL_APP

//Simulation interval between sensor measurements
#define SIMULATION_INTERVAL 5
//Interval for registration retries with the observing collector
#define REGISTRATION_INTERVAL 2
//Interval for connection retries with the border router
#define CONNECTION_TEST_INTERVAL 2

//Coap Resources for the Glucose monitor: sensor (glucose_level) and actuators (insulin pump, glucagon pump and menrgency alert) 
extern coap_resource_t res_glucose_sensor;
extern coap_resource_t res_insulin_control;
extern coap_resource_t res_glucagon_control;
extern coap_resource_t res_alert_control;


//URL for registration 
char *service_url = "/registration";

//Registration status
static bool registered = false;


// Timer variables for various operations
static struct etimer simulation_timer;  //Timer for simulations of sensor measurements
static struct etimer connectivity_timer;  //Timer for connection retries with the border router
static struct etimer registration_timer;  // Timer for registration retries
static struct etimer registration_led_timer;  // Timer for LED blinking during registration
static struct etimer actuating_led_timer;  // Timer for actuating LEDs
static struct etimer led_on_timer;  // Timer for turning LEDs on
static struct etimer blink_timer;  // General timer for blinking


/*-----------------------------------------------------------------------*/
//Declare the three protothreads: one for the sensing subsystem,
//the others are for sensing and actuatoring simulation 
PROCESS(glucose_monitoring_server, "Glucose Monitoring");
PROCESS(actuator_simulation, "Actutor Simulation");
PROCESS(sensor_simulating, "sensor_simulating");
/*-----------------------------------------------------------------------*/

// Automatically start the declared processes
AUTOSTART_PROCESSES(&glucose_monitoring_server, &actuator_simulation, &sensor_simulating);


/**Checks connectivity with the border router
 * This function checks if the node can reach the border router. If the
 * router is reachable, it prints "The Border Router is reachable" message 
 * and returns true.
 * Otherwise, it prints a waiting message and returns false.
 */
static bool is_connected() {
	if(NETSTACK_ROUTING.node_is_reachable()) {
		printf("The Border Router is reachable\n");
		return true;
  	} else {
		printf("Waiting for connection with the Border Router\n");
	}
	return false;
}



/** Handler for connection requests sent by the collector.
 * If the recieved response is NULL, it indicates a timeout, 
 * and the function sets a timer for registration retries.
 * If the response contains "Success", the registrationf lag is set to true. 
 * Otherwise, it sets a timer for registration retries.
 */
void client_chunk_handler(coap_message_t *response) {
	const uint8_t *chunk;
	// Check if the response is NULL (timeout)
	if(response == NULL) {
		printf("Request timed out\n");
		etimer_set(&registration_timer, CLOCK_SECOND* REGISTRATION_INTERVAL);
		return;
	}

	// Get the payload from the response
	int len = coap_get_payload(response, &chunk);
	

	// Check if the payload contains "Success"
	if(strncmp((char*)chunk, "Success", len) == 0){
		registered = true;
	} else
		// Registration failed, retry after 2 SECONDS delay
		etimer_set(&registration_timer, CLOCK_SECOND* REGISTRATION_INTERVAL);
}



/**Simulates a blood glucose level reading.
 * This function generates random blood glucose levels within a broader range 
 * from 50 to 250 mg/dL to mimic real-world variations.
 */
static int simulate_blood_glucose_level() {

   // Generate a random glucose level between 50 and 250 mg/dL
    return (int)(rand() % (250 - 50 + 1) + 50); 
} 



/** Updates the simulated glucose level based on the active actuators.
 * when insulin is activated glucose level decreases by 10
 * when glucagon is activated glucose level increases by 10 
 * unless generate random glucose levels
 */
static void update_glucose_level() {
    if (insulin_activate) 
    {
        glucose_level -= 10;
     
    } else if (glucagon_activate) 
    {
        glucose_level += 10;
    } 
    else {
        glucose_level = simulate_blood_glucose_level();
    }
}


//The Main_server Protothread for glucose monitoring.
PROCESS_THREAD(glucose_monitoring_server, ev, data){
	PROCESS_BEGIN();

	static coap_endpoint_t server_ep;
        static coap_message_t request[1];

	//Activate CoAP resources
	printf("Starting CoAP server\n");
	coap_activate_resource(&res_glucose_sensor, "glucose/level"); 
	coap_activate_resource(&res_insulin_control, "glucose_control/insulin");
	coap_activate_resource(&res_glucagon_control, "glucose_control/glucagon");
	coap_activate_resource(&res_alert_control, "glucose_control/alert");


	// try to connect to the border router
	etimer_set(&connectivity_timer, CLOCK_SECOND * CONNECTION_TEST_INTERVAL);
	PROCESS_WAIT_UNTIL(etimer_expired(&connectivity_timer));
	while(!is_connected()) {
		etimer_reset(&connectivity_timer);
		PROCESS_WAIT_UNTIL(etimer_expired(&connectivity_timer));
	}

	//try to connect to the collector
	while(!registered) {
    	printf("Sending registration message\n");
    	
    	// Parse the server endpoint string (SERVER_EP) and store the parsed 
        // endpoint information in the server_ep variable
    	coap_endpoint_parse(SERVER_EP, strlen(SERVER_EP), &server_ep);
    	
    	// Prepare the message
    	coap_init_message(request, COAP_TYPE_CON, COAP_POST, 0);
    	coap_set_header_uri_path(request, service_url);
    	coap_set_payload(request, (uint8_t *)RESOURCE_TYPE, sizeof(RESOURCE_TYPE) - 1);

    	// Send the registration request and wait for a response
    	COAP_BLOCKING_REQUEST(&server_ep, request, client_chunk_handler);

    	PROCESS_WAIT_UNTIL(etimer_expired(&registration_timer));
    }

	PROCESS_END();
}


/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/

 //PROCESS FOR: ACTUATING leds as actuators
PROCESS_THREAD(actuator_simulation, ev, data)
{
    
	PROCESS_BEGIN();
	// Initialize the timer to blink every second
	etimer_set(&blink_timer, CLOCK_SECOND);
	
        // Set a timer for controlling the yellow LED blinking during registration.
	etimer_set(&registration_led_timer, 1*CLOCK_SECOND);

	//yellow led blinking until the connection to the border router and the collector is not complete
	
	while(!is_connected()){
		PROCESS_YIELD();
		if (ev == PROCESS_EVENT_TIMER){
			if(etimer_expired(&registration_led_timer)){
			printf("Connected..."); 
				leds_single_toggle(LEDS_YELLOW);
				etimer_restart(&registration_led_timer);
			}
		}	
	}
	// Indicate successful connection
	printf("Connected\n");
	etimer_set(&actuating_led_timer, 5*CLOCK_SECOND);
	etimer_set(&led_on_timer, 1*CLOCK_SECOND);
	
	// Turn off the yellow LED after the connection is established.
	leds_single_off(LEDS_YELLOW);

  // Main loop for LED actuation 
   while (1) {
    PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_TIMER && etimer_expired(&blink_timer));

    // Emergency Alert (Red LED)
    if (alert_activate) {
        leds_on(LEDS_RED); 
        leds_off(LEDS_GREEN);
        leds_single_off(LEDS_YELLOW);
    } 

    // Drug taking/medication enabled (Yellow LED)
    else if (insulin_activate || glucagon_activate) { 
        leds_off(LEDS_RED);  
       leds_single_on(LEDS_YELLOW);
        leds_off(LEDS_GREEN);
    } 

    // Normal/Stable State (Green LED) 
    else { 
        leds_off(LEDS_RED);
        leds_single_off(LEDS_YELLOW);
        leds_on(LEDS_GREEN);
    }

    // Reset the blink timer
    etimer_reset(&blink_timer);
}
	PROCESS_END();
}



/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/

       
//PROCESS FOR : periodic simulating glucose levels
PROCESS_THREAD(sensor_simulating, ev, data)
{
	PROCESS_BEGIN();
	
    //Set up a timer for glucose level simulation
    etimer_set(&simulation_timer, CLOCK_SECOND * SIMULATION_INTERVAL);

        while (1) {
        // Wait until the simulation timer expires
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&simulation_timer));

        // Update glucose level 
        update_glucose_level();

        // Reset the simulation timer
        etimer_reset(&simulation_timer);
    }
	
	PROCESS_END();
}

