#include "weather_data.h"
#include <stdio.h>

void print_weather_data(WeatherData* data) {
    if (!data->valid) {
        printf("Invalid weather data\n");
        return;
    }
    
    printf("Timestamp: %s, City: %s, AQI: %d, Icon: %s, Wind: %.1f, Humidity: %d%%\n",
           data->timestamp,
           data->city,
           data->aqi,
           data->weather_icon,
           data->wind_speed,
           data->humidity);
} 