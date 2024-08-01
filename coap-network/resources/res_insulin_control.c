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

/** CoAP resource definition for glucagon control.
 * it defines a CoAP resource named "res_glucagon_control" with the title "Insulin"
 * and resource type "Control". It supports the PUT method, which is handled
 * by the "control_put_handler" function.
 */
RESOURCE(res_insulin_control,
         "title=\"Insulin\";rt=\"Control\"",
         NULL,
         NULL,
         control_put_handler,
         NULL);


/** Put_Handler for CoAP PUT requests to control the glucagon state.
 * It processes incoming PUT requests to set the glucagon state.
 * It expects a variable named "status" with the value "ON" or "OFF".
 * It updates the "Insulin_activate" global variable accordingly.
 * If the request is invalid, it sets the response status to 400 (Bad Request).
 */
static void control_put_handler(coap_message_t *request, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset) {
	size_t len = 0;
	const char *text = NULL;
	char status[4];
	memset(status, 0, 3);

    bool response_status = true;


	// Retrieve the "status" variable from the request
	len = coap_get_post_variable(request, "status", &text);
	if(len > 0 && len < 4) {
		memcpy(status, text, len);
		if(strncmp(status, "ON", len) == 0) {
			insulin_activate = true;
			printf("Insulin Activated\n");
			
		} else if(strncmp(status, "OFF", len) == 0) {
			insulin_activate = false;
			printf("Insulin Diactivated\n");
		} else {
			response_status = false;
		}
	} else {
		response_status = false;
	}
	
	if(!response_status) {
		//set the response status code to 400 (Bad Request)
    		coap_set_status_code(response, BAD_REQUEST_4_00);
 	}
}


