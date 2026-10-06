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
  char nomeFile[64] = {"\0"};
  const char* bufferRAM = nullptr; // Lasciare vuoto se il file è su SD
  size_t lunghezzaRAM = 0;         // Lasciare 0 se il file è su SD
};

// Tabella globale per la codifica Base64
const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

class SmtpMini {
public:
    SmtpMini(WiFiClientSecure &client);
    bool begin(const char* email, const char* appPassword, fs::FS *fileSystem = nullptr);
    
    bool sendEmail(const char* to, const char* subject, const char* body, bool isHtml = false, SMTPAttachment* attachments = nullptr, size_t attachmentCount = 0);
    
    int getLastError() const;

private:
    const char* _email;
    const char* _appPassword;
    WiFiClientSecure* _client;
    fs::FS* _fsDevice; // Puntatore generico al File System scelto dall'utente
    int _lastErrorCode; // Memorizza l'ultimo codice di errore o risposta SMTP

    bool _waitForResponse(const char* expectedCode);
    void codifica64(const char* buffer, size_t lunghezza = 0);
    void codificaFILE(char* percorso);
};

#endif
