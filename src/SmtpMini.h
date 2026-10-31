#ifndef SMTP_MINI_H
	#define SMTP_MINI_H
	
	#include <Arduino.h>
	
	// --- DEBUG CONDIZIONALE ---
	// Decommenta la riga sotto per vedere tutto il traffico SMTP sul monitor seriale.
	// Commentala per ripulire la seriale e risparmiare memoria Flash nel progetto definitivo.
	#define SMTP_MINI_DEBUG
	
	#define ENABLE_TIMEINFO
	
	#define SMTP_SERVER "smtp.gmail.com"
	
	//#define MULTIRCPT
	// es: "recipipient1@gmail.com;recipient2@....." ;
	
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
	
	/*	
		// STRUTTURA ALLEGATI
		struct SMTPAttachment {
    char nomeFile[64] = { "\0" };
    const void* bufferRAM = nullptr;   // Lasciare vuoto se il file è su SD
    size_t lunghezzaRAM = 0;						// Lasciare 0 se il file è su SD
		};
		*/
	
	struct SMTPAttachment {
		// Modificato da array fisso a puntatore: risparmia fino a 60 byte di RAM per allegato
		const char* nomeFile = nullptr;   
		const void* bufferRAM = nullptr;   
		size_t lunghezzaRAM = 0;           
	};
	
	class SmtpMini {
		public:
		#ifdef SMTP_USE_STARTTLS
			SmtpMini(ESP_SSLClient& client);
			#else
			SmtpMini(WiFiClientSecure& client);
		#endif
		bool begin(const char* email, const char* appPassword, fs::FS* fileSystem = nullptr);
		
		void setSmtpServer(const char* host, uint16_t port, const char* certCA = nullptr);
		
		bool sendEmail(const char* to, const char* subject, const char* body, bool isHtml = false, SMTPAttachment* attachments = nullptr, size_t attachmentCount = 0);
		
		int getLastError() const;
		#ifdef ENABLE_TIMEINFO
			void setSMTPtime(const char* timestr) { _timeSMTP = timestr; }
		#endif
		
		private:
		
		const char* _host = SMTP_SERVER;
		uint16_t _port = SMTP_PORT;
		const char* _email = nullptr;
		const char* _appPassword = nullptr;
		const char* _certCA = nullptr; // Memorizza il puntatore al certificato esterno
		#ifdef ENABLE_TIMEINFO
			const char* _timeSMTP = nullptr;
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
		bool codifica64(const uint8_t* data, size_t len, bool mime);
		bool codificaFILE(const char* percorso);
		bool inviaBody(const char* body);
		
	};
	
#endif
