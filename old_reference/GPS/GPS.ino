/*
* ===============================================================
*                    GEOLINKER GPS TRACKER
* ===============================================================
* 
* FEATURES:
* - Real-time GPS tracking with NMEA data parsing
* - WiFi connectivity for data transmission
* - Offline data storage with configurable buffer limits
* - Automatic reconnection capabilities
* - Timezone offset configuration
* - Multiple debug levels for troubleshooting
* - Status monitoring with detailed error codes
* - Configurable update intervals
* 
* WORKING:
* 1. Initializes GPS module on Serial1 with custom RX/TX pins
* 2. Connects to WiFi network using provided credentials
* 3. Continuously reads GPS data in main loop
* 4. Processes and validates GPS coordinates
* 5. Transmits location data to GeoLinker cloud service
* 6. Stores data offline when network is unavailable
* 7. Provides real-time status feedback via serial monitor
* 
* HARDWARE REQUIREMENTS:
* - ESP32 development board
* - GPS module (NMEA compatible)
* - WiFi network access
* 
* PIN CONNECTIONS:
* - GPS RX: GPIO16
* - GPS TX: GPIO17
* 
* CREDITS:
* - GeoLinker Library: GPS tracking and cloud connectivity
* - ESP32 Arduino Core: Hardware abstraction layer
* - NMEA GPS Protocol: Location data standardization
* 
* Author: CircuitDigest/Rithik_Krisna_M
* Version: 1.0
* Last Modified: 28/06/2025
* 
* ===============================================================
*/
#include <GeoLinker.h>
// ==================================================================
//                    HARDWARE CONFIGURATION
// ==================================================================
// GPS Serial Communication Setup
HardwareSerial gpsSerial(1);  // Using Serial1 for GPS communication
#define GPS_RX 16             // GPIO16 connected to GPS module TX pin
#define GPS_TX 17             // GPIO17 connected to GPS module RX pin
// GPS Communication Settings
#define GPS_BAUD 9600         // Standard NMEA GPS baud rate (9600 bps)
// ==================================================================
//                    NETWORK CONFIGURATION
// ==================================================================
// WiFi Network Credentials
const char* ssid = "yourSSID";       // Your WiFi network name (SSID)
const char* password = "yourPassword";   // Your WiFi network password
// ==================================================================
//                   GEOLINKER CONFIGURATION
// ==================================================================
// API Authentication
const char* apiKey = "xxxxxxxxxxxx";    // Your unique GeoLinker API key
const char* deviceID = "ESP-32_Tracker"; // Unique identifier for this device
// Data Transmission Settings
const uint16_t updateInterval = 2;       // How often to send data (seconds)
const bool enableOfflineStorage = true; // Store data when offline
const uint8_t offlineBufferLimit = 20;  // Maximum offline records to store
                                       // Keep minimal for MCUs with limited RAM
// Connection Management
const bool enableAutoReconnect = true;  // Automatically reconnect to WiFi
                                       // Note: Only applies to WiFi, ignored with GSM
// Timezone Configuration
const int8_t timeOffsetHours = 5;       // Timezone hours offset from UTC
const int8_t timeOffsetMinutes = 30;    // Timezone minutes offset from UTC
                                      // Example: IST = UTC+5:30
// Create GeoLinker instance
GeoLinker geo;
// ==================================================================
//                    INITIALIZATION SETUP
// ==================================================================
void setup() {
 // Initialize serial communication for debugging
 Serial.begin(115200);
 delay(1000);  // Allow serial to initialize
 Serial.println("Starting GeoLinker GPS Tracker...");
 // Initialize GPS serial communication with custom pins
 gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);
 Serial.println("GPS Serial initialized on pins 16(RX) and 17(TX)");
 // ==================================================================
 //                   GEOLINKER LIBRARY SETUP
 // ==================================================================
 // Initialize GeoLinker with GPS serial interface
 geo.begin(gpsSerial);
 Serial.println("GeoLinker library initialized");
 // Configure API authentication
 geo.setApiKey(apiKey);
 Serial.println("API key configured");
 // Set unique device identifier
 geo.setDeviceID(deviceID);
 Serial.println("Device ID set");
 // Configure data transmission interval
 geo.setUpdateInterval_seconds(updateInterval);
 Serial.print("Update interval set to ");
 Serial.print(updateInterval);
 Serial.println(" seconds");
 // Set debug verbosity level
 // Options: DEBUG_NONE, DEBUG_BASIC, DEBUG_VERBOSE
 geo.setDebugLevel(DEBUG_BASIC);
 Serial.println("Debug level set to BASIC");
 // Enable offline data storage capability
 geo.enableOfflineStorage(enableOfflineStorage);
 if(enableOfflineStorage) {
   Serial.println("Offline storage enabled");
 }
 // Enable automatic WiFi reconnection
 geo.enableAutoReconnect(enableAutoReconnect);
 if(enableAutoReconnect) {
   Serial.println("Auto-reconnect enabled");
 }
 // Set maximum offline buffer size (important for memory management)
 geo.setOfflineBufferLimit(offlineBufferLimit);
 Serial.print("Offline buffer limit set to ");
 Serial.print(offlineBufferLimit);
 Serial.println(" records");
 // Configure timezone offset for accurate timestamps
 geo.setTimeOffset(timeOffsetHours, timeOffsetMinutes);
 Serial.print("Timezone offset set to UTC+");
 Serial.print(timeOffsetHours);
 Serial.print(":");
 Serial.println(timeOffsetMinutes);
 // ==================================================================
 //                    NETWORK CONNECTION SETUP
 // ==================================================================
 // Configure for WiFi mode (alternative: GEOLINKER_GSM for cellular)
 geo.setNetworkMode(GEOLINKER_WIFI);
 Serial.println("Network mode set to WiFi");
 // Set WiFi network credentials
 geo.setWiFiCredentials(ssid, password);
 Serial.println("WiFi credentials configured");
 // Attempt WiFi connection
 Serial.print("Connecting to WiFi network: ");
 Serial.println(ssid);
 if (!geo.connectToWiFi()) {
   Serial.println("ERROR: WiFi connection failed!");
   Serial.println("Device will continue with offline storage mode");
 } else {
   Serial.println("WiFi connected successfully!");
 }
 Serial.println("\n" + String("=").substring(0,50));
 Serial.println("GeoLinker GPS Tracker setup complete!");
 Serial.println("Starting main tracking loop...");
 Serial.println(String("=").substring(0,50) + "\n");
}
// ==================================================================
//                   MAIN PROGRAM LOOP
// ==================================================================
void loop() {
 // ==========================================
 //         GEOLINKER MAIN OPERATION
 // ==========================================
 // Execute main GeoLinker processing cycle
 // This function handles:
 // - GPS data reading and parsing
 // - Network connectivity checking
 // - Data transmission to cloud service
 // - Offline storage management
 // - Error handling and recovery
 uint8_t status = geo.loop();
 // Process and display status information
 if (status > 0) {
   Serial.print("[STATUS] GeoLinker Operation: ");
   // Interpret status codes and provide user feedback
   switch(status) {
     case STATUS_SENT:
       Serial.println("✓ Data transmitted successfully to cloud!");
       break;
     case STATUS_GPS_ERROR:
       Serial.println("✗ GPS module connection error - Check wiring!");
       break;
     case STATUS_NETWORK_ERROR:
       Serial.println("⚠ Network connectivity issue - Data buffered offline");
       break;
     case STATUS_BAD_REQUEST_ERROR:
       Serial.println("✗ Server rejected request - Check API key and data format");
       break;
     case STATUS_PARSE_ERROR:
       Serial.println("✗ GPS data parsing error - Invalid NMEA format");
       break;
     case STATUS_INTERNAL_SERVER_ERROR:
       Serial.println("✗ GeoLinker server internal error - Try again later");
       break;
     default:
       Serial.print("? Unknown status code: ");
       Serial.println(status);
       break;
   }
 }
 // Small delay to prevent overwhelming the serial output
 // The actual timing is controlled by GeoLinker's internal mechanisms
 delay(100);
}