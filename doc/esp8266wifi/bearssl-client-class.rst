BearSSL as TLS 1.2 (SSL) Client and Server

BearSSL is a modern implemention of TLS 1.2 optimized for security and low memory requirements.  The underlying library is written by Thomas Pornin and available at https://www.bearssl.org.  The version running on the ESP8266 has been optimized for the unique heap and stack limitations present on this chip, with additional helper routines added to support our use cases.  It can be built from the main Arduino source tree under tools/sdk/ssl/bearssl, but you do not need to do this unless you're interested in working on the low-level details.

The BearSSL::WiFiClientSecure class is a wrapper around the raw BearSSL library, and part of the main ESP8266 Arduino core now.  In the future it will be the main supported SSL implementation, with the older unsupported axTLS eventually being deprecated.

Why use BearSSL over older axTLS based WiFiClientSecure
Bidirectionality
Many modern ciphers
Ongoing Support
Maximum Fragment Length Negotiation and support
No memory allocations at runtime


Using BearSSL in your application

If you're familiar with the standard WiFiClientSecure and WiFiClient, then BearSSL::WiFiClientSecure should be very simple to use.

There are many examples available to see working code, but the process is generally:
Create BearSSL::WiFiClientSecure object
-> You may want to place this object on the heap (i.e. "new") because it can be very large and the Arduino heap is very small.
Select a X509 certificate validation mode
-> Unlike axTLS which, by default, does not verify any X509 certificates, BearSSL will always attempt to validate the server certificate.  If you do not select a mode (see below for different modes) or use the setInsecure() method, BearSSL will not connect to a server.  This is by design, not a bug, as without some method to verify the X509 server certificate then you have no guarantee that you are not talking to a man-in-the-middle (MITM) and having traffic intercepted or modified.
Optionally attach client certificates
-> If you are connection to an MQTT server, for example, that requires client certificates as authentication, you can add the certificate and key to your client and it will automatically send it when requested.
Optionally set up a persistent SSL session storage
-> When reconnecting to the same SSL server repeatedly, SSL sessions (when supported by the server) let you avoid a large portion of the SSL handshake time on a reconnect by caching the SSL state when a connection is terminated.
Connect
-> On failure there is an API which will give both an integer and human readable string describing the error encountered.
Use the connection
-> Just like a WiFiClient, send and receive as needed



Converting from axTLS to BearSSL


Memory Usage

Initial BearSSL memory usage will be higher than AXTLS SSL for several reasons, but


Speed Concerns

SSL Sessions

Certificate Concepts

Client Certificates and Keys

X509Lists

CertStores

When you connect to a website from your PC or phone, you don't need to manyally specify a different certificate for every website you visit.  That is because

Authentication Modes
BearSSL provides a large selection of verifying the server you're connecting to is the one you think you're connecting to.

As a fallback option, when you do not care about MITM attacks or verifying server identity, you can use this option to allow SSL connections without checking any X509 information.

Cipher Selection

