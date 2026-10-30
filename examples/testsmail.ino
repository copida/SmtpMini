#include <WiFi.h>
#include <time.h>
#include <SD.h>
#include <SmtpMini.h>

// questo #define serve solo per selzionare in questo sketch la parte SSL o STARTTLS
// fatto per evitare due esempi separati
// ricordarsi di commentare o decommentare "#define SMTP_USE_STARTTLS" in SmtpMini.h
// per il reale funzionamento in SSL o STARTTLS
#define ENABLE_STARTTLS

#define SD_CS_PIN 46  // per lolin s3
//#define SD_CS_PIN 5  // esp32

#define AUTHOR_EMAIL "mittenteg@gmail.com"
#define AUTHOR_PASSWORD "passwordapp"
#define RECIPIENT_EMAIL "destinatario.com"

const char* ssid = "XXXXXXXXXXXXXXXXXXX";
const char* password = "XXXXXXXXXXXXXX";


char battute[] PROGMEM = R"rawliteral(
  Se sei di buon umore, non ti preoccupare. Ti passerà.,
  Ogni soluzione genera nuovi problemi.,
  Se non t'importa dove sei, non ti sei perso.,
  È incredibile quanto ci vuole a fare una cosa che non stai facendo tu.,
  Quando c'è bisogno di toccar ferro o legno, ci si accorge che il mondo è fatto di alluminio e plastica,
  Sorridi... Domani sarà peggio.
)rawliteral";
size_t len_battute = sizeof(battute);

#ifdef ENABLE_STARTTLS
#define BODY_TXT "mail spedita su porta 587 in STARTTLS"
#else
#define BODY_TXT "mail spedita su porta 465 in SSL"
#endif

void handleSerialCommand(const String& line);
void listDir();

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connesso!");
  delay(1000);


  Serial.println("Sincronizzazione orario tramite NTP...");
  configTime(3600, 3600, "time.inrim.it");

  setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
  tzset();

  time_t now = time(nullptr);
  while (now < 24 * 3600) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("\nOrario sincronizzato correttamente!");

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("[ERRORE] Inizializzazione SD fallita!");
    return;
  }
  Serial.println("Scheda SD pronta.");
  listDir();
  delay(2000);

  // File testFileWrite = SD.open("/test_sd.txt", FILE_WRITE);
  // if (testFileWrite) {
  //   testFileWrite.println("Caro Arduino, questo file proviene dalla SD!");
  //   testFileWrite.println("Temperatura: 24.0 C");
  //   testFileWrite.write(battute, len_battute);
  //   testFileWrite.close();
  //   Serial.println("File di test scritto sulla SD con successo.");
  // } else {
  //   Serial.println("[ERRORE] Impossibile SCRIVERE sulla SD. Controlla il pin CS o la formattazione.");
  // }

  Serial.println("TEST MAIL  DIGITA:");
  Serial.println("1 - TEST MAIL SEMPLICE");
  Serial.println("2 - TEST MAIL CON ALLEGATI MISTI (SD e buffer in memoria)");
}

void loop() {

  // Lettura input seriale
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    handleSerialCommand(line);
  }
}

void handleSerialCommand(const String& lineRaw) {

  String line = lineRaw;
  line.trim();
  if (line.length() == 0) return;

  Serial.printf("ESEGUO... %s\n", line.c_str());

  // single-char commands
  char c = line.charAt(0);
  if (isDigit(c)) {
    int cmd = atoi(line.c_str());
    //reqcount++;
    switch (cmd) {
      case 1:
        testmail1();
        break;
      case 2:
        testmail2();
        break;
      case 3:
        Serial.printf("RAM Libera totale finale: %d bytes\n", ESP.getFreeHeap());
        break;
      default:
        Serial.println("Comando numerico non gestito.");
        break;
    }
    return;
  }
}
//************************************************
void testmail1() {

  Serial.printf("RAM Libera totale finale: %d bytes\n", ESP.getFreeHeap());
  delay(1000);
  // blocco temporaneo
  {

#ifdef ENABLE_STARTTLS
    WiFiClient basic_client;
    ESP_SSLClient smtp_client;
    SmtpMini mailer(smtp_client);
#else
    WiFiClientSecure smtp_client;
    SmtpMini mailer(smtp_client);
#endif

    mailer.begin(AUTHOR_EMAIL, AUTHOR_PASSWORD);
    //mailer.begin(AUTHOR_EMAIL, "xxxxxxxxxxxxxxx");  // prova ERRORE
    
    Serial.println("Invio email...");
    bool esito = mailer.sendEmail(RECIPIENT_EMAIL,
                                  "Test Leggero",
                                  BODY_TXT);

    Serial.println("Spedita!");

    if (esito) {
      Serial.println("Email inviata con successo!");
    } else {
      // L'invio è fallito, interroghiamo la libreria
      helpError(mailer.getLastError());
    }

    smtp_client.flush();
    smtp_client.stop();
  }  // end blocco temporaneo

  Serial.printf("RAM Libera totale finale: %d bytes\n", ESP.getFreeHeap());
}

//========================================
void testmail2() {

  Serial.printf("RAM Libera totale finale: %d bytes\n", ESP.getFreeHeap());
  delay(1000);


  //SMTPAttachment singoloAllegato[1] = { {"/report.csv"} };
	// esempio di allegati misti
	//prime 2 files e 3 buufer in RAM 
  SMTPAttachment listaAllegati[3] = {
    { "/file1.csv" },
    { "/test_sd.txt" },
    { "BATTUTE.txt", battute, len_battute }  // buffer in memoria
  };


#ifdef ENABLE_STARTTLS
  WiFiClient basic_client;
  ESP_SSLClient smtp_client;
  SmtpMini mailer(smtp_client);
#else
  WiFiClientSecure smtp_client;
  SmtpMini mailer(smtp_client);
#endif

  mailer.begin(AUTHOR_EMAIL, AUTHOR_PASSWORD, &SD);

  Serial.println("Inizio invio email cumulativa...SD");

  bool esito = mailer.sendEmail(RECIPIENT_EMAIL, "Report Dati da SD Card",
                                BODY_TXT,
                                false,
                                listaAllegati,
                                3);

  if (esito) {
    Serial.println("Email inviata con successo!");
  } else {
    helpError(mailer.getLastError());
  }
  smtp_client.flush();
  smtp_client.stop();

  Serial.printf("RAM Libera totale finale: %d bytes\n", ESP.getFreeHeap());
}
//=============================
void helpError(int err) {
  if (err == -1) Serial.println("Causa: Il server ha smesso di rispondere (Timeout).");
  else if (err == -2) Serial.println("Causa: Impossibile connettersi al server (Rete o SSL fallito).");
  else if (err == 535) Serial.println("Causa: Autenticazione fallita (Email o password app errata).");
  else if (err == 550) Serial.println("Causa: Destinatario non valido o inesistente.");
  else Serial.printf("Causa: Errore risposto dal server SMTP. Codice: %d\n", err);
}

//=============================
void listDir() {

  File root = SD.open("/");
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }

  Serial.printf("Listing directory: %s\n", "/");

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.printf("DIR  %s\n", file.name());
    } else {
      Serial.printf("%-18s Size   %d byte\n", file.name(), file.size());
    }
    file = root.openNextFile();
  }
  Serial.print("\n======= END ===============\n");
}
