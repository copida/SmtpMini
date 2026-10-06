#include "SmtpMini.h"

SmtpMini::SmtpMini(WiFiClientSecure& client) {
  _client = &client;
  _lastErrorCode = 0;
}

bool SmtpMini::begin(const char* email, const char* appPassword, fs::FS *fileSystem) {
  _email = email;
  _appPassword = appPassword;
  _fsDevice = fileSystem;
  _lastErrorCode = 0;
  return true;
}

int SmtpMini::getLastError() const {
  return _lastErrorCode;
}

// Legge e codifica dalla RAM
void SmtpMini::codifica64(const char* buffer, size_t lunghezza) {

  bool formatoMIME = true;
  size_t i = 0;
  int charCount = 0;
  char rigaBuffer[79];
#ifdef SMTP_MINI_DEBUG
  Serial.print("Elaborazione e invio dati da RAM... ");
#endif
  if (lunghezza == 0) {
    formatoMIME = false;
    lunghezza = strlen(buffer);
  }

  while (i < lunghezza) {
    size_t rimanenti = lunghezza - i;
    uint8_t b1 = buffer[i++];
    uint8_t b2 = (rimanenti > 1) ? buffer[i++] : 0;
    uint8_t b3 = (rimanenti > 2) ? buffer[i++] : 0;

    rigaBuffer[charCount++] = b64_table[b1 >> 2];
    rigaBuffer[charCount++] = b64_table[((b1 & 0x03) << 4) | (b2 >> 4)];
    rigaBuffer[charCount++] = (rimanenti > 1) ? b64_table[((b2 & 0x0F) << 2) | (b3 >> 6)] : '=';
    rigaBuffer[charCount++] = (rimanenti > 2) ? b64_table[b3 & 0x3F] : '=';

    if (formatoMIME && charCount >= 76) {
      rigaBuffer[charCount++] = '\r';
      rigaBuffer[charCount++] = '\n';
      _client->write((const uint8_t*)rigaBuffer, charCount);
      charCount = 0;
    }
  }
  // Inviamo i caratteri rimanenti
  if (charCount > 0) {
    if (!formatoMIME) {
      rigaBuffer[charCount++] = '\r';
      rigaBuffer[charCount++] = '\n';
    } else {
      rigaBuffer[charCount++] = '\r';
      rigaBuffer[charCount++] = '\n';
    }
    _client->write((const uint8_t*)rigaBuffer, charCount);
  }

  if (formatoMIME) {
    _client->print("\r\n");
  }
  //Serial.println("Fatto!");
}

// Legge e codifica da SCHEDA SD (3 byte alla volta = consumo RAM zero)
void SmtpMini::codificaFILE(char* percorso) {

if (_fsDevice == nullptr) {
#ifdef SMTP_MINI_DEBUG
    Serial.println("Errore: Nessun File System specificato nel metodo begin().");
#endif
    return;
  }

#ifdef FS_DRV
  fs::File f = _fsDevice->open(percorso, FILE_READ);
  if (!f) {
    Serial.printf("Errore: Impossibile aprire il file su SD:%s \n", percorso);
    return;
  }

  uint8_t inputBuffer[3];
  int charCount = 0;
  char rigaBuffer[79];

  size_t totaleByte = f.size();
  size_t byteLetti = 0;
  int ultimaPercentuale = -1;

  while (f.available()) {
    int bytesRead = f.read(inputBuffer, 3);
    if (bytesRead > 0) {

      byteLetti += bytesRead;

      // Calcolo percentuale avanzamento
      int percentuale = (byteLetti * 100) / totaleByte;
#ifdef SMTP_MINI_DEBUG
      if (percentuale % 5 == 0 && percentuale != ultimaPercentuale) {
        Serial.print("#");
        ultimaPercentuale = percentuale;
      }
#endif
      rigaBuffer[charCount++] = b64_table[inputBuffer[0] >> 2];
      rigaBuffer[charCount++] = b64_table[((inputBuffer[0] & 0x03) << 4) | ((bytesRead > 1 ? inputBuffer[1] : 0) >> 4)];
      rigaBuffer[charCount++] = (bytesRead > 1) ? b64_table[((inputBuffer[1] & 0x0F) << 2) | ((bytesRead > 2 ? inputBuffer[2] : 0) >> 6)] : '=';
      rigaBuffer[charCount++] = (bytesRead > 2) ? b64_table[inputBuffer[2] & 0x3F] : '=';

      if (charCount >= 76) {
        rigaBuffer[charCount++] = '\r';
        rigaBuffer[charCount++] = '\n';
        _client->write((const uint8_t*)rigaBuffer, charCount);
        charCount = 0;
      }
    }
  }
  if (charCount > 0) {
    rigaBuffer[charCount++] = '\r';
    rigaBuffer[charCount++] = '\n';
    _client->write((const uint8_t*)rigaBuffer, charCount);
  }

  f.close();
  _client->print("\r\n");
#ifdef SMTP_MINI_DEBUG
  Serial.println("] 100% - Completato!");
#endif
#endif
}

bool SmtpMini::_waitForResponse(const char* expectedCode) {
  unsigned long timeout;
  String response = "";
  bool success = false;

  do {
    timeout = millis();
    while (!_client->available()) {
      if (millis() - timeout > 8000) {
        _lastErrorCode = -1;  // -1 significa TIMEOUT di rete
#ifdef SMTP_MINI_DEBUG
        Serial.println("<- [ERRORE LIBRERIA] Timeout risposta server.");
#endif
        return false;
      }
      delay(10);
    }
    response = _client->readStringUntil('\n');
    response.trim();

#ifdef SMTP_MINI_DEBUG
    Serial.println("<- SERVER: " + response);
#endif

    if (response.length() >= 3) {
      _lastErrorCode = response.substring(0, 3).toInt();
    }

    if (response.startsWith(expectedCode)) success = true;
  } while (response.length() > 3 && response.charAt(3) == '-');

  return success;
}

bool SmtpMini::sendEmail(const char* to, const char* subject, const char* body, bool isHtml, SMTPAttachment* attachments, size_t attachmentCount) {
  _lastErrorCode = 0;

#ifdef SMTP_MINI_DEBUG
  Serial.println("\n--- Connessione a :smtp.gmail.com ---");
#endif

  if (!_client->connect("smtp.gmail.com", 465)) {
    _lastErrorCode = -2;  // -2 significa Errore di Connessione TCP/SSL
    return false;
  }
  if (!_waitForResponse("220")) return false;

  _client->print("EHLO localhost\r\n");
  if (!_waitForResponse("250")) return false;

  _client->print("AUTH LOGIN\r\n");
  if (!_waitForResponse("334")) return false;

  //_client->print(_toBase64(_email) + "\r\n");
  codifica64(_email);
  if (!_waitForResponse("334")) return false;

  //_client->print(_toBase64(_appPassword) + "\r\n");
  codifica64(_appPassword);
  if (!_waitForResponse("235")) return false;

  _client->printf("MAIL FROM:<%s>\r\n", _email);
  if (!_waitForResponse("250")) return false;

  _client->printf("RCPT TO:<%s>\r\n", to);
  if (!_waitForResponse("250")) return false;

  _client->print("DATA\r\n");
  if (!_waitForResponse("354")) return false;

  const char* boundary = "----ArduinoBoundary12345";
  _client->printf("From: <%s>\r\n", _email);
  _client->printf("To: <%s>\r\n", to);
  _client->printf("Subject: %s\r\n", subject);
  _client->print("MIME-Version: 1.0\r\n");
  _client->printf("Content-Type: multipart/mixed; boundary=\"%s\"\r\n\r\n", boundary);

  _client->printf("--%s\r\n", boundary);
  if (isHtml) {
    _client->print("Content-Type: text/html; charset=UTF-8\r\n\r\n");
  } else {
    _client->print("Content-Type: text/plain; charset=UTF-8\r\n\r\n");
  }
  _client->printf("%s\r\n\r\n", body);

  // Ciclo dinamico degli allegati ****** ALLEGATI
  if (attachments != nullptr && attachmentCount > 0) {
    for (size_t i = 0; i < attachmentCount; i++) {
      SMTPAttachment corrente = attachments[i];

      char nomedamostrare[20];
      if (corrente.nomeFile[0] == '/') {
        snprintf(nomedamostrare, 20, "%s", &corrente.nomeFile[1]);
      } else {
        snprintf(nomedamostrare, 20, "%s", corrente.nomeFile);
      }
     
      _client->printf("--%s\r\n", boundary);

      _client->printf("Content-Type: application/octet-stream; name=\"%s\"\r\n", nomedamostrare);
      _client->print("Content-Transfer-Encoding: base64\r\n");
      _client->printf("Content-Disposition: attachment; filename=\"%s\"\r\n\r\n", nomedamostrare);

      // Scelta automatica della sorgente dati in base ai parametri
      if (corrente.bufferRAM != nullptr && corrente.lunghezzaRAM > 0) {
#ifdef SMTP_MINI_DEBUG
        Serial.printf("Invio allegato da RAM:%s\n", corrente.nomeFile);
#endif
        codifica64(corrente.bufferRAM, corrente.lunghezzaRAM);
      } else {
#ifdef SMTP_MINI_DEBUG
        Serial.printf("Invio allegato da SD:%s\n", corrente.nomeFile);
#endif
        codificaFILE(corrente.nomeFile);
      }
    }
  }
  //*************
  _client->printf("--%s--\r\n", boundary);
  _client->print(".\r\n");
  if (!_waitForResponse("250")) return false;

  _client->print("QUIT\r\n");
  _waitForResponse("221");

  _client->stop();
  return true;
}
