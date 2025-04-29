#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <time.h>
#include <TOTP.h>
#include <sha1.h>
#include <WiFiManager.h>
#include <Preferences.h> // For storing OTP URL in non-volatile memory

// Initialize the display
TFT_eSPI tft = TFT_eSPI();

// WiFiManager instance
WiFiManager wifiManager;

// Preferences for storing OTP URL
Preferences preferences;

// Custom parameters for WiFiManager
WiFiManagerParameter *customOtpUrlParam;

// Flag to indicate if we should save config
bool shouldSaveConfig = false;

// AP mode settings when configuration fails
#define AP_NAME "TOTP_Setup"    // Name of the access point for wifi manager AP init setup
#define AP_PASSWORD "totpsetup" //

// Define a configuration reset button pin
#define RESET_BUTTON_PIN 0 // GPIO0 is often the BOOT button on ESP32 boards

// Storage constants
#define PREFS_NAMESPACE "totpapp"
#define OTP_URL_KEY "otpurl"
#define MAX_OTP_URL_LENGTH 255

// Forward declarations
void saveParamsCallback();
String loadOtpUrl();
void checkResetButton();

// display settings
#define TFT_WIDTH 320
#define TFT_HEIGHT 240
#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
#define TFT_RED 0xF800
#define TFT_GREEN 0x07E0
#define TFT_BLUE 0x001F
#define TFT_YELLOW 0xFFE0
#define TFT_PURPLE 0xF81F
#define TFT_ORANGE 0xFD20
#define TFT_CYAN 0x07FF
#define TFT_MAGENTA 0xF81F
#define TFT_GRAY 0x7BEF

// NTP server settings - corrected for EST timezone
const char *ntpServer = "pool.ntp.org"; // Primary NTP server
const long gmtOffset_sec = -18000;      // EST offset in seconds (-5 hours * 3600 seconds)
const int daylightOffset_sec = 3600;    // 1 hour DST offset when active
const char *timezone = "EST";
const char *ntpServer2 = "time.google.com";  // Backup NTP server
const char *ntpServer3 = "time.windows.com"; // Third backup NTP server

// TOTP variables
uint8_t hmacKey[32];
int keyLength = 0;
TOTP *totp = nullptr;
String totpCode = "";
unsigned long lastCodeCheck = 0;
int codeRefreshInterval = 1000; // Check for new code every 1 second

// Function to connect to Wi-Fi using WiFiManager
void connectToWiFi()
{
  Serial.println("Starting WiFiManager...");
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  // Show a clear setup header
  tft.setTextSize(2);
  tft.setCursor(10, 20);
  tft.println("WiFi Setup");
  tft.drawLine(10, 40, 310, 40, TFT_BLUE);

  // Show clear connection instructions with highlighted AP name and password
  tft.setTextSize(2);
  tft.setCursor(10, 50);
  tft.println("Connect your phone to:");

  // Display AP name with highlight
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(10, 75);
  tft.print("Name: ");
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.println(AP_NAME);

  // Display AP password with highlight
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(10, 100);
  tft.print("Password: ");
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.println(AP_PASSWORD);

  // Additional instructions for after connection
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 130);
  tft.println("Then open browser to:");
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(10, 155);
  tft.println("192.168.4.1");

  // Instructions for what to enter in portal
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 185);
  tft.println("Enter WiFi and OTP URL");

  // Set callback for saving custom parameters
  wifiManager.setSaveParamsCallback(saveParamsCallback);

  // Don't timeout the configuration portal once someone has connected
  // This solves the issue of timing out while the user is configuring
  wifiManager.setConfigPortalTimeout(0);

  // Set portal to block (wait for configuration)
  wifiManager.setConfigPortalBlocking(true);

  // Enable debug info through serial port
  wifiManager.setDebugOutput(true);

  // Optional: Set AP channel (1-13)
  wifiManager.setWiFiAPChannel(1);

  // Load current OTP URL to pre-fill field (if any)
  String currentOtpUrl = loadOtpUrl();

  // Create custom parameter for OTP Auth URL with simpler instructions
  char customParamStr[40] = "Paste OTP Auth URL here";

  // Add any existing OTP URL to the custom parameter
  customOtpUrlParam = new WiFiManagerParameter("otpauthurl", customParamStr, currentOtpUrl.c_str(), MAX_OTP_URL_LENGTH);

  // Add parameter to WiFiManager
  wifiManager.addParameter(customOtpUrlParam);

  // Set AP name and password
  wifiManager.setAPStaticIPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));

  // Show waiting message
  Serial.println("Starting config portal");

  // Try to connect using saved credentials
  // If connection fails, start the config portal and WAIT until configured (blocking mode)
  if (!wifiManager.autoConnect(AP_NAME, AP_PASSWORD))
  {
    // This should not happen with timeout=0, but just in case
    Serial.println("Failed to connect");
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(10, 80);
    tft.println("WiFi config failed");
    tft.setCursor(10, 110);
    tft.println("Restarting...");
    delay(3000);
    ESP.restart();
  }

  // If we get here, we're connected to WiFi
  Serial.println("Connected to WiFi!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 80);
  tft.println("WiFi Connected!");
  tft.setCursor(10, 110);
  tft.println(WiFi.SSID());
  delay(1000);

  // Clean up parameter
  delete customOtpUrlParam;
}

// Function to check if the reset button is pressed
void checkResetButton()
{
  // Init pinMode for reset button
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  // Check if button is pressed
  if (digitalRead(RESET_BUTTON_PIN) == LOW)
  {
    Serial.println("Reset button pressed");

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 80);
    tft.println("WiFi RESET");
    tft.setCursor(10, 110);
    tft.println("Hold for 5 seconds");

    // Wait to see if the user holds the button
    int buttonHoldTime = 0;
    while (digitalRead(RESET_BUTTON_PIN) == LOW && buttonHoldTime < 50)
    {
      delay(100);
      buttonHoldTime++;

      // Draw progress bar
      tft.fillRect(10, 140, buttonHoldTime * 4, 20, TFT_RED);
    }

    // If button was held for 5 seconds
    if (buttonHoldTime >= 50)
    {
      tft.fillScreen(TFT_BLACK);
      tft.setCursor(10, 80);
      tft.println("Resetting WiFi...");

      Serial.println("Resetting WiFi credentials");
      wifiManager.resetSettings();
      delay(1000);
      ESP.restart();
    }
    else
    {
      // Button was released before timeout
      tft.fillScreen(TFT_BLACK);
      tft.setCursor(10, 80);
      tft.println("Reset cancelled");
      delay(1000);
    }
  }
}

// Function to synchronize time with NTP server
void syncTime()
{
  // Configure time with corrected timezone settings
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer, ntpServer2, ntpServer3);

  // Wait for time to be set
  int timeout = 20;
  time_t now = 0;
  while (now < 1000000000 && timeout > 0)
  {
    delay(500);
    time(&now);
    timeout--;
  }

  Serial.println("Time synchronized with NTP server");
}

// Function to get the current time with detailed output
void getCurrentTime()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 1000))
  {
    Serial.println("Failed to obtain time");
    return;
  }

  // Get UTC time as well for TOTP calculation
  time_t now;
  time(&now);
  struct tm utc;
  gmtime_r(&now, &utc);

  Serial.print("Local time: ");
  Serial.print(&timeinfo, "%Y-%m-%d %H:%M:%S");
  Serial.println();

  Serial.print("UTC time: ");
  Serial.print(&utc, "%Y-%m-%d %H:%M:%S");
  Serial.print(" (");
  Serial.print(now);
  Serial.println(" seconds since epoch)");

  // Calculate and show time step used for TOTP
  unsigned long timeStep = now / 30;
  Serial.print("Current 30-second time step: ");
  Serial.println(timeStep);
}

// Function to convert base32 to binary
int base32Decode(const char *encoded, uint8_t *result, int bufSize)
{
  int buffer = 0;
  int bitsLeft = 0;
  int count = 0;

  const char *ptr = encoded;
  while (*ptr && count < bufSize)
  {
    char ch = *ptr;
    ptr++;

    // Skip padding characters
    if (ch == '=')
      continue;

    // Convert from base32 character to 5-bit value
    buffer <<= 5;

    if (ch >= 'A' && ch <= 'Z')
      buffer |= (ch - 'A');
    else if (ch >= '2' && ch <= '7')
      buffer |= (ch - '2' + 26);
    else if (ch >= 'a' && ch <= 'z') // Also accept lowercase
      buffer |= (ch - 'a');
    else
      return -1; // Invalid character

    bitsLeft += 5;
    if (bitsLeft >= 8)
    {
      result[count++] = (uint8_t)((buffer >> (bitsLeft - 8)) & 0xFF);
      bitsLeft -= 8;
    }
  }

  return count;
}

// Function to parse the OTP Auth link and extract the secret key
String parseOTPAuthLink(const char *link)
{
  String secretKey = "";
  String linkStr = String(link);
  int secretStart = linkStr.indexOf("secret=") + 7;  // Find the start of the secret key
  int secretEnd = linkStr.indexOf("&", secretStart); // Find the end of the secret key
  if (secretStart != -1 && secretEnd != -1)
  {
    secretKey = linkStr.substring(secretStart, secretEnd); // Extract the secret key
  }
  return secretKey;
}

// Initialize TOTP from secret key
bool initTOTP(const char *secretBase32)
{
  keyLength = base32Decode(secretBase32, hmacKey, sizeof(hmacKey));

  if (keyLength > 0)
  {
    Serial.print("Secret key length: ");
    Serial.println(keyLength);
    Serial.print("Secret key bytes: ");
    for (int i = 0; i < keyLength; i++)
    {
      if (hmacKey[i] < 0x10)
        Serial.print("0");
      Serial.print(hmacKey[i], HEX);
      Serial.print(" ");
    }
    Serial.println();

    if (totp != nullptr)
    {
      delete totp;
    }
    totp = new TOTP(hmacKey, keyLength, 30); // 30 second time step (standard)
    return true;
  }

  return false;
}

// TOTP generation function
String generateTOTP()
{
  if (totp == nullptr)
  {
    return "ERROR";
  }

  // Get current UTC time
  time_t now;
  time(&now);

  // Log timestamp for debugging
  Serial.print("TOTP epoch time: ");
  Serial.println(now);

  char *code = totp->getCode(now);
  return String(code);
}

// Function to display TOTP on the screen
void displayTOTP(const char *totp)
{
  tft.fillScreen(TFT_BLACK);

  // Display the TOTP code
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(4); // Larger text for better visibility
  tft.setCursor(80, 70);
  tft.println(String(totp));

  // Draw remaining time indicator
  time_t now;
  time(&now);
  int secondsRemaining = 30 - (now % 30);

  // Draw progress bar background
  tft.fillRect(50, 120, 220, 30, TFT_GRAY);

  // Draw progress bar
  int barWidth = (220 * secondsRemaining) / 30;
  uint16_t barColor = (secondsRemaining > 5) ? TFT_GREEN : TFT_RED;
  tft.fillRect(50, 120, barWidth, 30, barColor);

  // Display remaining seconds
  tft.setTextSize(2);
  tft.setCursor(135, 125);
  tft.setTextColor(TFT_BLACK);
  tft.println(String(secondsRemaining) + "s");

  // Display "TOTP Code:" label
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(50, 40);
  tft.println("TOTP CODE:");
}

// Function to display the current time on the screen
void displayTime(struct tm *timeinfo)
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(20, 80);
  tft.println(String(timeinfo->tm_hour) + ":" +
              (timeinfo->tm_min < 10 ? "0" : "") + String(timeinfo->tm_min) + ":" +
              (timeinfo->tm_sec < 10 ? "0" : "") + String(timeinfo->tm_sec));
}

// Function to save OTP URL to preferences
void saveOtpUrl(const char *otpUrl)
{
  preferences.begin(PREFS_NAMESPACE, false);
  preferences.putString(OTP_URL_KEY, otpUrl);
  preferences.end();
  Serial.println("OTP URL saved to preferences");
}

// Function to load OTP URL from preferences
String loadOtpUrl()
{
  preferences.begin(PREFS_NAMESPACE, true); // Read-only mode
  String otpUrl = preferences.getString(OTP_URL_KEY, "");
  preferences.end();

  if (otpUrl.length() > 0)
  {
    Serial.println("Loaded OTP URL from preferences");
  }
  else
  {
    Serial.println("No OTP URL found in preferences");
  }

  return otpUrl;
}

// WiFiManager save callback function
void saveParamsCallback()
{
  Serial.println("WiFiManager - Parameters saved");

  // Get the OTP URL from the custom parameter
  if (customOtpUrlParam)
  {
    String otpUrl = String(customOtpUrlParam->getValue());

    if (otpUrl.length() > 0)
    {
      Serial.print("OTP URL from portal: ");
      Serial.println(otpUrl);

      // Save the OTP URL to preferences
      saveOtpUrl(otpUrl.c_str());
    }
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.println("TOTP Generator - Starting");

  // Initialize the display
  tft.init();
  tft.setRotation(3); // Landscape orientation
  tft.fillScreen(TFT_BLACK);

  // Turn on backlight
  pinMode(14, OUTPUT);
  digitalWrite(14, HIGH);

  Serial.println("Display initialized");

  // Initialize reset button pin
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  // Display startup message
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(20, 60);
  tft.println("MBenz TOTP");
  tft.setCursor(20, 90);
  tft.println("WiFi: Check button");
  tft.setCursor(20, 120);
  tft.println("to reset config");
  delay(2000);

  // Check for WiFi reset request
  checkResetButton();

  // Display connecting message
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(20, 80);
  tft.println("Connecting WiFi...");

  // Connect to WiFi using WiFiManager
  connectToWiFi();

  tft.fillScreen(TFT_BLACK);
  tft.setCursor(20, 80);
  tft.println("Syncing time...");

  // Sync time with NTP server
  syncTime();
  delay(1000); // Wait for time sync
  getCurrentTime();

  // Load OTP URL from preferences
  String savedOtpUrl = loadOtpUrl();

  // Check if we have a saved OTP URL
  if (savedOtpUrl.length() > 0)
  {
    Serial.println("Using custom OTP URL from settings");

    // Initialize TOTP with saved URL
    String secretKey = parseOTPAuthLink(savedOtpUrl.c_str());
    if (secretKey.length() > 0)
    {
      Serial.print("Secret key from OTP link: ");
      Serial.println(secretKey);

      if (initTOTP(secretKey.c_str()))
      {
        Serial.println("TOTP initialized successfully");

        // Generate and print a test TOTP code
        String testCode = generateTOTP();
        Serial.print("Test TOTP code: ");
        Serial.println(testCode);

        // Show configured status on screen
        tft.fillScreen(TFT_BLACK);
        tft.setCursor(20, 60);
        tft.println("TOTP Ready!");
        tft.setTextColor(TFT_GREEN);
        tft.setCursor(20, 90);
        tft.println("Custom OTP configured");
        tft.setTextColor(TFT_WHITE);
      }
      else
      {
        Serial.println("Failed to initialize TOTP");
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_RED);
        tft.setCursor(20, 80);
        tft.println("TOTP Error!");
        tft.setCursor(20, 110);
        tft.println("Invalid secret key");
        tft.setTextColor(TFT_WHITE);
        delay(3000);
      }
    }
    else
    {
      Serial.println("Failed to parse OTP auth link");
      tft.fillScreen(TFT_BLACK);
      tft.setTextColor(TFT_RED);
      tft.setCursor(20, 80);
      tft.println("Invalid OTP URL!");
      tft.setCursor(20, 110);
      tft.println("Reset and reconfigure");
      tft.setTextColor(TFT_WHITE);
      delay(3000);
    }
  }
  else
  {
    // No OTP URL configured yet
    Serial.println("No OTP URL configured");
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(20, 60);
    tft.println("OTP not configured");
    tft.setCursor(20, 90);
    tft.println("Please hold reset");
    tft.setCursor(20, 120);
    tft.println("button to setup");
    tft.setTextColor(TFT_WHITE);

    // Wait for the user to reset and configure
    delay(5000);
  }

  // Final setup message
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(20, 80);
  tft.println("LETS GO!");
  delay(1000);
}

void loop()
{
  unsigned long currentMillis = millis();

  // Check if reset button is pressed
  if (digitalRead(RESET_BUTTON_PIN) == LOW)
  {
    checkResetButton();
  }

  // Check WiFi connection status periodically
  static unsigned long lastWifiCheck = 0;
  if (currentMillis - lastWifiCheck >= 30000)
  { // Check every 30 seconds
    lastWifiCheck = currentMillis;
    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("WiFi connection lost. Reconnecting...");
      // Display WiFi reconnecting message in corner
      tft.fillRect(220, 0, 100, 20, TFT_BLACK);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.setTextSize(1);
      tft.setCursor(220, 5);
      tft.print("WiFi Recon...");

      // Try to reconnect without resetting settings
      WiFi.reconnect();
    }
  }

  // Check if it's time to update the code
  if (currentMillis - lastCodeCheck >= codeRefreshInterval)
  {
    lastCodeCheck = currentMillis;

    // Get the current time step
    time_t now;
    time(&now);
    unsigned long timeStep = now / 30;
    static unsigned long lastTimeStep = 0;

    // Generate new TOTP code only if time step changed
    if (timeStep != lastTimeStep)
    {
      lastTimeStep = timeStep;
      totpCode = generateTOTP();
      Serial.print("New TOTP code: ");
      Serial.println(totpCode);
    }

    // Always display the code with updated time indicator
    displayTOTP(totpCode.c_str());

    // Get and show current time
    struct tm timeinfo;
    if (getLocalTime(&timeinfo))
    {
      // Display time in a small area at the top of the screen
      tft.setTextColor(TFT_BLUE, TFT_BLACK);
      tft.setTextSize(1);
      tft.setCursor(10, 10);
      char timeStr[20];
      strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
      tft.println(timeStr);

      // Display WiFi information (SSID and signal strength)
      tft.setCursor(10, 20);
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.print(WiFi.SSID());
      tft.print(" (");
      tft.print(WiFi.RSSI());
      tft.print("dB)");
    }
  }
}