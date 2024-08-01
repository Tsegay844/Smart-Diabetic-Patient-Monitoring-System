import paho.mqtt.client as mqtt
import json
import time
import datetime
import pymysql

alert_active = False

# MQTT broker settings
broker_address = "127.0.0.1"
broker_port = 1883
app_mqtt_client = mqtt.Client()

# Database connection 
db = pymysql.connect(host="localhost", user="root", password="root", db="patientdata")
cursor = db.cursor()

## functions
def on_connect(app_mqtt_client, userdata, flags, rc):
    print("Connected to MQTT broker with result code: " + str(rc))
    connection_status = 1
    # Subscribe to sensor data topic (receives the data in json format)
    app_mqtt_client.subscribe("Heart/Data")
   
   
def on_disconnect(app_mqtt_client, userdata, rc):
    print("Disconnected from MQTT broker with result code: " + str(rc))
    mqtt_reconnect()

def on_message(app_mqtt_client, userdata, msg):
    if not msg.payload:
        print("Received an empty message.")
        return 
    global alert_active
    print("\n******************Cardiovascular Monitoring*************************\nReceived message on topic: " + str(msg.topic))#  + "\n"+ str(msg.payload.decode()))
    # Parsing the incoming message
    incoming_timestamp = datetime.datetime.now()
    msg_string = msg.payload.decode()
    msg_json = json.loads(msg_string)
    
    # Extract data from JSON
    patientId = msg_json["patientId"]
    client_id = msg_json["client_id"]
    heart_rate = msg_json["heart_rate"]
    blood_pressure = msg_json["blood_pressure"]
    button = msg_json["button"]
    
    # write the data in the mysql table
    write_sensor_data(patientId, client_id, heart_rate, blood_pressure, button, incoming_timestamp)
    
     # Check emergency
    average_bp = get_average_blood_pressure(patientId)
    average_heart_rate = get_average_heart_rate(patientId)
    
    if ((average_heart_rate > 100 or average_heart_rate < 60) or (average_bp > 120 or average_bp < 90) or (button == 1)):  
        app_mqtt_client.publish("Emergency_Alert", payload="ON")    
        print("\033[91m>>>Emergency Alert is activated!\033[0m")
        if (average_heart_rate > 100 or average_heart_rate < 60):
            print(f"Average Heart Rate for the last 10 entries: {average_heart_rate}")
            
        if ((average_bp > 120 or average_bp < 90)):
            print(f"Average Blood Pressure for the last 10 entries: {average_bp}")
        
        if (button == 1):
            print("The Emergeny button is pressed.")
            	
        alert_active = True
        time.sleep(2)
    else:
        if alert_active:
            #print("Turning alert OFF")
            app_mqtt_client.publish("Emergency_Alert!\n", payload="OFF")
            alert_active = False
        else:
            if (not alert_active):
                print("\033[92m>>>Normal state\033[0m")    
           	      
# atempts of reconnection to MQTT broker
def mqtt_reconnect():
    print("Attempt to reconnect to broker... ")
    while not app_mqtt_client.is_connected():
        try:
            # Attempt to reconnect - timeout 3 seconds before attempting again
            app_mqtt_client.reconnect()
            time.sleep(3)
        except OSError as e:
            print("Reconnection failed. Error: ", e)
            continue
    
#write query to store the sensor data to the MySQL database
def write_sensor_data(patientId, client_id, heart_rate, blood_pressure, button, incoming_timestamp):
    query = """
                INSERT INTO cardiovascular_monitoring (patientId, client_id, heart_rate, blood_pressure, button, incoming_timestamp)
                VALUES (%s, %s, %s, %s, %s, %s)
            """
    values = (patientId, client_id, heart_rate, blood_pressure, button, incoming_timestamp)
    cursor.execute(query, values)
    db.commit()
    print(f"Data inserted into database succefully!")
    
 
 # to calcualte average of latest 10 heart rate radings
def get_average_heart_rate(patientId):
    try:
        query = """
                SELECT heart_rate
                FROM cardiovascular_monitoring
                WHERE patientId = %s
                ORDER BY incoming_timestamp DESC
                LIMIT 10
                """
        cursor.execute(query, (patientId,))
        results = cursor.fetchall()

        if results:
            total = sum([int(row[0]) for row in results])
            average_heart_rate = total / len(results)
            return average_heart_rate
        else:
            print("No heart rate data found.")
            return None
    except Exception as e:
        print("Failed to fetch or calculate average heart rate:", e)
        return None


def get_average_blood_pressure(patientId):
    try:
        query = """
                SELECT blood_pressure
                FROM cardiovascular_monitoring
                WHERE patientId = %s
                ORDER BY incoming_timestamp DESC
                LIMIT 5
                """
        cursor.execute(query, (patientId,))
        results = cursor.fetchall()
        if results:
            total = sum([int(row[0]) for row in results])
            average_blood_pressure = total / len(results)
            return average_blood_pressure
        else:
            print("No blood pressure data found.")
            return None
    except Exception as e:
        print("Failed to fetch or calculate average blood pressure:", e)
        return None
        
        
        
def cloud_app():

    # Assign event callbacks
    app_mqtt_client.on_connect = on_connect
    app_mqtt_client.on_message = on_message
    app_mqtt_client.on_disconnect = on_disconnect
    conn = 0
    # Connect to the broker
    while conn == 0:
        try:
            app_mqtt_client.connect(broker_address, broker_port, 60)
            conn = 1
        except:
            print("Failed to connect to broker...")
        time.sleep(2)

    # Start the MQTT client loop - NON blocking
    app_mqtt_client.loop_start()
    
    # Wait for messages from the MQTT broker
    while True:
        time.sleep(1)
    
    client.publish("Emergency_Alert", payload="ON")    
    print("Alert triggered: ")
    
    
cloud_app()






