#include <FS.h>
#include <time.h>
#include <ESP8266WiFi.h>

#ifndef STASSID
#define STASSID "your-ssid"
#define STAPSK  "your-password"
#endif

const char *ssid = STASSID;
const char *pass = STAPSK;

long timezone = 2;
byte daysavetime = 1;


bool getLocalTime(struct tm * info, uint32_t ms) {
  uint32_t count = ms / 10;
  time_t now;

  time(&now);
  localtime_r(&now, info);

  if (info->tm_year > (2016 - 1900)) {
    return true;
  }

  while (count--) {
    delay(10);
    time(&now);
    localtime_r(&now, info);
    if (info->tm_year > (2016 - 1900)) {
      return true;
    }
  }
  return false;
}


void listDir(const char * dirname) {
  Serial.printf("Listing directory: %s\n", dirname);

  Dir root = SPIFFS.openDir(dirname);

  while (root.next()) {
    File file = root.openFile("r");
    Serial.print("  FILE: ");
    Serial.print(root.fileName());
    Serial.print("  SIZE: ");
    Serial.print(file.size());
    time_t t = file.getLastWrite();
    struct tm * tmstruct = localtime(&t);
    file.close();
    Serial.printf("  LAST WRITE: %d-%02d-%02d %02d:%02d:%02d\n", (tmstruct->tm_year) + 1900, (tmstruct->tm_mon) + 1, tmstruct->tm_mday, tmstruct->tm_hour, tmstruct->tm_min, tmstruct->tm_sec);
  }
}


void readFile(const char * path) {
  Serial.printf("Reading file: %s\n", path);

  File file = SPIFFS.open(path, "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.print("Read from file: ");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}

void writeFile(const char * path, const char * message) {
  Serial.printf("Writing file: %s\n", path);

  File file = SPIFFS.open(path, "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}

void appendFile(const char * path, const char * message) {
  Serial.printf("Appending to file: %s\n", path);

  File file = SPIFFS.open(path, "a");
  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if (file.print(message)) {
    Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
  file.close();
}

void renameFile(const char * path1, const char * path2) {
  Serial.printf("Renaming file %s to %s\n", path1, path2);
  if (SPIFFS.rename(path1, path2)) {
    Serial.println("File renamed");
  } else {
    Serial.println("Rename failed");
  }
}

void deleteFile(const char * path) {
  Serial.printf("Deleting file: %s\n", path);
  if (SPIFFS.remove(path)) {
    Serial.println("File deleted");
  } else {
    Serial.println("Delete failed");
  }
}

void setup() {
  Serial.begin(115200);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("Contacting Time Server");
  configTime(3600 * timezone, daysavetime * 3600, "time.nist.gov", "0.pool.ntp.org", "1.pool.ntp.org");
  struct tm tmstruct ;
  delay(2000);
  tmstruct.tm_year = 0;
  getLocalTime(&tmstruct, 5000);
  Serial.printf("\nNow is : %d-%02d-%02d %02d:%02d:%02d\n", (tmstruct.tm_year) + 1900, (tmstruct.tm_mon) + 1, tmstruct.tm_mday, tmstruct.tm_hour, tmstruct.tm_min, tmstruct.tm_sec);
  Serial.println("");

  Serial.printf("First, format an old-style filesystem without metadata\n");
  SPIFFS.setConfig(SPIFFSConfig(false, false));
  SPIFFS.format();
  SPIFFS.end();
  Serial.printf("Now mount it\n");
  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS mount failed");
    return;
  }

  listDir("/");
  deleteFile("/hello.txt");
  writeFile("/hello.txt", "Hello ");
  appendFile("/hello.txt", "World!\n");
  listDir("/");

  Serial.printf("The timestamp should be invalid above since the filesystem doesn't have metadata\n");

  Serial.printf("Now unmount and remount and perform the same operation.\n");
  Serial.printf("Timestamp should be invalid, but data should be good.\n");
  SPIFFS.end();
  Serial.printf("Now mount it\n");
  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS mount failed");
    return;
  }
  readFile("/hello.txt");
  listDir("/");

  Serial.printf("Now format a new SPIFFS FS with timestamp support\n");

  SPIFFS.end();
  SPIFFS.setConfig(SPIFFSConfig().setEnableTime(true));
  SPIFFS.format();
  SPIFFS.end();
  Serial.printf("Now mount it\n");
  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS mount failed");
    return;
  }

  listDir("/");
  deleteFile("/hello.txt");
  writeFile("/hello.txt", "Hello ");
  appendFile("/hello.txt", "World!\n");
  listDir("/");

  Serial.printf("The timestamp should be valid above\n");

  Serial.printf("Now unmount and remount and perform the same operation.\n");
  Serial.printf("Timestamp should be valid, data should be good.\n");
  SPIFFS.end();
  Serial.printf("Now mount it\n");
  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS mount failed");
    return;
  }
  readFile("/hello.txt");
  listDir("/");


}

void loop() { }
