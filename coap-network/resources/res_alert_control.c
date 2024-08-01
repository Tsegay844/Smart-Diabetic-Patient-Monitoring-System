#include <stdlib.h>
#include <string.h>
#include "contiki.h"
#include "coap-engine.h"
#include "os/dev/leds.h"
#include "global_variables.h"
#include "sys/log.h"

/* Log configuration */
#define LOG_MODULE "insulin-control"
#define LOG_LEVEL LOG_LEVEL_APP

// Declaration of the PUT handler function
static void control_put_handler(coap_message_t *request, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);


/**CoAP resource definition for alert control.
 * it defines a CoAP resource named "res_alert_control" with the title "Alert"
 * and resource type "Control". It supports the PUT method, which is handled
 * by the "control_put_handler" function.
 */
RESOURCE(res_alert_control,
         "title=\"Alert\";rt=\"Control\"",
         NULL,
         NULL,
         control_put_handler, // PUT handler
         NULL);


/**Put_Handler for CoAP PUT requests to control the alert state.
 * It processes incoming PUT requests to set the alert state.
 * It expects a variable named "status" with the value "ON" or "OFF".
 * It updates the `alert_activate` global variable accordingly.
 * If the request is invalid, it sets the response status to 400 (Bad Request).
 */
static void control_put_handler(coap_message_t *request, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset) {
	size_t len = 0;
	const char *text = NULL;
	char status[4]; // Buffer to store the status value (max 3 characters + null terminator)
	memset(status, 0, 3); // Initialize the status buffer

    	bool response_status = true;

	// Retrieve the "status" variable from the request
	len = coap_get_post_variable(request, "status", &text);
	
	// Check the value of the status and update the alert state accordingly
	if(len > 0 && len < 4) {
		memcpy(status, text, len);// Copy the status value to the buffer
		if(strncmp(status, "ON", len) == 0) {
			alert_activate = true;
			printf("Alert Activated\n");
			
		} else if(strncmp(status, "OFF", len) == 0) {
			alert_activate = false;
			printf("Alert Diactivated\n");
		} else {
			response_status = false; // Invalid status value
		}
	} else {
		response_status = false; // Invalid length of the status value
	}
	
	// If the request is invalid, set the response status code to 400 (Bad Request)
	if(!response_status) {
    		coap_set_status_code(response, BAD_REQUEST_4_00);
 	}
}


