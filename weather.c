/*              ___       _____        ___  __     __
 *  \        / |      /\    |   |   | |    |  \   /
 *   \  /\  /  |---  /__\   |   |---| |--- |__/  |
 *    \/  \/   |___ /    \  |   |   | |___ |  \ * \__
 *
 *	Licensed under the GPLv3
 *
 *	Dependencies are cjson and curl, both available on most distros' package
 *managers; try `make test` to check if they are installed. Pass the --location
 *or -l option if you want to get the coordinates of your location, convinient
 *for opening a weather.com forecast from a script, like in a polybar module.
 * 	Credit to the curl team for the example code I used (from here:
 *https://curl.se/libcurl/c/getinmemory.html)
 *
 *	Sample module:
 *	[module/weather]
 *	type = custom/script
 *	exec = cweather 2>/dev/null # Just incase curl fails, cutoff the long
 *error message tail = true inteval = 60 click-left = $your-browser-here
 *https://weather.com/weather/tenday/$(cweather --location)?par=google&temp=f #
 *Replace the 'f' with 'c' if you want metric units
 */

/* TODO TODO TODO
 * Add a script in the PKGBUILD to compile in an api key
 * TODO TODO TODO
*/

// This fork will display [current-weather] -> [16 hours ahead weather] with the ouptut formated for display as a polybar module

//#define API_KEY "" // Uncomment and paste your api key between the quotes if desired

#include <cjson/cJSON.h>       // For json parsing
#include <cjson/cJSON_Utils.h> // ^^
#include <curl/curl.h>         // To download stuff
#include <getopt.h>            // Get command line options
#include <stdio.h>             //
#include <stdlib.h>            //
#include <string.h>            //

#define getjson cJSON_GetObjectItemCaseSensitive
#define printj cJSON_Print
#define ISEMPTY(VAL) VAL##1

#ifdef API_KEY
static int key_flag = 1;
#else
char *API_KEY;
static int key_flag = 0;
#endif

void help(char *name) {
  printf("\
usage: %s [options]\n\
  options:\n\
    -h | --help		Display this help message\n\
    -k | --key <apikey>	Define api key (not necessary if compiled in)\n\
    -c | --celsius	Changes the temperature scale to celcius\n\
    -l | --location	Print latitude and longitude seperated by a comma\n\
    -s | --simple	Only use day/night icons instead of the full set\n\
",
         name);
}

// Gets the quotes off the json output
char *dequote(char *input) {
  char *p = (char *)malloc(strlen(input) + 1);
  strcpy(p, input);
  p++;
  p[(int)strlen(p) - 1] = '\0';
  return p;
}

// Escapes spaces in tricky city names (shoutout to Saint Augustine for
// 'finding' this bug)
char *spacereplace(char *input) {
  char *output = (char *)malloc(strlen(input) * 2);
  int J, length = strlen(input);
  for (int i = 0, j = 0; i < length; i++, j++) {
    if (input[i] == ' ') {
      output[j] = '%';
      j += 1;
      output[j] = '2';
      j += 1;
      output[j] = '0';
    } else {
      output[j] = input[i];
    }
    J = j;
  }
  output[J + 1] = '\0';
  return output;
}

static int help_flag, centigrade_flag, location_flag, icon_flag;
// Get command-line options with getopt
void getoptions(int argc, char **argv) {
  int c;
  for (;;) {
    static struct option
        long_options[] = // TODO add `static int help_flag centigrade_flag
                         // location_flag` at the top
        {
            {"help", no_argument, 0, 'h'},
            {"celcius", no_argument, 0, 'c'},
            {"location", no_argument, 0, 'l'},
            {"simple", no_argument, 0, 's'},
            {"key", required_argument, 0, 'k'},
        };

    int option_index = 0;
    c = getopt_long(argc, argv, "hclsk:", long_options, &option_index);

    if (c == -1) {
      break;
    }

    switch (c) {
    case 0: // do nothing
      break;
    case 'h':
      help_flag = 1;
      break;
    case 'c':
      centigrade_flag = 1;
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

// Functions from https://curl.se/libcurl/c/getinmemory.html
struct MemoryStruct {
  char *memory;
  size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb,
                                  void *userp) {
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;

  char *ptr = realloc(mem->memory, mem->size + realsize + 1);
  if (ptr == NULL) {
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

char *curl(char *url) {
  // getting a bunch of stuff ready
  CURL *curl_handle;
  CURLcode res;

  struct MemoryStruct chunk;
  chunk.memory = malloc(1);
  chunk.size = 0;

  curl_handle = curl_easy_init();                  // initiate the curl session
  curl_easy_setopt(curl_handle, CURLOPT_URL, url); // specify url
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION,
                   WriteMemoryCallback); // send data to memory
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk); // what
  curl_easy_setopt(
      curl_handle, CURLOPT_USERAGENT,
      "libcurl-agent/1.0"); // add user-agent field to appease the internet gods

  res = curl_easy_perform(curl_handle); // ladies and gentlemen, we got 'em

  // Now to see if we screwed up or not
  if (res != CURLE_OK) {
    fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
  } else {
    return chunk.memory;
  }

  curl_easy_cleanup(curl_handle);
  free(chunk.memory);
}

int main(int argc, char **argv) {
  curl_global_init(CURL_GLOBAL_ALL); // Initiates the global curl instance
                                    
// Get location from a public api
    char *location_api_url = "https://ipapi.co/json/"; // 100 per day or r8 limited
	cJSON *location_json = cJSON_Parse(curl(location_api_url));
	char *city = printj(getjson(location_json, "city"));
        char *err = printj(getjson(location_json, "error"));
        int city_id;
        if (err && !city) {
          city_id = 787050;
        }
 
  getoptions(argc, argv);

  if (location_flag == 1) {
    float lat, lon;
    lat = atof(printj(getjson(location_json, "latitude")));
    lon = atof(printj(getjson(location_json, "longitude")));
    printf("%.4f, %.4f\n", lat, lon);
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

  char *units = "metric", degreechar = 'C';
  if (centigrade_flag == 1) {
    units = "imperial";
    degreechar = 'F';
  }

  // Get json from openweathermap.org
  char weather_api_url[1024];

  if (err && !city) { 
      sprintf(weather_api_url, "https://api.openweathermap.org/data/2.5/weather?id=%d&units=%s&lang=en&appid=%s", city_id, units, API_KEY);
  }
  else {
      city = spacereplace(dequote(city));
      sprintf(weather_api_url, "https://api.openweathermap.org/data/2.5/weather?q=%s&units=%s&lang=en&appid=%s", city, units, API_KEY);
  }

  cJSON *weather_json = cJSON_Parse(curl(weather_api_url));

  // Get json for weather tomorrow
  char weather_api_fut_url[1024];

  if (!city) {
      sprintf(weather_api_fut_url, "https://api.openweathermap.org/data/2.5/"
          "forecast?id=%d&units=%s&appid=%s&cnt=5", city_id, units, API_KEY);
  } 
  else {
          city = spacereplace(dequote(city));
            sprintf(weather_api_fut_url, "https://api.openweathermap.org/data/2.5/"
            "forecast?q=%s&units=%s&appid=%s&cnt=5", city, units, API_KEY);
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

  // Get future forecast weather
  temp_future = atof(printj(getjson(
      getjson(cJSON_GetArrayItem(getjson(weather_tomorrow_json, "list"), 4),
              "main"),
      "temp")));
  icon_id_future = dequote(printj(getjson(
      cJSON_GetArrayItem(
          getjson(cJSON_GetArrayItem(getjson(weather_tomorrow_json, "list"), 4),
                  "weather"),
          0),
      "icon")));

  char *icon, *icon_fut, *night, *icons[64];

  if (icon_flag == 1) {
    if (icon_id[2] == 'd') {
      icon = "☀";
    } else {
      icon = "☽";
    }
    if (icon_id_future[2] == 'd') {
      icon_fut = "☀";
    } else {
      icon_fut = "☽";
    }
  } else {
    // char icon_array[50];
    icons[1] = "滛";
    icons[2] = "";
    icons[3] = "";
    icons[4] = "";
    icons[9] = "";
    icons[10] = "";
    icons[11] = "";
    icons[13] = "";
    icons[50] = "";
    icons[14] = "望";
    icons[15] = "";
    icons[16] = "";
    icons[17] = "";
    icons[22] = "";
    icons[23] = "";
    icons[24] = "";
    icons[26] = "";
    icons[64] = "";
    // hacky BS
    if (icon_id[2] == 'd') {
      icon = icons[atoi(icon_id)];
    }
    else {
      icon = icons[atoi(icon_id) + 13];
    }
    if (icon_id_future[2] == 'd') {
      icon_fut = icons[atoi(icon_id_future)];
    }
    else {
      icon_fut = icons[atoi(icon_id_future) + 13];
    }
 }

  // printf("%d--------------------------\n~ %s ~\n", atoi(icon_id_future),
  // icons[atoi(icon_id_future)]); hacky BS
  // sprintf(icon_id_future, "%c%c", icon_id[0], icon_id[1]);
  // icon_future = icons[atoi(icon_id_bb_future)];

  // Output
  // printf("%%{T6}%s%%{T1}%.0f°%%{T6}%s%%{T1}%.0f°\n",
         // icon, temperature, icon_fut ,
         // temp_future);  //   

  // Output                                                 move {0-3pt} inside %%{T1}
  printf("%%{T1}%.0f°%%{O-2pt}%%{T8}%%{F#F0C674}%s%%{F-}  %%{O-3pt}%%{T1}%.0f°%%{O-2pt}%%{T8}%%{F#F0C674}%s%%{F-}\n", temperature, icon, temp_future, icon_fut);


  // Cleanup cJSON pointers
  cJSON_Delete(weather_json);
  cJSON_Delete(weather_tomorrow_json);

  return 0;
}
