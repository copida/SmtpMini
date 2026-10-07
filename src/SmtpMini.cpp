#include "SmtpMini.h"

SmtpMini::SmtpMini(WiFiClientSecure& client) {
  _client = &client;
  _lastErrorCode = 0;
}

bool SmtpMini::begin(const char* email, const char* appPassword, fs::FS* fileSystem) {
  _email = email;
  _appPassword = appPassword;
  _fsDevice = fileSystem;
  _lastErrorCode = 0;
  return true;
}

void SmtpMini::setSmtpServer(const char* host, uint16_t port) {
  _host = host;
  _port = port;
}

// In sendEmail:
// if (!_client->connect(_host, _port)) {

int SmtpMini::getLastError() const {
  return _lastErrorCode;
}

// Legge e codifica dalla RAM
void SmtpMini::codifica64(const uint8_t* data, size_t len, bool mime) {
  char riga[80];
  size_t c = 0;

  for (size_t i = 0; i < len; i += 3) {
    size_t rem = len - i;
    uint8_t b1 = data[i];
    uint8_t b2 = (rem > 1) ? data[i + 1] : 0;
    uint8_t b3 = (rem > 2) ? data[i + 2] : 0;

    riga[c++] = b64_table[b1 >> 2];
    riga[c++] = b64_table[((b1 & 0x03) << 4) | (b2 >> 4)];
    riga[c++] = (rem > 1) ? b64_table[((b2 & 0x0F) << 2) | (b3 >> 6)] : '=';
    riga[c++] = (rem > 2) ? b64_table[b3 & 0x3F] : '=';

    if (mime && c >= 76) {   // riga piena: la invio
      riga[c++] = '\r';
      riga[c++] = '\n';
      _client->write((const uint8_t*)riga, c);
      c = 0;
    }
  }

  if (c > 0) {               // resto finale
    riga[c++] = '\r';
    riga[c++] = '\n';
    _client->write((const uint8_t*)riga, c);
  }
}

void SmtpMini::codifica64(const char* buffer, size_t lunghezza) {
#ifdef SMTP_MINI_DEBUG
  Serial.print("Elaborazione e invio dati da RAM... ");
#endif

  if (lunghezza == 0) {
    lunghezza = strlen(buffer);
    codifica64((const uint8_t*)buffer, lunghezza, false);
    return;
  }

  codifica64((const uint8_t*)buffer, lunghezza, true);
}

static const char* nomeBase(const char* percorso) {
  const char* p = strrchr(percorso, '/');
  return p ? p + 1 : percorso;
}

void SmtpMini::codificaFILE(const char* percorso) {
  if (_fsDevice == nullptr) {
#ifdef SMTP_MINI_DEBUG
    Serial.println("Errore: Nessun File System specificato nel metodo begin().");
#endif
    return;
  }


  fs::File f = _fsDevice->open(percorso, FILE_READ);
  if (!f) {
#ifdef SMTP_MINI_DEBUG
    Serial.printf("Errore: Impossibile aprire il file: %s\n", percorso);
#endif
    return;
  }

  const size_t totale = f.size();
  if (totale == 0) {
    f.close();
    return;
  }

  uint8_t accumulo[57];
  uint8_t lettura[16];
  size_t accLen = 0;
  size_t byteLetti = 0;
  int ultimaPercentuale = -1;

#ifdef SMTP_MINI_DEBUG
  Serial.print("Elaborazione e invio file da SD... ");
#endif

  while (f.available()) {
    size_t n = f.read(lettura, sizeof(lettura));
    if (n == 0) break;

    for (size_t i = 0; i < n; i++) {
      accumulo[accLen++] = lettura[i];
      byteLetti++;

      if (accLen == sizeof(accumulo)) {
        codifica64((const uint8_t*)accumulo, accLen, true);
        accLen = 0;
      }

#ifdef SMTP_MINI_DEBUG
      int percentuale = (int)((byteLetti * 100ULL) / totale);
      if (percentuale != ultimaPercentuale && percentuale % 5 == 0) {
        ultimaPercentuale = percentuale;
        Serial.print("#");
      }
#endif
    }
  }

  if (accLen > 0) {
    codifica64((const uint8_t*)accumulo, accLen, true);
  }

  f.close();
  _client->print("\r\n");

#ifdef SMTP_MINI_DEBUG
  Serial.println("] 100% - Completato!");
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
        Serial.println("<- [ERRORE] Timeout risposta server.");
#endif
        _client->stop();
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

    if (response.startsWith(expectedCode)){
      success = true;
    } else {
      _client->stop();
    }
  } while (response.length() > 3 && response.charAt(3) == '-');

  return success;
}


bool SmtpMini::sendEmail(const char* to, const char* subject, const char* body, bool isHtml, SMTPAttachment* attachments, size_t attachmentCount) {
  _lastErrorCode = 0;

#ifdef SMTP_MINI_DEBUG
  Serial.println("\n--- Connessione a :smtp.gmail.com ---");
#endif

  if (!_client->connect(_host, _port)) {
  //if (!_client->connect("smtp.gmail.com", 465)) {
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

      const char* nomedamostrare = nomeBase(corrente.nomeFile);

      _client->printf("--%s\r\n", boundary);

      _client->printf("Content-Type: application/octet-stream; name=\"%s\"\r\n", nomedamostrare);
      _client->print("Content-Transfer-Encoding: base64\r\n");
      _client->printf("Content-Disposition: attachment; filename=\"%s\"\r\n\r\n", nomedamostrare);

      // Scelta automatica della sorgente dati in base ai parametri
      if (corrente.bufferRAM != nullptr && corrente.lunghezzaRAM > 0) {
#ifdef SMTP_MINI_DEBUG
        Serial.printf("Invio allegato da RAM:%s\n", corrente.nomeFile);
#endif
        codifica64((const uint8_t*)corrente.bufferRAM, corrente.lunghezzaRAM, true);
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
