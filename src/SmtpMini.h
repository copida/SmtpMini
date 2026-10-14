#ifndef SMTP_MINI_H
#define SMTP_MINI_H

	#include <Arduino.h>

	// --- DEBUG CONDIZIONALE ---
	// Decommenta la riga sotto per vedere tutto il traffico SMTP sul monitor seriale.
	// Commentala per ripulire la seriale e risparmiare memoria Flash nel progetto definitivo.
	//#define SMTP_MINI_DEBUG

	#define ENABLE_TIMEINFO

	#define SMTP_SERVER "smtp.gmail.com"

	// Decommenta questa riga se usi la porta 587
	// Commentala se usi la porta 465 (Gmail o altri SSL implicito)
	//#define SMTP_USE_STARTTLS
		
	#ifndef SMTP_USE_STARTTLS
		#include <WiFiClientSecure.h>
		#define SMTP_PORT 465
		#else
		#include <WiFiClient.h>
		#include <ESP_SSLClient.h>
		#define SMTP_PORT 587
	#endif
	
	#include <FS.h>
		
	// STRUTTURA ALLEGATI
	struct SMTPAttachment {
    char nomeFile[64] = { "\0" };
    const void* bufferRAM = nullptr;   // Lasciare vuoto se il file è su SD
    size_t lunghezzaRAM = 0;						// Lasciare 0 se il file è su SD
};
	
	// Tabella globale per la codifica Base64
	const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	
	class SmtpMini {
		public:
		#ifdef SMTP_USE_STARTTLS
			SmtpMini(ESP_SSLClient& client);
			#else
			SmtpMini(WiFiClientSecure& client);
		#endif
		bool begin(const char* email, const char* appPassword, fs::FS* fileSystem = nullptr);
		
		void setSmtpServer(const char* host, uint16_t port = 465);
		
		bool sendEmail(const char* to, const char* subject, const char* body, bool isHtml = false, SMTPAttachment* attachments = nullptr, size_t attachmentCount = 0);
		
		int getLastError() const;
		#ifdef ENABLE_TIMEINFO
		void setSMTPtime(const char* timestr) { _timeSMTP = timestr; }
		#endif
		
		private:
		
		const char* _host = SMTP_SERVER;
		uint16_t _port = SMTP_PORT;
		const char* _email;
		const char* _appPassword;
		#ifdef ENABLE_TIMEINFO
		const char* _timeSMTP;
		#endif
		
		#ifdef SMTP_USE_STARTTLS
			// TCP sottostante e wrapper TLS
			WiFiClient basic_client;
			ESP_SSLClient* _client;
			#else
			WiFiClientSecure* _client;
		#endif
		
		fs::FS* _fsDevice;   // Puntatore generico al File System scelto dall'utente
		int _lastErrorCode;  
		
		int readSMTPResponse();
		bool readSMTPLine(char* line, size_t size,uint32_t timeoutMs = 10000);
		bool smtpCommand(const char* command, int expected);
		void codifica64(const char* buffer, size_t lunghezza = 0);
		void codifica64(const uint8_t* data, size_t len, bool mime);
		void codificaFILE(const char* percorso);

	};
	
#endif
