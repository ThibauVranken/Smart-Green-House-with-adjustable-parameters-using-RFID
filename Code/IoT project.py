import re
from typing import NamedTuple
import threading

import paho.mqtt.client as mqtt
from influxdb import InfluxDBClient
from time import sleep

first_connection_made = False

# InfluxDB instellingen
INFLUXDB_ADDRESS = 'localhost'
INFLUXDB_USER = '...'
INFLUXDB_PASSWORD = '...'
INFLUXDB_DATABASE = 'IoTproject'

# MQTT instellingen
MQTT_ADDRESS = 'localhost'
MQTT_USER = '...'
MQTT_PASSWORD = '...'
MQTT_TOPICWaarden = "serre/waarden/+"
MQTT_TOPICActuatoren = "serre/actuatoren/+"
MQTT_TOPICParameters = "serre/parameters/+"
MQTT_TOPICCommunication = "serre/communication/communication"
MQTT_REGEX = "serre/([^/]+)/([^/]+)"
MQTT_CLIENT_ID = 'MQTTInfluxDBBridge'

influxdb_client = InfluxDBClient(INFLUXDB_ADDRESS, 8086, INFLUXDB_USER, INFLUXDB_PASSWORD, INFLUXDB_DATABASE)

previous_sensor_values = {}

class SensorData(NamedTuple):
    location: str
    measurement: str
    value: float
def on_connect(client, userdata, flags, rc):
    global first_connection_made
    if not first_connection_made:
        print('Connected with result code ' + str(rc))
        first_connection_made = True
    client.subscribe(MQTT_TOPICWaarden)
    client.subscribe(MQTT_TOPICActuatoren)
    client.subscribe(MQTT_TOPICParameters)

def _is_valid_number(value):
    try:
        float_value = float(value)
        return not (float_value != float_value)  # Checks for NaN
    except ValueError:
        return False

def _parse_mqtt_message(topic, value):
    match = re.match(MQTT_REGEX, topic)
    if match:
        location = match.group(1)
        measurement = match.group(2)
        if measurement == 'nan':
            return None
        if _is_valid_number(value):
            return SensorData(location, measurement, float(value))
    else:
        return None

def _send_sensor_data_to_influxdb(sensor_data):
    json_body = [
        {
            'measurement': sensor_data.measurement,
            'tags': {
                'location': sensor_data.location
            },
            'fields': {
                'value': sensor_data.value
            }
        }
    ]
    influxdb_client.write_points(json_body)

def send_mqtt_signal(client, topic, message):
    client.publish(topic, message)

def on_message(client, userdata, msg):
    print(msg.topic + ' ' + str(msg.payload))
    sensor_data = _parse_mqtt_message(msg.topic, msg.payload.decode('utf-8'))
    if sensor_data is not None:
        _send_sensor_data_to_influxdb(sensor_data)
        send_mqtt_signal(client, MQTT_TOPICCommunication, "hallo")

def _init_influxdb_database():
    databases = influxdb_client.get_list_database()
    if len(list(filter(lambda x: x['name'] == INFLUXDB_DATABASE, databases))) == 0:
        influxdb_client.create_database(INFLUXDB_DATABASE)
    influxdb_client.switch_database(INFLUXDB_DATABASE)

def mqtt_loop():
    mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, MQTT_CLIENT_ID)
    mqtt_client.username_pw_set(MQTT_USER, MQTT_PASSWORD)
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message
    print("-------------------------")

    mqtt_client.connect(MQTT_ADDRESS, 1883)
    mqtt_client.loop_forever()

def main():
    _init_influxdb_database()

    mqtt_thread = threading.Thread(target=mqtt_loop)
    mqtt_thread.daemon = True
    mqtt_thread.start()

    while True:
        try:
            sleep(1)
        except KeyboardInterrupt:
            print("Program stopped by user")
            break


if __name__ == '__main__':
    print('MQTT to InfluxDB bridge')
    main()
