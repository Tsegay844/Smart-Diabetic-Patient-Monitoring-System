#include "contiki.h"
#include "coap-engine.h"
#include "sys/log.h"
#include "sys/etimer.h"
#include "lib/random.h" 
#include "global_variables.h"

/* Log configuration */
#define LOG_MODULE "Glucose-level"
#define LOG_LEVEL LOG_LEVEL_APP

//Declaration of the GET handler function
static void glucose_get_handler(coap_message_t *request, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);


/**CoAP resource definition for glucose level sensor.
 * This defines a CoAP resource named `res_glucose_sensor` with the title "Glucose Level".
 * It supports the GET method, which is handled by the `glucose_get_handler` function.
 */
RESOURCE(res_glucose_sensor,
         "title=\"Glucose Level\";obs",
         glucose_get_handler,
         NULL,
         NULL,
         NULL);


/* GET_Handler for CoAP GET requests to retrieve glucose level. 
 * It processes incoming GET requests to provide the current glucose level.
 * It formats the glucose level data as a JSON string (I choose the data encoding method) 
 * and sets it as the response payload.
 */
static void glucose_get_handler(coap_message_t *request, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset) {
    char message[40];// Buffer to store the JSON message
    
    // Format the glucose level data as JSON
    snprintf(message, sizeof(message), "{\"patient_Id\": 1, \"glucose_level\": %d}", glucose_level);
    size_t len = strlen(message); // Calculate the length of the message
    memcpy(buffer, message, len); // Copy the message to the response buffer

    // Set content format to JSON
    coap_set_header_content_format(response, APPLICATION_JSON);
    coap_set_header_etag(response, (uint8_t *)&len, 1);
    coap_set_payload(response, buffer, len);// Set the response payload

}
