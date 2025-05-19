/*              ___       _____        ___  __     __
 *  \        / |      /\    |   |   | |    |  \   /
 *   \  /\  /  |---  /__\   |   |---| |--- |__/  |
 *    \/  \/   |___ /    \  |   |   | |___ |  \ * \__
 *
 *  Licensed under the GPLv3
 *
 *    Dependencies are cjson and curl, both available on most distros' package managers,
 *  try `make test` to check if they are installed. Pass the --location or -l option if 
 *  you want to get the coordinates of your location, convinient for opening a weather.
 *  com forecast from a script, like in a polybar module. Credit to the curl team for 
 *  the example code I used (from here: https://curl.se/libcurl/c/getinmemory.html)
 *
 *  Sample module: 
 *  [module/weather]
 *  type = custom/script
 *  exec = cweather 2>/dev/null # Just incase curl fails, cutoff the long error message
 *  tail = true
 *  interval = 3600
 *  click-left = $your-browser-here https://weather.com/weather/tenday/$(cweather --location)?par=google&temp=c # Default is now metric units
 */    

// #define API_KEY "" // Uncomment and paste your api key between the quotes if desired

#include <stdio.h>              //
#include <stdlib.h>             //
#include <string.h>             //
#include <curl/curl.h>          // To download stuff
#include <cjson/cJSON.h>        // For json parsing
#include <cjson/cJSON_Utils.h>  // ^^
#include <getopt.h>             // Get command line options

#define getjson cJSON_GetObjectItemCaseSensitive
#define printj cJSON_Print 
#define ISEMPTY(VAL) VAL ## 1

#ifdef API_KEY
    static int key_flag = 1;
#else
    char* API_KEY;
    static int key_flag = 0;
#endif

// Define a structure to map icon IDs to actual icons
typedef struct {
    char id[4];       // Icon ID from API (like "01d", "02n", etc.)
    char* icon;       // Corresponding icon character
} IconMapping;

// Create an icon mapping table with all day and night variants
const IconMapping icon_map[] = {
    {"01d", "󰖨"},    {"01n", "󰖔"},    // clear sky
    {"02d", ""},    {"02n", ""},    // few clouds
    {"03d", "󰖕"},    {"03n", "󰼱"},    // scattered clouds
    {"04d", ""},    {"04n", ""},    // broken clouds
    {"09d", ""},    {"09n", ""},    // shower rain  
    {"10d", ""},    {"10n", ""},    // rain
    {"11d", ""},    {"11n", ""},    // thunderstorm
    {"13d", ""},    {"13n", ""},    // snow
    {"50d", ""},    {"50n", ""}     // mist
};

// Number of entries in the icon map
const int icon_map_size = sizeof(icon_map) / sizeof(IconMapping);
void help(char* name) {
printf("\
usage: %s [options]\n\
  options:\n\
    -h | --help         Display this help message\n\
    -k | --key <apikey> Define api key (not necessary if compiled in)\n\
    -F | --fahrenheit   Changes the temperature scale to Fahrenheit\n\
    -C | --city         Manually input a city name\n\
    -l | --location     Print latitude and longitude seperated by a comma\n\
    -s | --simple       Only use day/night icons instead of the full set\n\
", name);
}

// Gets the quotes off the json output 
char *dequote(char *input) {
    int length = strlen(input);
    char *p = (char*)malloc(length-1);
    strncpy(p, input+1, length-2);
    p[length-2] = '\0';
    return p;
}

// Escapes spaces in tricky city names (shoutout to St. Augustine for 'finding' this bug)
// May also need to escape other characters but I haven't encountered them yet
char *spacereplace(char *input) {
    int inlen  = strlen(input);
    int outlen = inlen + 1;
    char* output = (char*)malloc(outlen);
    for (int i = 0, j = 0; i<inlen; i++, j++) {
        if (input[i] == ' ') {
            outlen+=2;
            realloc(output, outlen);
            output[j] = '%';
            j+=1;
            output[j] = '2';
            j+=1;
            output[j] = '0';
        } else {
            output[j] = input[i];
        }
    }
    output[outlen-1] = '\0';
    return output;
}

// Get command-line options with getopt
char *city  = "";
static int help_flag=0, fahrenheit_flag=0, location_flag=0, icon_flag=0;
static int city_id = 0; // For city ID support
static int err = 0;     // Error flag for city ID fallback

void getoptions(int argc, char** argv) {
    int c;
    for (;;) {
        static struct option long_options[] =
        {
            {"help",        no_argument,        0, 'h'},
            {"fahrenheit",  no_argument,        0, 'F'},
            {"city",        required_argument,  0, 'C'},
            {"location",    no_argument,        0, 'l'},
            {"simple",      no_argument,        0, 's'},
            {"key",         required_argument,  0, 'k'},
        };

        int option_index = 0;
        c = getopt_long(argc, argv, "hFClsk:", long_options, &option_index);

        if (c == -1) { break; }

        switch(c) {
            case 0:        // do nothing
                break;
            case 'h':
                help_flag = 1;
                break;
            case 'F':
                fahrenheit_flag = 1;
                break;
            case 'C':
                city = optarg;
                break;
            case 'l':
                location_flag = 1;
                break;
            case 's':
                icon_flag = 1;
                break;
            case 'k':
                #ifndef API_KEY
                    API_KEY = optarg;
                    key_flag = 1;
                #endif
                break;
            default:
                abort();
        }
    }
}

// Function to get icon based on icon ID
char* get_icon(const char* icon_id, int simple_mode) {
    // Simple mode just returns day/night indicators
    if (simple_mode) {
        return (icon_id[2] == 'd') ? "☀" : "☽"; //        󰖨  󰖙   󰼷 󰖨 |     󰖔 
    }
    
    // Look up the icon in our mapping table
    for (int i = 0; i < icon_map_size; i++) {
        if (strcmp(icon_map[i].id, icon_id) == 0) {
            return icon_map[i].icon;
        }
    }
    
    // Fallback icon if not found
    return "?";
}

// XXX Functions from https://curl.se/libcurl/c/getinmemory.html
struct MemoryStruct {
    char *memory;
    size_t size;
};
 
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
     
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(ptr == NULL) {
        // out of memory!
        printf("not enough memory (realloc returned NULL)\n");
        return 0;
    }
 
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
 
    return realsize;
}
// XXX End copied functions

char *curl(char *url) {
    // getting a bunch of stuff ready
    CURL* curl_handle;
    CURLcode res;

    struct MemoryStruct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;

    // initiate the curl session
    curl_handle = curl_easy_init();
    // specify url
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    // send data to memory 
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    // what
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
    // add user-agent field to appease the internet gods
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    res = curl_easy_perform(curl_handle); // get the data

    // Now to see if we screwed up or not
    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        return NULL;
    }
    else {
        curl_easy_cleanup(curl_handle);
        return chunk.memory;
    }
        
    //free(chunk.memory);
}

int main(int argc, char **argv) { 
    getoptions(argc, argv);
    curl_global_init(CURL_GLOBAL_ALL); // Initiates the global curl instance 

    // Get location from public api
    char *location_api_url = "http://ip-api.com/json/"; 
    char *raw_data_location = curl(location_api_url);
    cJSON *location_json = cJSON_Parse(raw_data_location);
    free(raw_data_location);
    if (strlen(city)==0) {
        char *dequoted = dequote(printj(getjson(location_json, "city")));
        city = dequoted;
    }
    
    if (location_flag == 1) {
        float lat, lon;
            lat = atof(printj(getjson(location_json, "lat")));
            lon = atof(printj(getjson(location_json, "lon")));
        printf("%.4f,%.4f\n", lat, lon);
        curl_global_cleanup(); // Stops and cleans up the global curl instance 
        return 0;
    }

    if (help_flag == 1) {
        help(argv[0]);
        return 0;
    }

    if (key_flag != 1) {
        printf("Error: missing api key\n");
        return 1;
    }

    char * units = "metric", degreechar = 'C';
    if (fahrenheit_flag == 1) {
        units = "imperial";
        degreechar = 'F';
    }

    // Get json from openweathermap.org
    char weather_api_url[1024];
    if (err && !city) { 
        sprintf(weather_api_url, "https://api.openweathermap.org/data/2.5/weather?id=%d&units=%s&lang=en&appid=%s", city_id, units, API_KEY);
    }
    else {
        city = spacereplace(city);
        sprintf(weather_api_url, "https://api.openweathermap.org/data/2.5/weather?q=%s&units=%s&lang=en&appid=%s", city, units, API_KEY);
    }
    cJSON *weather_json = cJSON_Parse(curl(weather_api_url));

    // Get json for weather tomorrow
    char weather_api_fut_url[1024];
    if (!city) {
        sprintf(weather_api_fut_url, "https://api.openweathermap.org/data/2.5/forecast?id=%d&units=%s&appid=%s&cnt=8", city_id, units, API_KEY);
    } 
    else {
        sprintf(weather_api_fut_url, "https://api.openweathermap.org/data/2.5/forecast?q=%s&units=%s&appid=%s&cnt=8", city, units, API_KEY);
    }
    cJSON *weather_tomorrow_json = cJSON_Parse(curl(weather_api_fut_url));

    // Stops and cleans up the global curl instance
    curl_global_cleanup();  
 
    // Declare some variables
    char *weather, *sky, *icon_id, *icon_id_future;
    float temperature, temp_future;

    // Get the weather data out of the json and put it in some variables
    weather = dequote(printj(getjson(
        cJSON_GetArrayItem(getjson(weather_json, "weather"), 0), "main")));
    temperature = atof(printj(getjson(getjson(weather_json, "main"), "temp")));
    icon_id = dequote(printj(getjson(
        cJSON_GetArrayItem(getjson(weather_json, "weather"), 0), "icon")));

    // Get future forecast weather in 3 hour increments
    temp_future = atof(printj(getjson(
        getjson(cJSON_GetArrayItem(getjson(weather_tomorrow_json, "list"), 4), // Integer that picks including 0-eth so _7_ is 8th inrement amounting to 24hrs
                "main"),
        "temp")));
    icon_id_future = dequote(printj(getjson(
        cJSON_GetArrayItem(
            getjson(cJSON_GetArrayItem(getjson(weather_tomorrow_json, "list"), 4),
                    "weather"),
            0),
        "icon")));

    char *icon = get_icon(icon_id, icon_flag);
    char *icon_fut = get_icon(icon_id_future, icon_flag);

    //------------------------------------------------------OUTPUT----------------------------------------------------------//
    // Only current weather
    // printf("%.0f°%%{O-3pt}%%{F#F0C674}%%{T9}%s%%{T-}%%{F-}\n", temperature, icon);

    // POLYBAR [current weather]->[weather in 15h] output with lemonbar tags
    printf("%.0f°%%{O-1pt}%%{F#F0C674}%%{T9}%s%%{T-}%%{F-}%%{O2pt}%%{T7}  %%{T-}%%{O-4pt}%.0f°%%{O-2pt}%%{F#F0C674}%%{T9}%s%%{T-}%%{F-}%%{O5px}\n", 
           temperature, icon, temp_future, icon_fut);

    // DWMBLOCKS and similar bars, simple output format
    // printf("%.0f° %s -> %.0f° %s\n", temperature, icon, temp_future, icon_fut);
    //------------------------------------------------------OUTPUT----------------------------------------------------------//

    // Cleanup cJSON pointers
    cJSON_Delete(weather_json);
    cJSON_Delete(weather_tomorrow_json);

    return 0;
}
