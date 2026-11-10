#ifndef SMTP_MINI_H
	#define SMTP_MINI_H
	
	#include <Arduino.h>
	
	// --- DEBUG CONDIZIONALE ---
	// Decommenta la riga sotto per vedere tutto il traffico SMTP sul monitor seriale.
	#define SMTP_MINI_DEBUG
	
	//#define ENABLE_TIMEINFO
	#define SMTP_SERVER "smtp.gmail.com"
	
	//#define MULTIRCPT
	
	// Decommenta questa riga se usi la porta 587 (STARTTLS)
	// Commentala se usi la porta 465 (Gmail o altri con SSL implicito)
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
    const char* nomeFile = nullptr;   
    const void* bufferRAM = nullptr;   
    size_t lunghezzaRAM = 0;           
		
    // Costruttore per file da FileSystem
    SMTPAttachment(const char* file) 
		: nomeFile(file), bufferRAM(nullptr), lunghezzaRAM(0) {}
		
    // Costruttore per buffer RAM / PROGMEM
    SMTPAttachment(const char* nome, const void* ram, size_t len) 
		: nomeFile(nome), bufferRAM(ram), lunghezzaRAM(len) {}
	};
	
	class SmtpMini {
		public:
    // Costruttore unico senza parametri esterni: gestisce la rete autonomamente
    SmtpMini();
		
    bool begin(const char* email, const char* appPassword, fs::FS* fileSystem = nullptr);
    void setSmtpServer(const char* host, uint16_t port, const char* certCA = nullptr);
    bool sendEmail(const char* mailboxes, const char* subject, const char* body, bool isHtml = false, SMTPAttachment* attachments = nullptr, size_t attachmentCount = 0);
    int getLastError() const;
		
    //#ifdef ENABLE_TIMEINFO
		const char* _timeSMTP = nullptr;
		void setSMTPtime(const char* timestr) { _timeSMTP = timestr; }
    //#endif
		
		private:
    const char* _host = SMTP_SERVER;
    uint16_t _port = SMTP_PORT;
    const char* _email = nullptr;
    const char* _appPassword = nullptr;
    const char* _certCA = nullptr;
    
		
    // Gestione nativa e diretta dei client
    #ifdef SMTP_USE_STARTTLS
			WiFiClient basic_client;     // Client TCP di base
			ESP_SSLClient _sslClient;    // Wrapper TLS
			#else
			WiFiClientSecure _sslClient; // Client SSL nativo ESP32
		#endif
		
    fs::FS* _fsDevice = nullptr;
    int _lastErrorCode = 0;
		
    int readSMTPResponse();
    bool readSMTPLine(char* line, size_t size, uint32_t timeoutMs = 10000);
    bool smtpCommand(const char* command, int expected);
    bool codifica64(const uint8_t* data, size_t len, bool mime);
    bool codificaFILE(const char* percorso);
    bool inviaBody(const char* body);
	};
	
#endif
