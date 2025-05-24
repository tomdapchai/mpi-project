from kafka import KafkaConsumer
import json
import os

KAFKA_BROKER = '52.139.169.162:9092'
TOPIC = 'climate-data'
OUTPUT_FILE = 'received_data.csv'

consumer = KafkaConsumer(
    TOPIC,
    bootstrap_servers=[KAFKA_BROKER],
    auto_offset_reset='earliest',
    enable_auto_commit=True,
    group_id='climate-group'
)

print(f"Consumer đang lắng nghe trên topic: {TOPIC}")

# Create a new CSV file with headers if it doesn't exist or is empty
if not os.path.exists(OUTPUT_FILE) or os.path.getsize(OUTPUT_FILE) == 0:
    with open(OUTPUT_FILE, 'w', encoding='utf-8') as f:
        f.write("timestamp,city,aqi,weather_icon,wind_speed,humidity\n")

with open(OUTPUT_FILE, 'a', encoding='utf-8') as f:
    for message in consumer:
        try:
            # Decode and parse the JSON message
            decoded_message = message.value.decode('utf-8')
            print(f"Nhận được: {decoded_message}")
            
            # Parse JSON
            data = json.loads(decoded_message)
            
            # Extract fields and format as CSV
            csv_line = f"{data['timestamp']},{data['city']},{data['aqi']},{data['weather_icon']},{data['wind_speed']},{data['humidity']}\n"
            
            # Write the CSV formatted line
            f.write(csv_line)
            f.flush()
        except json.JSONDecodeError as e:
            print(f"Error decoding JSON: {e}")
        except KeyError as e:
            print(f"Missing key in JSON data: {e}")
        except Exception as e:
            print(f"Error processing message: {e}")
