#include "SmtpMini.h"

static constexpr char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static constexpr char boundary[] = "ESP32Block";

#ifdef SMTP_USE_STARTTLS
	SmtpMini::SmtpMini(ESP_SSLClient& client) {
		_client = &client;
		_lastErrorCode = 0;
		_fsDevice = nullptr;
		_certCA = nullptr;
	}
	#else
	SmtpMini::SmtpMini(WiFiClientSecure& client) {
		_client = &client;
		_lastErrorCode = 0;
		_fsDevice = nullptr;
		_certCA = nullptr;
	}
#endif

bool SmtpMini::begin(const char* email, const char* appPassword, fs::FS* fileSystem) {
  _email = email;
  _appPassword = appPassword;
  _fsDevice = fileSystem;
  _lastErrorCode = 0;
  return true;
}

void SmtpMini::setSmtpServer(const char* host, uint16_t port, const char* certCA) {
  _host = host;
  _port = port;
	_certCA = certCA;
}

int SmtpMini::getLastError() const {
  return _lastErrorCode;
}

// Legge e codifica
bool SmtpMini::codifica64(const uint8_t* data, size_t len, bool mime) {
	if (_client == nullptr || (!data && len > 0)) {
		return false;
	}
	
	char riga[80];
	size_t c = 0;
	
	// Invio completo del buffer, gestendo eventuali scritture parziali.
	auto invia = [&](const uint8_t* buf, size_t n) -> bool {
		size_t pos = 0;
		
		while (pos < n) {
			size_t scritto = _client->write(buf + pos, n - pos);
			
			if (scritto == 0) {
				return false;
			}
			
			pos += scritto;
		}
		
		return true;
	};
	
	for (size_t i = 0; i < len; i += 3) {
		size_t rem = len - i;
		
		uint8_t b1 = data[i];
		uint8_t b2 = (rem > 1) ? data[i + 1] : 0;
		uint8_t b3 = (rem > 2) ? data[i + 2] : 0;
		
		riga[c++] = b64_table[b1 >> 2];
		riga[c++] = b64_table[((b1 & 0x03) << 4) | (b2 >> 4)];
		riga[c++] = (rem > 1)
		? b64_table[((b2 & 0x0F) << 2) | (b3 >> 6)]
		: '=';
		riga[c++] = (rem > 2)
		? b64_table[b3 & 0x3F]
		: '=';
		
		// Buffer pieno: 76 caratteri Base64.
		if (c == 76) {
			if (mime) {
				riga[c++] = '\r';
				riga[c++] = '\n';
			}
			
			if (!invia((const uint8_t*)riga, c)) {
				return false;
			}
			
			c = 0;
		}
	}
	
	// Invio degli eventuali caratteri residui.
	if (c > 0) {
		if (mime) {
			riga[c++] = '\r';
			riga[c++] = '\n';
		}
		
		if (!invia((const uint8_t*)riga, c)) {
			return false;
		}
	}
	
	// In modalità non MIME, la riga termina qui,
	// anche quando la lunghezza è un multiplo di 76.
	if (!mime && len > 0) {
		static const uint8_t CRLF[] = {'\r', '\n'};
		
		if (!invia(CRLF, sizeof(CRLF))) {
			return false;
		}
	}
	
	return true;
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
bool SmtpMini::codificaFILE(const char* percorso) {
	if (_fsDevice == nullptr || _client == nullptr || percorso == nullptr) {
		#ifdef SMTP_MINI_DEBUG
			Serial.println(F("Errore: File System, client o percorso non valido."));
		#endif
		return false;
	}
	
	fs::File f = _fsDevice->open(percorso, FILE_READ);
	if (!f) {
		#ifdef SMTP_MINI_DEBUG
			Serial.printf("Errore: Impossibile aprire il file: %s\n", percorso);
		#endif
		return false;
	}
	
	const size_t totale = f.size();
	if (totale == 0) {
		f.close();
		return true;
	}
	
	// Leggo blocco di 57 byte
	uint8_t accumulo[57]; 
	size_t byteLetti = 0;
	int ultimaPercentuale = -1;
	bool errore = false;
	
	#ifdef SMTP_MINI_DEBUG
		Serial.print(F("Elaborazione e invio file da SD... "));
	#endif
	
	while (byteLetti < totale) {
		size_t daLeggere = totale - byteLetti;
		if (daLeggere > sizeof(accumulo)) {
			daLeggere = sizeof(accumulo);
		}
		
		size_t n = f.read(accumulo, daLeggere);
		if (n == 0) {
			errore = true;
			break;
		}
		
		byteLetti += n;
		
		if (!codifica64(accumulo, n, true)) {
			errore = true;
			break;
		}
		
		#ifdef SMTP_MINI_DEBUG
			int percentuale = (int)((byteLetti * 100ULL) / totale);
			if (percentuale != ultimaPercentuale && percentuale % 5 == 0) {
				ultimaPercentuale = percentuale;
				Serial.print('#');
			}
		#endif
	}
	
	f.close();
	
	#ifdef SMTP_MINI_DEBUG
		if (errore) {
			Serial.println(F(" ERRORE durante lettura o trasmissione!"));
			} else {
			Serial.println(F(" 100% - Completato!"));
		}
	#endif
	
	return !errore && byteLetti == totale;
}

bool SmtpMini::readSMTPLine(char* line, size_t size, uint32_t timeoutMs) {
	size_t n = 0;
	uint32_t start = millis();
	
	while ((uint32_t)(millis() - start) < timeoutMs) {
		int av = _client->available();
		if (av > 0) {
			// Leggiamo finché ci sono dati nel buffer di rete
			while (av-- > 0) {
				int ch = _client->read();
				if (ch < 0) break;
				
				if (ch == '\n') {
					line[n] = '\0';
					#ifdef SMTP_MINI_DEBUG
						Serial.printf("S: %s\n", line);
					#endif
					return true;
				}
				
				if (ch != '\r' && n < size - 1) {
					line[n++] = (char)ch;
				}
			}
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
  char line[256];
	_lastErrorCode = -1;
	
  while (true) {
    if (!readSMTPLine(line, sizeof(line)))
		break;
		
    if (strlen(line) < 4)
		break;
		
    if (line[3] == ' ') {
      line[3] = '\0';
			_lastErrorCode = atoi(line);
      break;
		}
		
    if (line[3] != '-')
		break;
	}
	
	return _lastErrorCode;
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
		//_lastErrorCode = code;
    Serial.printf("ERRORE SMTP: atteso %d, ricevuto %d\n", expected, code);
    return false;
	}
	//Serial.println(code);
  return true;
}

bool SmtpMini::inviaBody(const char* body) {
	if (body == nullptr || _client == nullptr) return false;
	
	char bufferinterno[128];
	size_t pbuf = 0;
	bool inizioRiga = true;
	
	auto flush = [&]() -> bool {
		size_t inviati = 0;
		
		while (inviati < pbuf) {
			size_t n = _client->write(
				(const uint8_t*)bufferinterno + inviati,
				pbuf - inviati
			);
			
			if (n == 0) return false;
			inviati += n;
		}
		
		pbuf = 0;
		return true;
	};
	
	for (size_t i = 0; body[i] != '\0'; i++) {
		char c = body[i];
		
		if (pbuf > sizeof(bufferinterno) - 2) {
			if (!flush()) return false;
		}
		
		// Normalizza CR, LF e CRLF in CRLF.
		if (c == '\r' || c == '\n') {
			if (c == '\r' && body[i + 1] == '\n') i++;
			
			bufferinterno[pbuf++] = '\r';
			bufferinterno[pbuf++] = '\n';
			inizioRiga = true;
			continue;
		}
		
		// Dot-stuffing
		if (inizioRiga && c == '.') {
			bufferinterno[pbuf++] = '.';
		}
		
		bufferinterno[pbuf++] = c;
		inizioRiga = false;
	}
	
	// Termina il body con CRLF, se necessario.
	if (!inizioRiga) {
		if (pbuf > sizeof(bufferinterno) - 2) {
			if (!flush()) return false;
		}
		
		bufferinterno[pbuf++] = '\r';
		bufferinterno[pbuf++] = '\n';
	}
	
	if (!flush()) return false;
	return true;
}


bool SmtpMini::sendEmail(const char* to, const char* subject, const char* body, bool isHtml, SMTPAttachment* attachments, size_t attachmentCount) {
  _lastErrorCode = 0;
	
	#ifdef SMTP_MINI_DEBUG
		#ifdef SMTP_USE_STARTTLS
			Serial.printf("\n--- Connessione STARTTLS (Porta %d) a %s %s---\n", _port, _host, (_certCA)? "(certCA)": "");
			#else
			Serial.printf("\n--- Connessione SSL/TLS (Porta %d) a %s %s---\n", _port, _host, (_certCA)? "(certCA)": "");
		#endif
	#endif
	
	
	#ifdef SMTP_USE_STARTTLS
		// Buffer TLS: regolabili in base alla RAM disponibile.
		_client->setBufferSizes(4096, 1024);
		_client->setDebugLevel(1);
		// il wrapper deve iniziare in modalità TCP non cifrata.
		_client->setClient(&basic_client, false);
		
	#endif
	
	if(_certCA != nullptr){
		_client->setCACert(_certCA); // Usa il certificato dell'utente
		//Serial.println(_certCA);
		} else {
		_client->setInsecure();
	}
	
	
	if (!_client->connect(_host, _port)) {
		_lastErrorCode = -2;  
		Serial.println(F("FAIL connesion!"));
		return false;
	}
	Serial.println(F("Connected"));
	
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
			char sslError[128] = {0};
			int errore = _client->getLastSSLError(sslError, sizeof(sslError));
			_lastErrorCode = errore;
			
			Serial.printf("SSL error: %d\n", errore);
			if (strlen(sslError) > 0) {
        Serial.printf("SSL detail: %s\n", sslError);
			}
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
	
	
	// --- GESTIONE DESTINATARI ---
	#ifdef MULTIRCPT
		char emailBuffer[64]; 
		size_t indlistaEmail = 0;
		int pemailbuffer = 0;
		
		while (true) {
			char carattereCorrente = to[indlistaEmail];
			
			if (carattereCorrente == ' ') {
				indlistaEmail++;
				continue;
			}    
			
			if (carattereCorrente == '\0' || carattereCorrente == ';') {
				if (pemailbuffer > 0) {
					emailBuffer[pemailbuffer] = '\0';
					// Rimosso il limite fisso > 10 (permette email corte ma sintatticamente valide)
					_client->printf("RCPT TO:<%s>\r\n", emailBuffer);
					if (readSMTPResponse() != 250) return false;
					pemailbuffer = 0;
				}
				if (carattereCorrente == '\0') break;
				} else if (pemailbuffer < (int)sizeof(emailBuffer) - 1) {
				emailBuffer[pemailbuffer++] = carattereCorrente;
			}
			indlistaEmail++;
		}
    #else
		_client->printf("RCPT TO:<%s>\r\n", to);
		if (readSMTPResponse() != 250) return false;
	#endif
	
	if (!smtpCommand("DATA", 354)) return false;
	
	//_client->printf("From: <%s>\r\n", _email);
	//_client->printf("To: <%s>\r\n", to);
	//_client->printf("Subject: %s\r\n", subject);
	_client->printf("From: <%s>\r\nTo: <%s>\r\nSubject: %s\r\n", _email, to, subject);
	
	#ifdef ENABLE_TIMEINFO
		//Serial.println(_timeSMTP);
		if(_timeSMTP != nullptr){_client->printf("Date: %s\r\n", _timeSMTP); }
		//Wed, 14 Oct 2026 08:14:13 +0200
		//strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S %z", &timeinfo);
	#endif
	//_client->print("MIME-Version: 1.0\r\n");
	//// 1 RIGA VUOTA
	//_client->printf("Content-Type: multipart/mixed; boundary=\"%s\"\r\n\r\n", boundary);	
	//_client->printf("--%s\r\n", boundary);
	// 1 RIGA VUOTA
	_client->printf("MIME-Version: 1.0\r\nContent-Type: multipart/mixed; boundary=\"%s\"\r\n\r\n--%s\r\n", boundary, boundary);
	
	// 2 RIGA VUOTA
	if (isHtml) {
		_client->print("Content-Type: text/html; charset=UTF-8\r\n\r\n");
		} else {
		_client->print("Content-Type: text/plain; charset=UTF-8\r\n\r\n");
	}
	
	if (!inviaBody(body)) {
		_lastErrorCode = -3;
		return false;
	}
	// 3 RIGA VUOTA
	_client->print("\r\n");
	
	// Ciclo dinamico degli allegati ****** ALLEGATI
	if (attachments != nullptr && attachmentCount > 0) {
		for (size_t i = 0; i < attachmentCount; i++) {
			SMTPAttachment corrente = attachments[i];
			const char* nomedamostrare = nomeBase(corrente.nomeFile);
			
			//_client->printf("--%s\r\n", boundary);
			//_client->printf("Content-Type: application/octet-stream; name=\"%s\"\r\n", nomedamostrare);
			//	_client->print("Content-Transfer-Encoding: base64\r\n");
			//  RIGA VUOTA
			//_client->printf("Content-Disposition: attachment; filename=\"%s\"\r\n\r\n", nomedamostrare);
			
			//  RIGA VUOTA
			// Unificato l'invio dell'header dell'allegato in un'unica macro print
			_client->printf("--%s\r\nContent-Type: application/octet-stream; name=\"%s\"\r\nContent-Transfer-Encoding: base64\r\nContent-Disposition: attachment; filename=\"%s\"\r\n\r\n", 
			boundary, nomedamostrare, nomedamostrare);
			
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
	if (!smtpCommand(".", 250)) return false;
	smtpCommand("QUIT", 221);
	_lastErrorCode = 0;   // invio riuscito
	return true;
}
