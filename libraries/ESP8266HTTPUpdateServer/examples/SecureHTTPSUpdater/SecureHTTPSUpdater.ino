/*
  SecureHTTPSUpdater - SSL encrypted, password-protected firmware update

  This example starts a HTTPS server on the ESP8266 to allow firmware updates
  to be performed.  All communication, including the username and password,
  is encrypted via SSL.  Be sure to update the SSID and PASSWORD before running
  to allow connection to your WiFi network.

  IMPORTANT NOTES ABOUT SSL CERTIFICATES

  1. USE/GENERATE YOUR OWN CERTIFICATES
    While a sample, self-signed certificate is included in this example,
    it is ABSOLUTELY VITAL that you use your own SSL certificate in any
    real-world deployment.  Anyone with the certificate and key may be
    able to decrypt your traffic, so your own keys should be kept in a
    safe manner, not accessible on any public network.

  2. HOW TO GENERATE YOUR OWN CERTIFICATE/KEY PAIR
    A sample script, "make-self-signed-cert.sh" is provided in the
    ESP8266WiFi/examples/WiFiHTTPSServer directory.  This script can be
    modified (replace "your-name-here" with your Organization name).  Note
    that this will be a *self-signed certificate* and will *NOT* be accepted
    by default by most modern browsers.  They'll display something like,
    "This certificate is from an untrusted source," or "Your connection is
    not secure," or "Your connection is not private," and the user will
    have to manully allow the browser to continue by using the
    "Advanced/Add Exception" (FireFox) or "Advanced/Proceed" (Chrome) link.

    You may also, of course, use a commercial, trusted SSL provider to
    generate your certificate.  When requesting the certificate, you'll
    need to specify that it use SHA256 and 1024 or 512 bits in order to
    function with the axTLS implementation in the ESP8266.

  Interactive usage:
    Go to https://esp8266-webupdate.local/firmware, enter the username
    and password, and the select a new BIN to upload.

  To upload through terminal you can use:
  curl -u admin:admin -F "image=@firmware.bin" esp8266-webupdate.local/firmware

  Adapted by Earle F. Philhower, III, from the SecureWebUpdater.ino example.
  This example is released into the public domain.
*/

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServerSecure.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>

#ifndef STASSID
#define STASSID "your-ssid"
#define STAPSK  "your-password"
#endif

const char* host = "esp8266-webupdate";
const char* update_path = "/firmware";
const char* update_username = "admin";
const char* update_password = "admin";
const char* ssid = STASSID;
const char* password = STAPSK;

ESP8266WebServerSecure httpServer(443);
ESP8266HTTPUpdateServerTemplate<axTLS::WiFiServerSecure, axTLS::WiFiClientSecure> httpUpdater;

// The certificate is stored in PMEM
static const uint8_t x509[] PROGMEM = {
  0x30, 0x82, 0x01, 0xc9, 0x30, 0x82, 0x01, 0x32, 0x02, 0x09, 0x00, 0xe6,
  0x60, 0x8d, 0xa3, 0x47, 0x8f, 0x57, 0x7a, 0x30, 0x0d, 0x06, 0x09, 0x2a,
  0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05, 0x00, 0x30, 0x29,
  0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x0c, 0x0a, 0x70,
  0x73, 0x79, 0x63, 0x68, 0x6f, 0x70, 0x6c, 0x75, 0x67, 0x31, 0x12, 0x30,
  0x10, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0c, 0x09, 0x31, 0x32, 0x37, 0x2e,
  0x30, 0x2e, 0x30, 0x2e, 0x31, 0x30, 0x1e, 0x17, 0x0d, 0x31, 0x37, 0x30,
  0x32, 0x32, 0x34, 0x30, 0x38, 0x30, 0x35, 0x33, 0x36, 0x5a, 0x17, 0x0d,
  0x33, 0x30, 0x31, 0x31, 0x30, 0x33, 0x30, 0x38, 0x30, 0x35, 0x33, 0x36,
  0x5a, 0x30, 0x29, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x0a,
  0x0c, 0x0a, 0x70, 0x73, 0x79, 0x63, 0x68, 0x6f, 0x70, 0x6c, 0x75, 0x67,
  0x31, 0x12, 0x30, 0x10, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0c, 0x09, 0x31,
  0x32, 0x37, 0x2e, 0x30, 0x2e, 0x30, 0x2e, 0x31, 0x30, 0x81, 0x9f, 0x30,
  0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01,
  0x05, 0x00, 0x03, 0x81, 0x8d, 0x00, 0x30, 0x81, 0x89, 0x02, 0x81, 0x81,
  0x00, 0xb6, 0x59, 0xd0, 0x57, 0xbc, 0x3e, 0xb9, 0xa0, 0x6c, 0xf5, 0xd5,
  0x46, 0x49, 0xaa, 0x9a, 0xb3, 0xbf, 0x09, 0xa9, 0xbb, 0x82, 0x3b, 0xdf,
  0xb7, 0xe3, 0x5a, 0x8e, 0x31, 0xf7, 0x27, 0xdf, 0xaa, 0xed, 0xa3, 0xd6,
  0xf6, 0x74, 0x35, 0xfc, 0x8d, 0x0b, 0xbc, 0xa2, 0x96, 0x10, 0x57, 0xe8,
  0xb2, 0xaa, 0x94, 0xf2, 0x47, 0x12, 0x4e, 0x3f, 0x7c, 0x5e, 0x90, 0xfe,
  0xad, 0x75, 0x88, 0xca, 0x7b, 0x9a, 0x18, 0x15, 0xbe, 0x3d, 0xe0, 0x31,
  0xb5, 0x45, 0x7f, 0xe7, 0x9d, 0x22, 0x99, 0x65, 0xba, 0x63, 0x70, 0x81,
  0x3b, 0x37, 0x22, 0x97, 0x64, 0xc5, 0x57, 0x8c, 0x98, 0x9c, 0x10, 0x36,
  0x98, 0xf0, 0x0b, 0x19, 0x28, 0x16, 0x9a, 0x40, 0x31, 0x5f, 0xbc, 0xd9,
  0x8e, 0x73, 0x68, 0xe1, 0x6a, 0x5d, 0x91, 0x0b, 0x4f, 0x73, 0xa4, 0x6b,
  0x8f, 0xa5, 0xad, 0x12, 0x09, 0x32, 0xa7, 0x66, 0x3b, 0x02, 0x03, 0x01,
  0x00, 0x01, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d,
  0x01, 0x01, 0x05, 0x05, 0x00, 0x03, 0x81, 0x81, 0x00, 0x1b, 0x46, 0x78,
  0xd1, 0xfa, 0x21, 0xc1, 0xd6, 0x75, 0xc0, 0x83, 0x59, 0x57, 0x05, 0xd5,
  0xae, 0xf8, 0x8c, 0x78, 0x03, 0x65, 0x3b, 0xbf, 0xef, 0x70, 0x3f, 0x78,
  0xc6, 0xe1, 0x5a, 0xac, 0xb1, 0x93, 0x5b, 0x41, 0x35, 0x45, 0x47, 0xf8,
  0x07, 0x86, 0x40, 0x34, 0xa2, 0x9e, 0x2a, 0x16, 0x8d, 0xea, 0xf9, 0x1e,
  0x1f, 0xd7, 0x70, 0xb4, 0x28, 0x6b, 0xd8, 0xf5, 0x3f, 0x33, 0x3f, 0xc2,
  0x2c, 0x69, 0xf2, 0xa3, 0x54, 0x4d, 0xbf, 0x7d, 0xf9, 0xde, 0x05, 0x0c,
  0x9c, 0xe3, 0x1b, 0x72, 0x07, 0x7b, 0x41, 0x76, 0x1a, 0x57, 0x03, 0x5d,
  0xb2, 0xff, 0x4c, 0x17, 0xbd, 0xd7, 0x73, 0x32, 0x98, 0x26, 0x6b, 0x2c,
  0xc4, 0xbf, 0x6e, 0x01, 0x36, 0x8b, 0xbf, 0x00, 0x48, 0x9c, 0xfb, 0x3d,
  0x7d, 0x76, 0x1f, 0x55, 0x96, 0x43, 0xc5, 0x4e, 0xc1, 0xa3, 0xa1, 0x6a,
  0x94, 0x5f, 0x84, 0x3a, 0xdd
};

// And so is the key.  These could also be in DRAM
static const uint8_t rsakey[] PROGMEM = {
  0x30, 0x82, 0x02, 0x5c, 0x02, 0x01, 0x00, 0x02, 0x81, 0x81, 0x00, 0xb6,
  0x59, 0xd0, 0x57, 0xbc, 0x3e, 0xb9, 0xa0, 0x6c, 0xf5, 0xd5, 0x46, 0x49,
  0xaa, 0x9a, 0xb3, 0xbf, 0x09, 0xa9, 0xbb, 0x82, 0x3b, 0xdf, 0xb7, 0xe3,
  0x5a, 0x8e, 0x31, 0xf7, 0x27, 0xdf, 0xaa, 0xed, 0xa3, 0xd6, 0xf6, 0x74,
  0x35, 0xfc, 0x8d, 0x0b, 0xbc, 0xa2, 0x96, 0x10, 0x57, 0xe8, 0xb2, 0xaa,
  0x94, 0xf2, 0x47, 0x12, 0x4e, 0x3f, 0x7c, 0x5e, 0x90, 0xfe, 0xad, 0x75,
  0x88, 0xca, 0x7b, 0x9a, 0x18, 0x15, 0xbe, 0x3d, 0xe0, 0x31, 0xb5, 0x45,
  0x7f, 0xe7, 0x9d, 0x22, 0x99, 0x65, 0xba, 0x63, 0x70, 0x81, 0x3b, 0x37,
  0x22, 0x97, 0x64, 0xc5, 0x57, 0x8c, 0x98, 0x9c, 0x10, 0x36, 0x98, 0xf0,
  0x0b, 0x19, 0x28, 0x16, 0x9a, 0x40, 0x31, 0x5f, 0xbc, 0xd9, 0x8e, 0x73,
  0x68, 0xe1, 0x6a, 0x5d, 0x91, 0x0b, 0x4f, 0x73, 0xa4, 0x6b, 0x8f, 0xa5,
  0xad, 0x12, 0x09, 0x32, 0xa7, 0x66, 0x3b, 0x02, 0x03, 0x01, 0x00, 0x01,
  0x02, 0x81, 0x81, 0x00, 0xa8, 0x55, 0xf9, 0x33, 0x45, 0x20, 0x52, 0x94,
  0x7a, 0x81, 0xe6, 0xc4, 0xe0, 0x34, 0x92, 0x63, 0xe4, 0xb3, 0xb2, 0xf0,
  0xda, 0xa5, 0x13, 0x3d, 0xda, 0xb0, 0x3a, 0x1c, 0x7e, 0x21, 0x5d, 0x25,
  0x9a, 0x03, 0x69, 0xea, 0x52, 0x15, 0x94, 0x73, 0x50, 0xa6, 0x6f, 0x21,
  0x41, 0x2d, 0x26, 0x2f, 0xe9, 0xb1, 0x5e, 0x87, 0xa5, 0xaa, 0x7e, 0x88,
  0xfd, 0x73, 0xb4, 0xe7, 0xc4, 0x5c, 0xe7, 0x2d, 0xeb, 0x9e, 0x6b, 0xe1,
  0xf1, 0x38, 0x45, 0xf4, 0x10, 0x12, 0xac, 0x79, 0x40, 0x72, 0xf0, 0x45,
  0x89, 0x5c, 0x9d, 0x8b, 0x7b, 0x5d, 0x69, 0xd9, 0x11, 0xf9, 0x25, 0xff,
  0xe1, 0x2a, 0xb3, 0x6d, 0x49, 0x18, 0x8d, 0x38, 0x0a, 0x6f, 0x0f, 0xbd,
  0x48, 0xd0, 0xdd, 0xcb, 0x41, 0x5c, 0x2a, 0x75, 0xa0, 0x51, 0x43, 0x4a,
  0x0b, 0xf6, 0xa2, 0xd2, 0xe9, 0xda, 0x37, 0xca, 0x2d, 0xd7, 0x22, 0x01,
  0x02, 0x41, 0x00, 0xe7, 0x11, 0xea, 0x93, 0xf4, 0x0b, 0xe6, 0xa0, 0x1a,
  0x57, 0x2d, 0xee, 0x96, 0x05, 0x5c, 0xa1, 0x08, 0x8f, 0x9c, 0xac, 0x9a,
  0x72, 0x60, 0x5a, 0x41, 0x2a, 0x92, 0x38, 0x36, 0xa5, 0xfe, 0xb9, 0x35,
  0xb2, 0x06, 0xbb, 0x02, 0x58, 0xc8, 0x93, 0xd6, 0x09, 0x6f, 0x57, 0xd7,
  0xc1, 0x2e, 0x90, 0xb3, 0x09, 0xdd, 0x0c, 0x63, 0x99, 0x91, 0xb7, 0xe4,
  0xcc, 0x6f, 0x78, 0x24, 0xbc, 0x3b, 0x7b, 0x02, 0x41, 0x00, 0xca, 0x06,
  0x4a, 0x09, 0x36, 0x08, 0xaa, 0x27, 0x08, 0x91, 0x86, 0xc5, 0x17, 0x14,
  0x6e, 0x24, 0x9a, 0x86, 0xd1, 0xbc, 0x41, 0xb1, 0x42, 0x5e, 0xe8, 0x80,
  0x5a, 0x8f, 0x7c, 0x9b, 0xe8, 0xcc, 0x28, 0xe1, 0xa2, 0x8f, 0xe9, 0xdc,
  0x60, 0xd5, 0x00, 0x34, 0x76, 0x32, 0x36, 0x00, 0x93, 0x69, 0x6b, 0xab,
  0xc6, 0x8b, 0x70, 0x95, 0x4e, 0xc2, 0x27, 0x4a, 0x24, 0x73, 0xbf, 0xcd,
  0x24, 0x41, 0x02, 0x40, 0x40, 0x46, 0x75, 0x90, 0x0e, 0x54, 0xb9, 0x24,
  0x53, 0xef, 0x68, 0x31, 0x73, 0xbd, 0xae, 0x14, 0x85, 0x43, 0x1d, 0x7b,
  0xcd, 0xc2, 0x7f, 0x16, 0xdc, 0x05, 0xb1, 0x82, 0xbd, 0x80, 0xd3, 0x28,
  0x45, 0xcd, 0x6d, 0x9d, 0xdb, 0x7b, 0x42, 0xe0, 0x0c, 0xab, 0xb7, 0x33,
  0x22, 0x2a, 0xf4, 0x7e, 0xff, 0xae, 0x80, 0xb4, 0x8f, 0x88, 0x0a, 0x46,
  0xb2, 0xf8, 0x43, 0x11, 0x92, 0x76, 0x61, 0xbd, 0x02, 0x40, 0x5c, 0x86,
  0x3a, 0xdc, 0x33, 0x1a, 0x0e, 0xcb, 0xa7, 0xb9, 0xf6, 0xae, 0x47, 0x5e,
  0xbc, 0xff, 0x18, 0xa2, 0x8c, 0x66, 0x1a, 0xf4, 0x13, 0x00, 0xa2, 0x9d,
  0x3e, 0x5c, 0x9e, 0xe6, 0x4c, 0xdd, 0x4c, 0x0f, 0xe2, 0xc2, 0xe4, 0x89,
  0x60, 0xf3, 0xcc, 0x8f, 0x3a, 0x5e, 0xce, 0xaa, 0xbe, 0xd8, 0xb6, 0x4e,
  0x4a, 0xb5, 0x4c, 0x0f, 0xa5, 0xad, 0x78, 0x0f, 0x15, 0xd8, 0xc9, 0x4c,
  0x2b, 0xc1, 0x02, 0x40, 0x4e, 0xe9, 0x78, 0x48, 0x94, 0x11, 0x75, 0xc1,
  0xa2, 0xc7, 0xff, 0xf0, 0x73, 0xa2, 0x93, 0xd7, 0x67, 0xc7, 0xf8, 0x96,
  0xac, 0x15, 0xaa, 0xe5, 0x5d, 0x18, 0x18, 0x29, 0xa9, 0x9a, 0xfc, 0xac,
  0x48, 0x4d, 0xa0, 0xca, 0xa2, 0x34, 0x09, 0x7c, 0x13, 0x22, 0x4c, 0xfc,
  0x31, 0x75, 0xa0, 0x21, 0x1e, 0x7a, 0x91, 0xbc, 0xb1, 0x97, 0xde, 0x43,
  0xe1, 0x40, 0x2b, 0xe3, 0xbd, 0x98, 0x44, 0xad
};

void setup() {

  Serial.begin(115200);
  Serial.println();
  Serial.println("Booting Sketch...");
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid, password);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    WiFi.begin(ssid, password);
    Serial.println("WiFi failed, retrying.");
  }

  MDNS.begin(host);

  httpServer.getServer().setServerKeyAndCert_P(rsakey, sizeof(rsakey), x509, sizeof(x509));
  httpUpdater.setup(&httpServer, update_path, update_username, update_password);
  httpServer.begin();

  MDNS.addService("https", "tcp", 443);
  Serial.printf("HTTPSUpdateServer ready!\nOpen https://%s.local%s in "\
                "your browser and login with username '%s' and password "\
                "'%s'\n", host, update_path, update_username, update_password);
}

void loop() {
  httpServer.handleClient();
  MDNS.update();
}
