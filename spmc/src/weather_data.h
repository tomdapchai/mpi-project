#ifndef WEATHER_DATA_H
#define WEATHER_DATA_H

#include <stdbool.h>
#include <string.h>

#define MAX_CITY_LEN 64
#define MAX_ICON_LEN 32
#define MAX_TIMESTAMP_LEN 33

typedef struct
{
    char timestamp[MAX_TIMESTAMP_LEN];
    char city[MAX_CITY_LEN];
    int aqi;
    char weather_icon[MAX_ICON_LEN];
    float wind_speed;
    int humidity;
    bool valid; // Flag to indicate if this record is valid
} WeatherData;

// Function to print weather data
void print_weather_data(WeatherData *data);

#endif // WEATHER_DATA_H