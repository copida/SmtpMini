#ifndef SMTP_MINI_H
#define SMTP_MINI_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <FS.h>


// --- DEBUG CONDIZIONALE ---
// Decommenta la riga sotto per vedere tutto il traffico SMTP sul monitor seriale.
// Commentala per ripulire la seriale e risparmiare memoria Flash nel progetto definitivo.
#define SMTP_MINI_DEBUG
#define SMTP_SERVER "smtp.gmail.com"
#define SMTP_PORT 465

// ==========================================
// 2. STRUTTURA ALLEGATI
// ==========================================
struct SMTPAttachment {
  char nomeFile[64] = { "\0" };
  const uint8_t* bufferRAM = nullptr;  // Lasciare vuoto se il file è su SD
  size_t lunghezzaRAM = 0;             // Lasciare 0 se il file è su SD
};

// Tabella globale per la codifica Base64
const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

class SmtpMini {
public:
  SmtpMini(WiFiClientSecure& client);
  bool begin(const char* email, const char* appPassword, fs::FS* fileSystem = nullptr);

  void setSmtpServer(const char* host, uint16_t port = 465);

  bool sendEmail(const char* to, const char* subject, const char* body, bool isHtml = false, SMTPAttachment* attachments = nullptr, size_t attachmentCount = 0);

  int getLastError() const;

private:

  const char* _host = SMTP_SERVER;
  uint16_t _port = SMTP_PORT;
  const char* _email;
  const char* _appPassword;
  WiFiClientSecure* _client;
  fs::FS* _fsDevice;   // Puntatore generico al File System scelto dall'utente
  int _lastErrorCode;  // Memorizza l'ultimo codice di errore o risposta SMTP

  bool _waitForResponse(const char* expectedCode);
  void codifica64(const char* buffer, size_t lunghezza = 0);
  void codifica64(const uint8_t* data, size_t len, bool mime);
  void codificaFILE(const char* percorso);
  //static const char* nomeBase(const char* percorso);
};

#endif
