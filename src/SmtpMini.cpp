#include "SmtpMini.h"

static constexpr char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Costruttore: l'associazione del client avviene in begin()
SmtpMini::SmtpMini() : _sslClient() {
	_lastErrorCode = 0;
}

bool SmtpMini::begin(const char* email, const char* appPassword, fs::FS* fileSystem) {
	_email = email;
	_appPassword = appPassword;
	_fsDevice = fileSystem;
	_lastErrorCode = 0;
	
	#ifdef SMTP_USE_STARTTLS
		// Associa il socket TCP sottostante a ESP_SSLClient
		_sslClient.setClient(&basic_client, false);
		_sslClient.setBufferSizes(4096, 1024);
		_sslClient.setDebugLevel(0);
	#endif
	
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

static bool streamWriteAll(
	#ifdef SMTP_USE_STARTTLS
    ESP_SSLClient& client,
		#else
    WiFiClientSecure& client,
	#endif
	const uint8_t* buf, size_t len) {
	size_t pos = 0;
	while (pos < len) {
		size_t scritto = client.write(buf + pos, len - pos);
		if (scritto == 0) return false;
		pos += scritto;
	}
	return true;
}

bool SmtpMini::codifica64(const uint8_t* data, size_t len, bool mime) {
	if ((!data && len > 0)) return false;
	
	// Buffer locale espanso: contiene fino a 7 righe MIME (532 char + 14 CRLF)
	char riga[546];
	size_t c = 0;
	size_t lineLen = 0;
	
	for (size_t i = 0; i < len; i += 3) {
		size_t rem = len - i;
		
		uint8_t b1 = data[i];
		uint8_t b2 = (rem > 1) ? data[i + 1] : 0;
		uint8_t b3 = (rem > 2) ? data[i + 2] : 0;
		
		riga[c++] = b64_table[b1 >> 2];
		riga[c++] = b64_table[((b1 & 0x03) << 4) | (b2 >> 4)];
		riga[c++] = (rem > 1) ? b64_table[((b2 & 0x0F) << 2) | (b3 >> 6)] : '=';
		riga[c++] = (rem > 2) ? b64_table[b3 & 0x3F] : '=';
		lineLen += 4;
		
		if (mime && lineLen == 76) {
			riga[c++] = '\r';
			riga[c++] = '\n';
			lineLen = 0;
		}
		
		// Svuota il buffer verso la rete quando è quasi pieno
		if (c >= sizeof(riga) - 6) {
			if (!streamWriteAll(_sslClient, (const uint8_t*)riga, c)) return false;
			c = 0;
		}
	}
	
	if (c > 0) {
		if (mime && lineLen > 0) {
			riga[c++] = '\r';
			riga[c++] = '\n';
		}
		if (!streamWriteAll(_sslClient, (const uint8_t*)riga, c)) return false;
	}
	
	if (!mime && len > 0) {
		static const uint8_t CRLF[] = {'\r', '\n'};
		if (!streamWriteAll(_sslClient, CRLF, sizeof(CRLF))) return false;
	}
	
	return true;
}

void SmtpMini::codifica64(const char* buffer, size_t lunghezza) {
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

// Copia scartando CR, LF e virgolette (anti-iniezione negli header/comandi SMTP)
static void pulisci(char* dst, size_t size, const char* src) {
	size_t n = 0;
	for (; src && *src && n < size - 1; src++) {
		if (*src != '\r' && *src != '\n' && *src != '"') dst[n++] = *src;
	}
	dst[n] = '\0';
}

bool SmtpMini::codificaFILE(const char* percorso) {
	if (_fsDevice == nullptr || percorso == nullptr) return false;
	
	fs::File f = _fsDevice->open(percorso, FILE_READ);
	if (!f) {
		#ifdef SMTP_MINI_DEBUG
			Serial.printf("File not found..(%s) \n", percorso);
		#endif
		return false;
	}
	
	const size_t totale = f.size();
	if (totale == 0) {
		f.close();
		return true;
	}
	
	// Buffer da 570 byte: legge 10 righe MIME per singola transazione I/O
	uint8_t accumulo[570];
	size_t residui = 0;
	size_t byteLetti = 0;
	bool errore = false;
	
	#ifdef SMTP_MINI_DEBUG
		Serial.print(F("Elaborazione e invio file da FS...\n "));
	#endif
	
	while (byteLetti < totale) {
		size_t spazio = sizeof(accumulo) - residui;
		size_t n = f.read(accumulo + residui, spazio);
		
		if (n == 0) {
			errore = true;
			break;
		}
		
		byteLetti += n;
		residui += n;
		
		// Processa solo i blocchi multipli di 57 byte (1 riga Base64 pulita)
		size_t bloccoProcessabile = (residui / 57) * 57;
		
		if (bloccoProcessabile > 0) {
			if (!codifica64(accumulo, bloccoProcessabile, true)) {
				errore = true;
				break;
			}
			
			// Mantiene gli eventuali byte incompleti (< 57) in testa al buffer
			residui -= bloccoProcessabile;
			if (residui > 0) {
				memmove(accumulo, accumulo + bloccoProcessabile, residui);
			}
		}
	}
	
	if (!errore && residui > 0) {
		if (!codifica64(accumulo, residui, true)) {
			errore = true;
		}
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
		while (_sslClient.available() > 0) {
			int ch = _sslClient.read();
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
		
		if (!_sslClient.connected() && !_sslClient.available()) {
			#ifdef SMTP_MINI_DEBUG
				Serial.println(F("Connessione SMTP chiusa dal server"));
			#endif
			return false;
		}
		
		vTaskDelay(1);
	}
	
	line[n] = '\0';
	#ifdef SMTP_MINI_DEBUG
		Serial.printf("TIMEOUT SMTP, riga parziale: [%s]\n", line);
	#endif
	return false;
}

int SmtpMini::readSMTPResponse() {
	char line[514];
	_lastErrorCode = -1;
	
	while (true) {
		if (!readSMTPLine(line, sizeof(line))) break;
		if (strlen(line) < 4) break;
		
		// Verifica che la riga inizi con 3 cifre
		if (isdigit(line[0]) && isdigit(line[1]) && isdigit(line[2])) {
			if (line[3] == ' ') {
				line[3] = '\0';
				_lastErrorCode = atoi(line);
				break;
			}
			if (line[3] != '-') break;
			} else {
			break;
		}
	}
	
	return _lastErrorCode;
}

bool SmtpMini::smtpCommand(const char* command, int expected) {
	#ifdef SMTP_MINI_DEBUG
		Serial.printf("C: %s\n", command);
		//		Serial.println(command);
	#endif
	
	_sslClient.print(command);
	_sslClient.print("\r\n");
	//_sslClient.printf("%s\r\n", command);
	
	int code = readSMTPResponse();
	
	if (code != expected) {
		#ifdef SMTP_MINI_DEBUG
			Serial.printf("ERRORE SMTP: atteso %d, ricevuto %d\n", expected, code);
		#endif
		return false;
	}
	return true;
}

bool SmtpMini::inviaBody(const char* body) {
	if (body == nullptr) return false;
	
	char bufferinterno[256];
	size_t pbuf = 0;
	bool inizioRiga = true;
	
	auto flush = [&]() -> bool {
		if (pbuf == 0) return true;
		bool res = streamWriteAll(_sslClient, (const uint8_t*)bufferinterno, pbuf);
		pbuf = 0;
		return res;
	};
	
	for (size_t i = 0; body[i] != '\0'; i++) {
		char c = body[i];
		
		if (pbuf > sizeof(bufferinterno) - 3) {
			if (!flush()) return false;
		}
		
		if (c == '\r' || c == '\n') {
			if (c == '\r' && body[i + 1] == '\n') i++;
			
			bufferinterno[pbuf++] = '\r';
			bufferinterno[pbuf++] = '\n';
			inizioRiga = true;
			continue;
		}
		
		if (inizioRiga && c == '.') {
			bufferinterno[pbuf++] = '.';
		}
		
		bufferinterno[pbuf++] = c;
		inizioRiga = false;
	}
	
	if (!inizioRiga) {
		if (pbuf > sizeof(bufferinterno) - 2) {
			if (!flush()) return false;
		}
		bufferinterno[pbuf++] = '\r';
		bufferinterno[pbuf++] = '\n';
	}
	
	return flush();
}

bool SmtpMini::sendEmail(const char* to, const char* subject, const char* body, bool isHtml, SMTPAttachment* attachments, size_t attachmentCount) {
	
	_lastErrorCode = 0;
	char emailP[65], toP[65], subjectP[81];
	pulisci(emailP, sizeof(emailP), _email);
	pulisci(toP, sizeof(toP), to);
	pulisci(subjectP, sizeof(subjectP), subject);
	
	char boundary[48];
	snprintf(boundary, sizeof(boundary), "=_Part_%08X_%08X", esp_random(), esp_random());
	
	#ifdef SMTP_MINI_DEBUG
		#ifdef SMTP_USE_STARTTLS
			Serial.printf("\n--- Connessione STARTTLS (Porta %d) a %s %s---\n", _port, _host, (_certCA) ? "(certCA)" : "");
			#else
			Serial.printf("\n--- Connessione SSL/TLS (Porta %d) a %s %s---\n", _port, _host, (_certCA) ? "(certCA)" : "");
		#endif
	#endif
	
	if (_certCA != nullptr) {
		_sslClient.setCACert(_certCA);
    } else {
		_sslClient.setInsecure();
	}
	
	if (!_sslClient.connect(_host, _port)) {
		_lastErrorCode = -2;
		#ifdef SMTP_MINI_DEBUG
			Serial.println(F("FAIL connessione!"));
		#endif
		return false;
	}
	
	#ifdef SMTP_MINI_DEBUG
		Serial.println(F("Connected"));
	#endif
	
	// 1. Saluto
	if (readSMTPResponse() != 220) { _sslClient.stop(); return false; }
	
	// 2. EHLO in chiaro
	if (!smtpCommand("EHLO esp32.local", 250)) { _sslClient.stop(); return false; }
	
	#ifdef SMTP_USE_STARTTLS
		// 3. STARTTLS
		if (!smtpCommand("STARTTLS", 220)) { _sslClient.stop(); return false; }
		
		#ifdef SMTP_MINI_DEBUG
			Serial.println(F("Avvio handshake STARTTLS..."));
		#endif
		
		if (!_sslClient.connectSSL()) {
			char sslError[128] = {0};
			int errore = _sslClient.getLastSSLError(sslError, sizeof(sslError));
			_lastErrorCode = errore;
			
			#ifdef SMTP_MINI_DEBUG
				Serial.printf("SSL error: %d\n", errore);
				if (strlen(sslError) > 0) {
					Serial.printf("SSL detail: %s\n", sslError);
				}
			#endif
			_sslClient.stop();
			return false;
		}
		
		#ifdef SMTP_MINI_DEBUG
			Serial.println(F("TLS attivo"));
		#endif
		
		// 4. Secondo EHLO dentro TLS
		if (!smtpCommand("EHLO esp32.local", 250)) { _sslClient.stop(); return false; }
	#endif
	
	// Autenticazione AUTH LOGIN
	if (!smtpCommand("AUTH LOGIN", 334)) { _sslClient.stop(); return false; }
	
	codifica64((const uint8_t*)_email, strlen(_email), false);
	if (readSMTPResponse() != 334) { _sslClient.stop(); return false; }
	
	codifica64((const uint8_t*)_appPassword, strlen(_appPassword), false);
	if (readSMTPResponse() != 235) { _sslClient.stop(); return false; }
	
	#ifdef SMTP_MINI_DEBUG
		Serial.println(F("Autenticazione riuscita"));
	#endif
	
	char headerBuf[256];
	snprintf(headerBuf, sizeof(headerBuf), "MAIL FROM:<%s>\r\n", emailP);
	_sslClient.print(headerBuf);
	if (readSMTPResponse() != 250) { _sslClient.stop(); return false; }
	
	// Multi-recipient
	#ifdef MULTIRCPT
		char emailBuffer[64];
		size_t indlistaEmail = 0;
		int pemailbuffer = 0;
		
		while (true) {
			char carattereCorrente = to[indlistaEmail];
			//if (carattereCorrente == ' ') {
			if (carattereCorrente == ' ' || carattereCorrente == '\r' || carattereCorrente == '\n' || carattereCorrente == '"') {
				indlistaEmail++;
				continue;
			}
			
			if (carattereCorrente == '\0' || carattereCorrente == ';') {
				if (pemailbuffer > 0) {
					emailBuffer[pemailbuffer] = '\0';
					if(pemailbuffer > 4){
						snprintf(headerBuf, sizeof(headerBuf), "RCPT TO:<%s>\r\n", emailBuffer);
						_sslClient.print(headerBuf);
						if (readSMTPResponse() != 250) { _sslClient.stop(); return false; }
					}
					pemailbuffer = 0;
				}
				if (carattereCorrente == '\0') break;
				} else if (pemailbuffer < (int)sizeof(emailBuffer) - 1) {
				emailBuffer[pemailbuffer++] = carattereCorrente;
			}
			indlistaEmail++;
		}
    #else
		snprintf(headerBuf, sizeof(headerBuf), "RCPT TO:<%s>\r\n", toP);
		_sslClient.print(headerBuf);
		if (readSMTPResponse() != 250) { _sslClient.stop(); return false; }
	#endif
	
	if (!smtpCommand("DATA", 354)) { _sslClient.stop(); return false; }
	
	// Headers principali
	int len = snprintf(headerBuf, sizeof(headerBuf),
		"From: <%.64s>\r\nTo: <%.64s>\r\nSubject: %.80s\r\n",
	emailP, toP, subjectP);
	//_sslClient.write((const uint8_t*)headerBuf, len);
	if (!streamWriteAll(_sslClient, (const uint8_t*)headerBuf, len)) {
		_lastErrorCode = -10;
		_sslClient.stop();
		return false;
	}
	
	
	//#ifdef ENABLE_TIMEINFO
	if (_timeSMTP != nullptr) {
		len = snprintf(headerBuf, sizeof(headerBuf), "Date: %.40s\r\n", _timeSMTP);
		//_sslClient.write((const uint8_t*)headerBuf, len);
		if (!streamWriteAll(_sslClient, (const uint8_t*)headerBuf, len)) {
			_lastErrorCode = -11;
			_sslClient.stop();
			return false;
		}
	}
	//#endif
	
	len = snprintf(headerBuf, sizeof(headerBuf),
		"MIME-Version: 1.0\r\nContent-Type: multipart/mixed; boundary=\"%s\"\r\n\r\n--%s\r\n",
	boundary, boundary);
	//_sslClient.write((const uint8_t*)headerBuf, len);
	if (!streamWriteAll(_sslClient,	(const uint8_t*)headerBuf, len)) {
		_lastErrorCode = -12;
		_sslClient.stop();
		return false;
	}
	
	if (isHtml) {
		_sslClient.print("Content-Type: text/html; charset=UTF-8\r\n\r\n");
    } else {
		_sslClient.print("Content-Type: text/plain; charset=UTF-8\r\n\r\n");
	}
	
	if (!inviaBody(body)) {
		_lastErrorCode = -3;
		_sslClient.stop();
		return false;
	}
	
	_sslClient.print("\r\n");
	
	// Ciclo allegati
	bool allegatoOk = true;
	if (attachments != nullptr && attachmentCount > 0) {
		for (size_t i = 0; i < attachmentCount; i++) {
			SMTPAttachment corrente = attachments[i];
			
			if (corrente.nomeFile == nullptr) { allegatoOk = false; continue; }
			
      char nomedamostrare[51];
      pulisci(nomedamostrare, sizeof(nomedamostrare), nomeBase(corrente.nomeFile));
			
			len = snprintf(headerBuf, sizeof(headerBuf),
				"--%s\r\nContent-Type: application/octet-stream; name=\"%.50s\"\r\nContent-Transfer-Encoding: base64\r\nContent-Disposition: attachment; filename=\"%.50s\"\r\n\r\n",
			boundary, nomedamostrare, nomedamostrare);
			
			//_sslClient.write((const uint8_t*)headerBuf, len);
			if (!streamWriteAll(_sslClient,	(const uint8_t*)headerBuf,len)) {
				allegatoOk = false;
				continue;
			}
			
			if (corrente.bufferRAM != nullptr && corrente.lunghezzaRAM > 0) {
				#ifdef SMTP_MINI_DEBUG
					Serial.printf("Invio allegato da RAM: %s\n", corrente.nomeFile);
				#endif
				if (!codifica64((const uint8_t*)corrente.bufferRAM, corrente.lunghezzaRAM, true)) allegatoOk = false;
				} else if (corrente.nomeFile != nullptr && strlen(corrente.nomeFile) > 0) {
				#ifdef SMTP_MINI_DEBUG
					Serial.printf("Invio allegato da FS: %s\n", corrente.nomeFile);
				#endif
				if (!codificaFILE(corrente.nomeFile)) allegatoOk = false;
			}
		}
	}
	attachments = nullptr;
	_timeSMTP = nullptr;
	
	len = snprintf(headerBuf, sizeof(headerBuf), "--%s--\r\n", boundary);
	//_sslClient.write((const uint8_t*)headerBuf, len);
	if (!streamWriteAll(_sslClient,	(const uint8_t*)headerBuf,len)) {
		_lastErrorCode = -15;
		_sslClient.stop();
		return false;
	}
	
	if (!smtpCommand(".", 250)) { _sslClient.stop(); return false; }
	
	smtpCommand("QUIT", 221);
	_sslClient.stop();
	_lastErrorCode = allegatoOk ? 0 : -4;
	return true;
}
