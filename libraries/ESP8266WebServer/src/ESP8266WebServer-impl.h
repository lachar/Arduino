/*
  ESP8266WebServer.cpp - Dead simple web-server.
  Supports only one simultaneous client, knows how to handle GET and POST.

  Copyright (c) 2014 Ivan Grokhotkov. All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
  Modified 8 May 2015 by Hristo Gochkov (proper post and file upload handling)
*/


#include <Arduino.h>
#include <libb64/cencode.h>
#include "WiFiServer.h"
#include "WiFiClient.h"
#include "ESP8266WebServer.h"
#include "FS.h"
#include "detail/RequestHandlersImpl.h"

//#define DEBUG_ESP_HTTP_SERVER
#ifdef DEBUG_ESP_PORT
#define DEBUG_OUTPUT DEBUG_ESP_PORT
#else
#define DEBUG_OUTPUT Serial
#endif

static const char AUTHORIZATION_HEADER[] PROGMEM = "Authorization";
static const char qop_auth[] PROGMEM = "qop=auth";
static const char WWW_Authenticate[] PROGMEM = "WWW-Authenticate";
static const char Content_Length[] PROGMEM = "Content-Length";


template <class ServerClass, class ClientClass>
ESP8266WebServerTemplate<ServerClass, ClientClass>::ESP8266WebServerTemplate(IPAddress addr, int port)
: _server(addr, port)
, _currentMethod(HTTP_ANY)
, _currentVersion(0)
, _currentStatus(HC_NONE)
, _statusChange(0)
, _currentHandler(nullptr)
, _firstHandler(nullptr)
, _lastHandler(nullptr)
, _currentArgCount(0)
, _currentArgs(nullptr)
, _headerKeysCount(0)
, _currentHeaders(nullptr)
, _contentLength(0)
, _chunked(false)
{
}

template <class ServerClass, class ClientClass>
ESP8266WebServerTemplate<ServerClass, ClientClass>::ESP8266WebServerTemplate(int port)
: _server(port)
, _currentMethod(HTTP_ANY)
, _currentVersion(0)
, _currentStatus(HC_NONE)
, _statusChange(0)
, _currentHandler(nullptr)
, _firstHandler(nullptr)
, _lastHandler(nullptr)
, _currentArgCount(0)
, _currentArgs(nullptr)
, _headerKeysCount(0)
, _currentHeaders(nullptr)
, _contentLength(0)
, _chunked(false)
{
}

template <class ServerClass, class ClientClass>
ESP8266WebServerTemplate<ServerClass, ClientClass>::~ESP8266WebServerTemplate() {
  _server.close();
  if (_currentHeaders)
    delete[]_currentHeaders;
  RequestHandler<ServerClass, ClientClass>* handler = _firstHandler;
  while (handler) {
    RequestHandler<ServerClass, ClientClass>* next = handler->next();
    delete handler;
    handler = next;
  }
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::begin() {
  close();
  _server.begin();
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::begin(uint16_t port) {
  close();
  _server.begin(port);
}

template <class ServerClass, class ClientClass>
String ESP8266WebServerTemplate<ServerClass, ClientClass>::_extractParam(String& authReq,const String& param,const char delimit){
  int _begin = authReq.indexOf(param);
  if (_begin == -1) 
    return "";
  return authReq.substring(_begin+param.length(),authReq.indexOf(delimit,_begin+param.length()));
}

template <class ServerClass, class ClientClass>
bool ESP8266WebServerTemplate<ServerClass, ClientClass>::authenticate(const char * username, const char * password){
  if(hasHeader(FPSTR(AUTHORIZATION_HEADER))) {
    String authReq = header(FPSTR(AUTHORIZATION_HEADER));
    if(authReq.startsWith(("Basic"))){
      authReq = authReq.substring(6);
      authReq.trim();
      char toencodeLen = strlen(username)+strlen(password)+1;
      char *toencode = new char[toencodeLen + 1];
      if(toencode == NULL){
        authReq = "";
        return false;
      }
      char *encoded = new char[base64_encode_expected_len(toencodeLen)+1];
      if(encoded == NULL){
        authReq = "";
        delete[] toencode;
        return false;
      }
      sprintf(toencode, "%s:%s", username, password);
      if(base64_encode_chars(toencode, toencodeLen, encoded) > 0 && authReq.equalsConstantTime(encoded)) {
        authReq = "";
        delete[] toencode;
        delete[] encoded;
        return true;
      }
      delete[] toencode;
      delete[] encoded;
    } else if(authReq.startsWith(("Digest"))) {
      authReq = authReq.substring(7);
      #ifdef DEBUG_ESP_HTTP_SERVER
      DEBUG_OUTPUT.println(authReq);
      #endif
      String _username = _extractParam(authReq,("username=\""));
      if(!_username.length() || _username != String(username)) {
        authReq = "";
        return false;
      }
      // extracting required parameters for RFC 2069 simpler Digest
      String _realm    = _extractParam(authReq, ("realm=\""));
      String _nonce    = _extractParam(authReq, ("nonce=\""));
      String _uri      = _extractParam(authReq, ("uri=\""));
      String _response = _extractParam(authReq, ("response=\""));
      String _opaque   = _extractParam(authReq, ("opaque=\""));

      if((!_realm.length()) || (!_nonce.length()) || (!_uri.length()) || (!_response.length()) || (!_opaque.length())) {
        authReq = "";
        return false;
      }
      if((_opaque != _sopaque) || (_nonce != _snonce) || (_realm != _srealm)) {
        authReq = "";
        return false;
      }
      // parameters for the RFC 2617 newer Digest
      String _nc,_cnonce;
      if(authReq.indexOf(FPSTR(qop_auth)) != -1) {
        _nc = _extractParam(authReq, ("nc="), ',');
        _cnonce = _extractParam(authReq, ("cnonce=\""));
      }
      MD5Builder md5;
      md5.begin();
      md5.add(String(username) + ':' + _realm + ':' + String(password));  // md5 of the user:realm:user
      md5.calculate();
      String _H1 = md5.toString();
      #ifdef DEBUG_ESP_HTTP_SERVER
      DEBUG_OUTPUT.println("Hash of user:realm:pass=" + _H1);
      #endif
      md5.begin();
      if(_currentMethod == HTTP_GET){
        md5.add(String(("GET:")) + _uri);
      }else if(_currentMethod == HTTP_POST){
        md5.add(String(("POST:")) + _uri);
      }else if(_currentMethod == HTTP_PUT){
        md5.add(String(("PUT:")) + _uri);
      }else if(_currentMethod == HTTP_DELETE){
        md5.add(String(("DELETE:")) + _uri);
      }else{
        md5.add(String(("GET:")) + _uri);
      }
      md5.calculate();
      String _H2 = md5.toString();
      #ifdef DEBUG_ESP_HTTP_SERVER
      DEBUG_OUTPUT.println("Hash of GET:uri=" + _H2);
      #endif
      md5.begin();
      if(authReq.indexOf(FPSTR(qop_auth)) != -1) {
        md5.add(_H1 + ':' + _nonce + ':' + _nc + ':' + _cnonce + (":auth:") + _H2);
      } else {
        md5.add(_H1 + ':' + _nonce + ':' + _H2);
      }
      md5.calculate();
      String _responsecheck = md5.toString();
      #ifdef DEBUG_ESP_HTTP_SERVER
      DEBUG_OUTPUT.println("The Proper response=" +_responsecheck);
      #endif
      if(_response == _responsecheck){
        authReq = "";
        return true;
      }
    }
    authReq = "";
  }
  return false;
}

template <class ServerClass, class ClientClass>
String ESP8266WebServerTemplate<ServerClass, ClientClass>::_getRandomHexString() {
  char buffer[33];  // buffer to hold 32 Hex Digit + /0
  int i;
  for(i = 0; i < 4; i++) {
    sprintf (buffer + (i*8), "%08x", RANDOM_REG32);
  }
  return String(buffer);
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::requestAuthentication(HTTPAuthMethod mode, const char* realm, const String& authFailMsg) {
  if(realm == NULL) {
    _srealm = String(("Login Required"));
  } else {
    _srealm = String(realm);
  }
  if(mode == BASIC_AUTH) {
    sendHeader(String(FPSTR(WWW_Authenticate)), String(("Basic realm=\"")) + _srealm + String(("\"")));
  } else {
    _snonce=_getRandomHexString();
    _sopaque=_getRandomHexString();
    sendHeader(String(FPSTR(WWW_Authenticate)), String(("Digest realm=\"")) +_srealm + String(("\", qop=\"auth\", nonce=\"")) + _snonce + String(("\", opaque=\"")) + _sopaque + String(("\"")));
  }
  using namespace mime;
  send(401, String(FPSTR(mimeTable[html].mimeType)), authFailMsg);
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::on(const String &uri, ESP8266WebServerTemplate<ServerClass, ClientClass>::THandlerFunction handler) {
  on(uri, HTTP_ANY, handler);
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::on(const String &uri, HTTPMethod method, ESP8266WebServerTemplate<ServerClass, ClientClass>::THandlerFunction fn) {
  on(uri, method, fn, _fileUploadHandler);
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::on(const String &uri, HTTPMethod method, ESP8266WebServerTemplate<ServerClass, ClientClass>::THandlerFunction fn, ESP8266WebServerTemplate<ServerClass, ClientClass>::THandlerFunction ufn) {
  _addRequestHandler(new FunctionRequestHandler<ServerClass, ClientClass>(fn, ufn, uri, method));
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::addHandler(RequestHandler<ServerClass, ClientClass>* handler) {
    _addRequestHandler(handler);
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::_addRequestHandler(RequestHandler<ServerClass, ClientClass>* handler) {
    if (!_lastHandler) {
      _firstHandler = handler;
      _lastHandler = handler;
    }
    else {
      _lastHandler->next(handler);
      _lastHandler = handler;
    }
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::serveStatic(const char* uri, FS& fs, const char* path, const char* cache_header) {
    _addRequestHandler(new StaticRequestHandler<ServerClass, ClientClass>(fs, path, uri, cache_header));
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::handleClient() {
  if (_currentStatus == HC_NONE) {
    ClientClass client = _server.available();
    if (!client) {
      return;
    }

#ifdef DEBUG_ESP_HTTP_SERVER
    DEBUG_OUTPUT.println("New client");
#endif

    _currentClient = client;
    _currentStatus = HC_WAIT_READ;
    _statusChange = millis();
  }

  bool keepCurrentClient = false;
  bool callYield = false;

  if (_currentClient.connected()) {
    switch (_currentStatus) {
    case HC_NONE:
      // No-op to avoid C++ compiler warning
      break;
    case HC_WAIT_READ:
      // Wait for data from client to become available
      if (_currentClient.available()) {
        if (_parseRequest(_currentClient)) {
          _currentClient.setTimeout(HTTP_MAX_SEND_WAIT);
          _contentLength = CONTENT_LENGTH_NOT_SET;
          _handleRequest();

          if (_currentClient.connected()) {
            _currentStatus = HC_WAIT_CLOSE;
            _statusChange = millis();
            keepCurrentClient = true;
          }
        }
      } else { // !_currentClient.available()
        if (millis() - _statusChange <= HTTP_MAX_DATA_WAIT) {
          keepCurrentClient = true;
        }
        callYield = true;
      }
      break;
    case HC_WAIT_CLOSE:
      // Wait for client to close the connection
      if (millis() - _statusChange <= HTTP_MAX_CLOSE_WAIT) {
        keepCurrentClient = true;
        callYield = true;
      }
    }
  }

  if (!keepCurrentClient) {
    _currentClient = ClientClass();
    _currentStatus = HC_NONE;
    _currentUpload.reset();
  }

  if (callYield) {
    yield();
  }
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::close() {
  _server.close();
  _currentStatus = HC_NONE;
  if(!_headerKeysCount)
    collectHeaders(0, 0);
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::stop() {
  close();
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::sendHeader(const String& name, const String& value, bool first) {
  String headerLine = name;
  headerLine += (": ");
  headerLine += value;
  headerLine += "\r\n";

  if (first) {
    _responseHeaders = headerLine + _responseHeaders;
  }
  else {
    _responseHeaders += headerLine;
  }
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::setContentLength(const size_t contentLength) {
    _contentLength = contentLength;
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::_prepareHeader(String& response, int code, const char* content_type, size_t contentLength) {
    response = String(("HTTP/1.")) + String(_currentVersion) + ' ';
    response += String(code);
    response += ' ';
    response += _responseCodeToString(code);
    response += "\r\n";

    using namespace mime;
    if (!content_type)
        content_type = mimeTable[html].mimeType;

    sendHeader(String(("Content-Type")), String(FPSTR(content_type)), true);
    if (_contentLength == CONTENT_LENGTH_NOT_SET) {
        sendHeader(String(FPSTR(Content_Length)), String(contentLength));
    } else if (_contentLength != CONTENT_LENGTH_UNKNOWN) {
        sendHeader(String(FPSTR(Content_Length)), String(_contentLength));
    } else if(_contentLength == CONTENT_LENGTH_UNKNOWN && _currentVersion){ //HTTP/1.1 or above client
      //let's do chunked
      _chunked = true;
      sendHeader(String(("Accept-Ranges")),String(("none")));
      sendHeader(String(("Transfer-Encoding")),String(("chunked")));
    }
    sendHeader(String(("Connection")), String(("close")));

    response += _responseHeaders;
    response += "\r\n";
    _responseHeaders = "";
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::send(int code, const char* content_type, const String& content) {
    String header;
    // Can we asume the following?
    //if(code == 200 && content.length() == 0 && _contentLength == CONTENT_LENGTH_NOT_SET)
    //  _contentLength = CONTENT_LENGTH_UNKNOWN;
    _prepareHeader(header, code, content_type, content.length());
    _currentClient.write((const uint8_t *)header.c_str(), header.length());
    if(content.length())
      sendContent(content);
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::send_P(int code, PGM_P content_type, PGM_P content) {
    size_t contentLength = 0;

    if (content != NULL) {
        contentLength = strlen_P(content);
    }

    String header;
    char type[64];
    memccpy_P((void*)type, (PGM_VOID_P)content_type, 0, sizeof(type));
    _prepareHeader(header, code, (const char* )type, contentLength);
    _currentClient.write((const uint8_t *)header.c_str(), header.length());
    sendContent_P(content);
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::send_P(int code, PGM_P content_type, PGM_P content, size_t contentLength) {
    String header;
    char type[64];
    memccpy_P((void*)type, (PGM_VOID_P)content_type, 0, sizeof(type));
    _prepareHeader(header, code, (const char* )type, contentLength);
    sendContent(header);
    sendContent_P(content, contentLength);
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::send(int code, char* content_type, const String& content) {
  send(code, (const char*)content_type, content);
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::send(int code, const String& content_type, const String& content) {
  send(code, (const char*)content_type.c_str(), content);
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::sendContent(const String& content) {
  const char * footer = "\r\n";
  size_t len = content.length();
  if(_chunked) {
    char * chunkSize = (char *)malloc(11);
    if(chunkSize){
      sprintf(chunkSize, "%x%s", len, footer);
      _currentClient.write((const uint8_t *)chunkSize, strlen(chunkSize));
      free(chunkSize);
    }
  }
  _currentClient.write((const uint8_t *)content.c_str(), len);
  if(_chunked){
    _currentClient.write((const uint8_t *)footer, 2);
    if (len == 0) {
      _chunked = false;
    }
  }
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::sendContent_P(PGM_P content) {
  sendContent_P(content, strlen_P(content));
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::sendContent_P(PGM_P content, size_t size) {
  const char * footer = "\r\n";
  if(_chunked) {
    char * chunkSize = (char *)malloc(11);
    if(chunkSize){
      sprintf(chunkSize, "%x%s", size, footer);
      _currentClient.write((const uint8_t *)chunkSize, strlen(chunkSize));
      free(chunkSize);
    }
  }
  _currentClient.write_P(content, size);
  if(_chunked){
    _currentClient.write((const uint8_t *)footer, 2);
    if (size == 0) {
      _chunked = false;
    }
  }
}


template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::_streamFileCore(const size_t fileSize, const String & fileName, const String & contentType)
{
  using namespace mime;
  setContentLength(fileSize);
  if (fileName.endsWith(String(FPSTR(mimeTable[gz].endsWith))) &&
      contentType != String(FPSTR(mimeTable[gz].mimeType)) &&
      contentType != String(FPSTR(mimeTable[none].mimeType))) {
    sendHeader(("Content-Encoding"), ("gzip"));
  }
  send(200, contentType, "");
}


template <class ServerClass, class ClientClass>
String ESP8266WebServerTemplate<ServerClass, ClientClass>::arg(String name) {
  for (int i = 0; i < _currentArgCount; ++i) {
    if ( _currentArgs[i].key == name )
      return _currentArgs[i].value;
  }
  return "";
}

template <class ServerClass, class ClientClass>
String ESP8266WebServerTemplate<ServerClass, ClientClass>::arg(int i) {
  if (i < _currentArgCount)
    return _currentArgs[i].value;
  return "";
}

template <class ServerClass, class ClientClass>
String ESP8266WebServerTemplate<ServerClass, ClientClass>::argName(int i) {
  if (i < _currentArgCount)
    return _currentArgs[i].key;
  return "";
}

template <class ServerClass, class ClientClass>
int ESP8266WebServerTemplate<ServerClass, ClientClass>::args() {
  return _currentArgCount;
}

template <class ServerClass, class ClientClass>
bool ESP8266WebServerTemplate<ServerClass, ClientClass>::hasArg(String  name) {
  for (int i = 0; i < _currentArgCount; ++i) {
    if (_currentArgs[i].key == name)
      return true;
  }
  return false;
}


template <class ServerClass, class ClientClass>
String ESP8266WebServerTemplate<ServerClass, ClientClass>::header(String name) {
  for (int i = 0; i < _headerKeysCount; ++i) {
    if (_currentHeaders[i].key.equalsIgnoreCase(name))
      return _currentHeaders[i].value;
  }
  return "";
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::collectHeaders(const char* headerKeys[], const size_t headerKeysCount) {
  _headerKeysCount = headerKeysCount + 1;
  if (_currentHeaders)
     delete[]_currentHeaders;
  _currentHeaders = new RequestArgument[_headerKeysCount];
  _currentHeaders[0].key = FPSTR(AUTHORIZATION_HEADER);
  for (int i = 1; i < _headerKeysCount; i++){
    _currentHeaders[i].key = headerKeys[i-1];
  }
}

template <class ServerClass, class ClientClass>
String ESP8266WebServerTemplate<ServerClass, ClientClass>::header(int i) {
  if (i < _headerKeysCount)
    return _currentHeaders[i].value;
  return "";
}

template <class ServerClass, class ClientClass>
String ESP8266WebServerTemplate<ServerClass, ClientClass>::headerName(int i) {
  if (i < _headerKeysCount)
    return _currentHeaders[i].key;
  return "";
}

template <class ServerClass, class ClientClass>
int ESP8266WebServerTemplate<ServerClass, ClientClass>::headers() {
  return _headerKeysCount;
}

template <class ServerClass, class ClientClass>
bool ESP8266WebServerTemplate<ServerClass, ClientClass>::hasHeader(String name) {
  for (int i = 0; i < _headerKeysCount; ++i) {
    if ((_currentHeaders[i].key.equalsIgnoreCase(name)) &&  (_currentHeaders[i].value.length() > 0))
      return true;
  }
  return false;
}

template <class ServerClass, class ClientClass>
String ESP8266WebServerTemplate<ServerClass, ClientClass>::hostHeader() {
  return _hostHeader;
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::onFileUpload(THandlerFunction fn) {
  _fileUploadHandler = fn;
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::onNotFound(THandlerFunction fn) {
  _notFoundHandler = fn;
}

template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::_handleRequest() {
  bool handled = false;
  if (!_currentHandler){
#ifdef DEBUG_ESP_HTTP_SERVER
    DEBUG_OUTPUT.println("request handler not found");
#endif
  }
  else {
    handled = _currentHandler->handle(*this, _currentMethod, _currentUri);
#ifdef DEBUG_ESP_HTTP_SERVER
    if (!handled) {
      DEBUG_OUTPUT.println("request handler failed to handle request");
    }
#endif
  }
  if (!handled && _notFoundHandler) {
    _notFoundHandler();
    handled = true;
  }
  if (!handled) {
    using namespace mime;
    send(404, String(FPSTR(mimeTable[html].mimeType)), String(("Not found: ")) + _currentUri);
    handled = true;
  }
  if (handled) {
    _finalizeResponse();
  }
  _currentUri = "";
}


template <class ServerClass, class ClientClass>
void ESP8266WebServerTemplate<ServerClass, ClientClass>::_finalizeResponse() {
  if (_chunked) {
    sendContent("");
  }
}

template <class ServerClass, class ClientClass>
String ESP8266WebServerTemplate<ServerClass, ClientClass>::_responseCodeToString(int code) {
  switch (code) {
    case 100: return ("Continue");
    case 101: return ("Switching Protocols");
    case 200: return ("OK");
    case 201: return ("Created");
    case 202: return ("Accepted");
    case 203: return ("Non-Authoritative Information");
    case 204: return ("No Content");
    case 205: return ("Reset Content");
    case 206: return ("Partial Content");
    case 300: return ("Multiple Choices");
    case 301: return ("Moved Permanently");
    case 302: return ("Found");
    case 303: return ("See Other");
    case 304: return ("Not Modified");
    case 305: return ("Use Proxy");
    case 307: return ("Temporary Redirect");
    case 400: return ("Bad Request");
    case 401: return ("Unauthorized");
    case 402: return ("Payment Required");
    case 403: return ("Forbidden");
    case 404: return ("Not Found");
    case 405: return ("Method Not Allowed");
    case 406: return ("Not Acceptable");
    case 407: return ("Proxy Authentication Required");
    case 408: return ("Request Time-out");
    case 409: return ("Conflict");
    case 410: return ("Gone");
    case 411: return ("Length Required");
    case 412: return ("Precondition Failed");
    case 413: return ("Request Entity Too Large");
    case 414: return ("Request-URI Too Large");
    case 415: return ("Unsupported Media Type");
    case 416: return ("Requested range not satisfiable");
    case 417: return ("Expectation Failed");
    case 500: return ("Internal Server Error");
    case 501: return ("Not Implemented");
    case 502: return ("Bad Gateway");
    case 503: return ("Service Unavailable");
    case 504: return ("Gateway Time-out");
    case 505: return ("HTTP Version not supported");
    default:  return ("");
  }
}

// AXTLS specialization templates
template <>
void ESP8266WebServerTemplate<axTLS::WiFiServerSecure, axTLS::WiFiClientSecure>::setServerKeyAndCert_P(const uint8_t *key, int keyLen, const uint8_t *cert, int certLen)
{
    _server.setServerKeyAndCert_P(key, keyLen, cert, certLen);
}

template <>
void ESP8266WebServerTemplate<axTLS::WiFiServerSecure, axTLS::WiFiClientSecure>::setServerKeyAndCert(const uint8_t *key, int keyLen, const uint8_t *cert, int certLen)
{
    _server.setServerKeyAndCert(key, keyLen, cert, certLen);
}

// BearSSL specialization templates
template<>
void ESP8266WebServerTemplate<BearSSL::WiFiServerSecure, BearSSL::WiFiClientSecure>::setServerKeyAndCert_P(const uint8_t *key, int keyLen, const uint8_t *cert, int certLen)
{
    _server.setServerKeyAndCert_P(key, keyLen, cert, certLen);
}

template<>
void ESP8266WebServerTemplate<BearSSL::WiFiServerSecure, BearSSL::WiFiClientSecure>::setServerKeyAndCert(const uint8_t *key, int keyLen, const uint8_t *cert, int certLen)
{
    _server.setServerKeyAndCert(key, keyLen, cert, certLen);
}

template<>
void ESP8266WebServerTemplate<BearSSL::WiFiServerSecure, BearSSL::WiFiClientSecure>::setBufferSizes(int recv, int xmit)
{
  _server.setBufferSizes(recv, xmit);
}

template<>
void ESP8266WebServerTemplate<BearSSL::WiFiServerSecure, BearSSL::WiFiClientSecure>::setRSACert(const BearSSLX509List *chain, const BearSSLPrivateKey *sk)
{
  _server.setRSACert(chain, sk);
}

template<>
void ESP8266WebServerTemplate<BearSSL::WiFiServerSecure, BearSSL::WiFiClientSecure>::setECCert(const BearSSLX509List *chain, unsigned cert_issuer_key_type, const BearSSLPrivateKey *sk)
{
  _server.setECCert(chain, cert_issuer_key_type, sk);
}

