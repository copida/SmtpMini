#include "SmtpMini.h"

#ifdef SMTP_USE_STARTTLS
	SmtpMini::SmtpMini(ESP_SSLClient& client) {
		_client = &client;
		_lastErrorCode = 0;
		_fsDevice = nullptr;
	}
	#else
	SmtpMini::SmtpMini(WiFiClientSecure& client) {
		_client = &client;
		_lastErrorCode = 0;
		_fsDevice = nullptr;
	}
#endif

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

int SmtpMini::getLastError() const {
  return _lastErrorCode;
}

// Legge e codifica
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
		//Serial.print(F("Codifica e invio dati..."));
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
			Serial.println(F("Errore: Nessun File System specificato nel metodo begin()."));
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
		Serial.print(F("Elaborazione e invio file da SD... "));
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
					Serial.print(F("#"));
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
		Serial.println(F("] 100% - Completato!"));
	#endif
}


// Legge una riga SMTP fino a LF, con timeout complessivo.
bool SmtpMini::readSMTPLine(char* line, size_t size,uint32_t timeoutMs) {
  size_t n = 0;
  uint32_t start = millis();
	
  while ((uint32_t)(millis() - start) < timeoutMs) {
    while (_client->available() > 0) {
      int ch = _client->read();
      if (ch < 0)
			break;
      if (ch == '\n') {
        line[n] = '\0';
				#ifdef SMTP_MINI_DEBUG
					Serial.printf("S: %s\n", line);
				#endif
        return true;
			}
      if (ch != '\r' && n < size - 1)
			line[n++] = (char)ch;
		}
		
    if (!_client->connected()) {
      Serial.println(F("Connessione SMTP chiusa"));
      return false;
		}
		
    delay(1);
	}
	
  line[n] = '\0';
  Serial.printf("TIMEOUT SMTP, riga parziale: [%s]\n", line);
  return false;
}


// Legge la risposta SMTP, anche multilinea.
int SmtpMini::readSMTPResponse() {
  char line[512];
	
  while (true) {
    if (!readSMTPLine(line, sizeof(line)))
		return -1;
		
    if (strlen(line) < 4)
		return -1;
		
    if (line[3] == ' ') {
      line[3] = '\0';
      return atoi(line);
		}
		
    if (line[3] != '-')
		return -1;
	}
}

bool SmtpMini::smtpCommand(const char* command, int expected) {
	#ifdef SMTP_MINI_DEBUG
		Serial.print(F("C: "));
		Serial.println(command);
	#endif
  _client->print(command);
  _client->print("\r\n");
	
  int code = readSMTPResponse();
	
  if (code != expected) {
		_lastErrorCode = code;
    Serial.printf("ERRORE SMTP: atteso %d, ricevuto %d\n", expected, code);
    return false;
	}
  return true;
}


bool SmtpMini::sendEmail(const char* to, const char* subject, const char* body, bool isHtml, SMTPAttachment* attachments, size_t attachmentCount) {
  _lastErrorCode = 0;
	
	#ifdef SMTP_MINI_DEBUG
		#ifdef SMTP_USE_STARTTLS
			Serial.printf("\n--- Connessione STARTTLS (Porta 587) a %s ---\n", _host);
			#else
			Serial.printf("\n--- Connessione SSL/TLS (Porta 465) a %s ---\n", _host);
		#endif
	#endif
	
	_client->setInsecure(); // Ignora i controlli della CA per i test
	
	#ifdef SMTP_USE_STARTTLS
		// Buffer TLS: regolabili in base alla RAM disponibile.
		_client->setBufferSizes(4096, 1024);
		_client->setDebugLevel(1);
		
		// IMPORTANTE:
		// il wrapper deve iniziare in modalità TCP non cifrata.
		_client->setClient(&basic_client, false);
	#endif
	
	
	// Connessione TCP (se PlainStart è false, esegue anche l'handshake SSL in automatico)
	if (!_client->connect(_host, _port)) {
		_lastErrorCode = -2;  
		Serial.println(F("NON CONNESSO!!!!!!"));
		return false;
	}
	Serial.println(F("CONNESSO"));
	
	// 2. Saluto del server
  if (readSMTPResponse() != 220)
	return false;
	
  // 3. EHLO in chiaro
  if (!smtpCommand("EHLO esp32.local", 250))
	return false;
	
	// fino qui tutto in chiaro ...dopo TLS
	
	
	#ifdef SMTP_USE_STARTTLS
	 // 4. Richiesta STARTTLS
  if (!smtpCommand("STARTTLS", 220))
    return false;

  // 5. Upgrade TLS sullo stesso socket TCP
	#ifdef SMTP_MINI_DEBUG
  Serial.println(F("Avvio handshake STARTTLS..."));
#endif

  if (!_client->connectSSL()) {
    char err[160] = { 0 };
    int errCode = _client->getLastSSLError(err, sizeof(err));

    Serial.printf("Handshake TLS fallito: %d, %s\n", errCode, err);
    return false;
  }
#ifdef SMTP_MINI_DEBUG
  Serial.println(F("TLS attivo"));
#endif
  // 6. Nuovo EHLO dentro TLS
  if (!smtpCommand("EHLO esp32.local", 250))
    return false;
#endif
	
	// =========================================================================
	// DA QUI IN POI IL CODICE È IDENTICO PER ENTRAMBI I PROTOCOLLI
	// =========================================================================
	
	// Autenticazione (AUTH LOGIN)
	 // 7. Autenticazione LOGIN
  if (!smtpCommand("AUTH LOGIN", 334)) return false;
		
	codifica64((const uint8_t*)_email, strlen(_email), false);
	 if (readSMTPResponse() != 334)return false;
	
	codifica64((const uint8_t*)_appPassword, strlen(_appPassword), false);
	if (readSMTPResponse() != 235)return false;
	
	#ifdef SMTP_MINI_DEBUG
	Serial.println(F("Autenticazione riuscita"));
	#endif
	
	_client->printf("MAIL FROM:<%s>\r\n", _email);
	if (readSMTPResponse() != 250)return false;
		
	_client->printf("RCPT TO:<%s>\r\n", to);
	if (readSMTPResponse() != 250)return false;
	
	if (!smtpCommand("DATA", 354)) return false;

	
	const char* boundary = "----ESP32Boundary12345";
	_client->printf("From: <%s>\r\n", _email);
	_client->printf("To: <%s>\r\n", to);
	_client->printf("Subject: %s\r\n", subject);
	#ifdef ENABLE_TIMEINFO
	if(_timeSMTP != nullptr){_client->printf("Date: %s\r\n", _timeSMTP); }
		//Wed, 14 Oct 2026 08:14:13 +0200
		//strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S %z", &timeinfo);
	#endif
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
				} else if(strlen(corrente.nomeFile) > 0){
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
	if (readSMTPResponse() != 250) return false;
	
	smtpCommand("QUIT", 221);
	
	_client->stop();
	return true;
}
