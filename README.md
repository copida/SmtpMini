# SmtpMini

An **ultra-lightweight, high-performance, and RAM-optimized** SMTP client library for Arduino (ESP32 / ESP8266) designed to send secure emails (SSL/TLS) with dynamic text, HTML, and **mixed attachments** (from RAM buffers or File Systems like SD/LittleFS) with near-zero memory allocation.

[![Arduino Library](https://shields.io)](https://github.com)
[![License: MIT](https://shields.io)](https://opensource.org)

---

## 🚀 Perché SmtpMini?

Le librerie SMTP tradizionali per Arduino sono spesso pesanti, allocano costantemente oggetti `String` frammentando la heap e caricano interi file in memoria prima di inviarli. 

**SmtpMini adotta un approccio radicalmente diverso:**
* **Zero-Copy Base64:** Un'unica funzione ottimizzata (`codifica64`) converte sia le credenziali di autenticazione sia gli allegati al volo a blocchi di 3 byte, usando un minuscolo buffer di riga di soli **79 byte**.
* **Allegati Misti Intelligenti:** Permette di allegare file fisici da SD/LittleFS e stringhe/buffer generati istantaneamente in RAM nello stesso ciclo di invio.
* **Nativa WiFiClientSecure:** Integra la cifratura obbligatoria per Gmail (Porta 465) supportando la modalità `setInsecure()` per evitare la gestione e la scadenza dei certificati CA.
* **Niente Stringhe per gli Allegati:** Sfrutta array di `char` statici per prevenire la frammentazione della memoria SRAM.
* **Debug Condizionale:** Tramite un singolo flag `#define` nel file `.h` puoi rimuovere tutte le stringhe di log seriali dal firmware di produzione, risparmiando memoria Flash.

---

## 📂 Supporto universale ai File System (SD, LittleFS, SPIFFS)

Grazie all'uso del polimorfismo C++, `SmtpMini` rileva automaticamente qualsiasi File System di Arduino senza bisogno di configurazioni manuali o modifiche ai file `.h`. È sufficiente passare il puntatore al dispositivo desiderato nel metodo `begin()`:

```cpp
// Opzione A: Invio file da Scheda SD
SD.begin(4);
mailer.begin("latuamail@gmail.com", "password_app", &SD);

// Opzione B: Invio file dalla memoria Flash interna (LittleFS)
LittleFS.begin();
mailer.begin("latuamail@gmail.com", "password_app", &LittleFS);

// Opzione C: Solo allegati da RAM (Nessun File System richiesto)
mailer.begin("latuamail@gmail.com", "password_app"); // Il parametro FS va a nullptr di default
```


## 🛠️ Come Funziona (Esempio Completo)

Ecco come inviare un'email contenente un corpo in puro testo o HTML e due allegati contemporaneamente: uno storico letto da scheda SD e un report generato al volo nella memoria RAM.

```cpp
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <SD.h>
#include <SmtpMini.h>

const char* ssid = "IL_TUO_WIFI";
const char* password = "LA_TUA_PASSWORD";

WiFiClientSecure secureClient;
SmtpMini mailer(secureClient);

void setup() {
    Serial.begin(115200);

    // Connessione Wi-Fi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\nWi-Fi Connesso!");

    // Saltiamo il controllo formale del certificato per massima stabilità
    secureClient.setInsecure();

    // Inizializzazione SD Card (es: CS Pin 4)
    if (!SD.begin(4)) {
        Serial.println("SD card non trovata!");
    }

    // 1. Inizializziamo il mittente (Usa una "Password per le app" se usi Gmail)
    mailer.begin("latuamail@gmail.com", "abcd efgh ijkl mnop");

    // 2. Prepariamo un log istantaneo memorizzato solo nella RAM di Arduino
    char datiIstantaneiRAM[128];
    snprintf(datiIstantaneiRAM, sizeof(datiIstantaneiRAM), "Sensore,Valore\nVolt,5.12\nAmpere,0.85\n");

    // 3. Configurazione dinamica degli Allegati Misti (SD + RAM)
    SMTPAttachment mieiAllegati[2];

    // Allegato 1: File fisico memorizzato su SD
    // (Il carattere '/' iniziale viene rimosso automaticamente dal nome visibile nella mail)
    strncpy(mieiAllegati[0].nomeFile, "/storico.csv", 20);

    // Allegato 2: Buffer di testo generato al volo in RAM
    strncpy(mieiAllegati[1].nomeFile, "live_report.txt", 20);
    mieiAllegati[1].bufferRAM = datiIstantaneiRAM;
    mieiAllegati[1].lunghezzaRAM = strlen(datiIstantaneiRAM);

    // 4. Invio effettivo (Supporta testo semplice o HTML)
    bool successo = mailer.sendEmail(
			"destinatario@example.com",          // Destinatario
        "Report di Sistema",                  // Oggetto
        "Ciao! In allegato trovi i report.",  // Corpo del messaggio
        false,                               // isHtml (metti true se invii codice HTML)
        mieiAllegati,                        // Array degli allegati
        2                                    // Numero totale di allegati
    );

    if (successo) {
        Serial.println("Email inviata correttamente!");
    } else {
        Serial.print("Errore durante l'invio. Codice SMTP: ");
        Serial.println(mailer.getLastError());
    }
}

void loop() {}
```

---

## 📂 Struttura dell'Allegato (`SMTPAttachment`)

La configurazione della struttura è intuitiva e non richiede l'utilizzo di enumeratori o flag complessi:

```cpp
// Per un file su scheda SD (Cerca il percorso corrispondente):
SMTPAttachment att1;
strncpy(att1.nomeFile, "/cartella/file.txt", 20); // bufferRAM rimane nullptr per default

// Per un buffer dinamico presente nella RAM:
SMTPAttachment att2;
strncpy(att2.nomeFile, "nome_visualizzato.csv", 20);
att2.bufferRAM = mioBufferDiCaratteri;
att2.lunghezzaRAM = strlen(mioBufferDiCaratteri);
```

---

## 📈 Gestione Diagnostica ed Errori

In caso di fallimento della funzione `sendEmail()`, puoi risalire alla causa esatta interrogando `getLastError()`. La libreria mappa sia le risposte native del server che gli stati di rete:

| Codice Errore | Descrizione |
| :--- | :--- |
| `-1` | **Timeout di Rete:** Il server SMTP ha impiegato più di 8 secondi a rispondere. |
| `-2` | **Connessione Fallita:** Impossibile stabilire l'handshake TCP/SSL con il server. |
| `535` | **Authentication Failed:** Username o Password per le app errati. |
| `250` / `354` / ... | Qualsiasi codice di stato standard a 3 cifre del protocollo SMTP. |

---

## ⚙️ Supporto File System Alternativi (LittleFS)

Di base la libreria punta su `SD`. Se preferisci utilizzare la memoria Flash interna tramite **LittleFS**, ti basta aprire il file `SmtpMini.h` e invertire i commenti relativi al driver:

```cpp
//#include <SD.h>
//#define FS_DRV SD
#include <LittleFS.h>
#define FS_DRV LittleFS
```

---

## 📝 Licenza

Questa libreria è rilasciata sotto licenza MIT. Sentiti libero di usarla, modificarla e integrarla nei tuoi progetti commerciali o open-source.
