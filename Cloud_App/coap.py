from coapthon.client.helperclient import HelperClient
import json
import time
import datetime
import pymysql

# Database connection 
db = pymysql.connect(host="localhost", user="root", password="root", db="patientdata")
cursor = db.cursor()
 
 # IPv6 addresses of NORDIC Dongle devices
#host = "fd00::f6ce:36a7:5fc6:567"
host = "fd00::f6ce:3631:85d:5f3a"

# Cooja simulation IPv6 addresses 
#host = "fd00::202:2:2:2"
#host = "fd00::203:3:3:3"
#host = "fd00::204:4:4:4"

# CoAP server port 
port = 5683
# CoAP resource paths for different operations
get_path = "glucose/level" # Path to get glucose level
put_insulin_path = "glucose_control/insulin" # Path to control insulin actuator
put_glucagon_path = "glucose_control/glucagon" # Path to control glucagon actuator
put_alert_path = "glucose_control/alert" # Path to control alert actuator

# Create a CoAP client instance
client = HelperClient(server=(host, port))


#get glucose level from sensor
def get_sensor_data(path):
    print("\n")
    print("****************GLUCOSE LEVEL MONITORING*******************************")
    print("Sending GET request to read sensor data...")
    
    # Send a GET request to the specified path and store the response
    response = client.get(path)
    
    # Check if a response is received
    if response:
        
        try:
            # Parse the response payload from JSON to a Python dictionary
            data = json.loads(response.payload) 
            print(f"Response received from GET request: {data}")
            
            # Extract patient ID and glucose level from the response dictionary
            patient_id = data.get('patient_Id', 'Not specified')
            glucose_level = data.get('glucose_level', 'Not specified')
            incoming_timestamp = datetime.datetime.now()
            # Write the extracted data to the database, I have a explicit function to handle this call
            write_sensor_data(patient_id, glucose_level, incoming_timestamp)
        except json.JSONDecodeError:
            print("Failed to decode JSON from response.")
    else:
        print("No response. Check server availability or path")
        

#Store data into database
def write_sensor_data(patient_id, glucose_level, incoming_timestamp):
    # SQL query to insert data into the glucose_monitoring table
    query = "INSERT INTO glucose_monitoring (patientId, glucose_level, incoming_timestamp) VALUES (%s, %s, %s)"
    try:
        cursor.execute(query, (patient_id, glucose_level, incoming_timestamp))
        db.commit()
        print("Data inserted into database successfully!")
    except Exception as e:
        print("Failed to insert data into database:", e)

        
# To calcualte average of latest 10 glucose level readings
def calculate_average_glucose():
    query = """
    SELECT glucose_level 
    FROM glucose_monitoring 
    ORDER BY incoming_timestamp DESC 
    LIMIT 10
    """
    try:
        cursor.execute(query)
        results = cursor.fetchall()
        if results:
            total_glucose = sum(result[0] for result in results)
            average_glucose = total_glucose / len(results)
            return average_glucose
        else:
            print("No records found.")
            return None
    except Exception as e:
        print(f"Failed to fetch and calculate average glucose: {e}")
        return None
        

#excuting Logic based on the calculated averages
def manage_glucose_levels(average_glucose):
    if average_glucose > 180:
        deactivate_glucagon(put_glucagon_path)
        activate_insulin(put_insulin_path)
        activate_alert(put_alert_path)
        print("\033[91m>>>Alert is calling!\033[0m")
        
    elif 120 < average_glucose <= 180:
        deactivate_glucagon(put_glucagon_path)
        activate_insulin(put_insulin_path)
        deactivate_alert(put_alert_path)
        print("\033[93m>>>Insuline automatically activated\033[0m")
        
    elif 70 < average_glucose <= 120:
        deactivate_glucagon(put_glucagon_path)
        deactivate_insulin(put_insulin_path)
        deactivate_alert(put_alert_path)
        print("\033[92m>>>Normal state\033[0m")
        
    elif 50 < average_glucose <= 70:
        activate_glucagon(put_glucagon_path)
        deactivate_insulin(put_insulin_path)
        deactivate_alert(put_alert_path)
        print("\033[93m>>>glucagon automatically activated\033[0m")
        
    elif average_glucose <= 50:
        activate_glucagon(put_glucagon_path)
        deactivate_insulin(put_insulin_path)
        activate_alert(put_alert_path)
        print("\033[91m>>>Alert is calling!\033[0m")

    """
    The function below are for Controlling the actuators 
    by sending a PUT request to the CoAP server.

    Args:
        path: The CoAP resource path to control the actuators.
    """
def activate_insulin(path):
    payload = "status=ON"
    #print("Activating insulin...")
    response = client.put(path, payload)
    if response:
        pass #print(response.pretty_print())
    else:
        print("Insulin activation failed. Check server availability or path")
        
def deactivate_insulin(path):
    payload = "status=OFF"
    #print("Deactivating insulin...")
    response = client.put(path, payload)
    if response:
        pass #print(response.pretty_print())
    else:
        print("Insulin deactivation failed. Check server availability or path")

def activate_glucagon(path):
    payload = "status=ON"
    #print("Activating Glucagon...")
    response = client.put(path, payload)
    if response:
        pass #print(response.pretty_print())
    else:
        print("Glucagon activation failed. Check server availability or path")

def deactivate_glucagon(path):
    payload = "status=OFF"
    #print("Deactivating Glucagon...")
    response = client.put(path, payload)
    if response:
        pass #print(response.pretty_print())
    else:
        print("Glucagon deactivation failed. Check server availability or path")
 
        
def activate_alert(path):
    payload = "status=ON"
    #print("Activating Alert!...")
    response = client.put(path, payload)
    if response:
        pass #print(response.pretty_print())
    else:
        print("Alert activation failed. Check server availability or path")

def deactivate_alert(path):
    payload = "status=OFF"
    #print("Deactivating Alert...")
    response = client.put(path, payload)
    if response:
        pass #print(response.pretty_print())
    else:
        print("Alert deactivation failed. Check server availability or path")
      
try:  
    while True:
    	path = get_path
    	get_sensor_data(path)
    	average_glucose = calculate_average_glucose()
    	
    	if average_glucose is not None:
    	    manage_glucose_levels(average_glucose)
    	    print(f"Average Glocose_level for the last 10 entries: {average_glucose}\n")
    	#get glucose level every 10 second and act based on the recieved data  
    	time.sleep(5)
except Exception as e:
    print(f"An error occurred: {e}")
finally:

    client.stop()
