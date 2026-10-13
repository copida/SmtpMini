# SmtpMini

Minimal secure SMTP client for **ESP32** with support for:

- text or HTML email body
- attachments from **RAM**
- attachments from **filesystem** (`SD`, `LittleFS`, or any `fs::FS` compatible filesystem)
- streaming Base64 encoding to keep memory usage low
- **SMTPS (port 465)** as the default mode
- **STARTTLS (port 587)** available when `SMTP_USE_STARTTLS` is enabled

The goal of the library is to stay **small, simple, and practical** for Arduino projects that need to send emails without pulling in heavy dependencies.

---

## Features

- SMTP over a secure client
- Secure email sending with TLS/SSL
- Plain text or HTML body
- Multiple attachments
- Attachments from RAM and/or filesystem in the same email
- Streaming Base64 encoding
- Support for:
  - **implicit TLS / SMTPS** on port `465` as the default mode
  - **STARTTLS** on port `587` when explicitly enabled
- Simple API:
  - `begin()`
  - `setSmtpServer()`
  - `sendEmail()`
  - `getLastError()`
- Optional debug logging via `SMTP_MINI_DEBUG`

---

## Requirements

- ESP32 board with Arduino core
- Wi-Fi connection
- SMTP server supporting either:
  - **SMTPS** on port `465`
  - **STARTTLS** on port `587`
- For Gmail, an **app password** is required

> **Important**
> - `SMTPS` on port `465` is the default and recommended mode(using `WiFiClientSecure`).
> - `STARTTLS` on port `587` is supported only when `SMTP_USE_STARTTLS` is enabled in `SmtpMini.h`.
> - The destination email address is sent as a single `RCPT TO`.
> - The library does not manage client shutdown automatically; close the connection in your sketch when needed.

---

## Installation

Copy the library folder into your Arduino `libraries/` directory, or add it as a local library in PlatformIO.

---

## Quick start

### Example with a filesystem attachment (default SMTPS / 465)

```cpp
#include <WiFi.h>
#include <SmtpMini.h>
#include <SD.h>

WiFiClientSecure secureClient;
SmtpMini mailer(secureClient);

void setup() {
  Serial.begin(115200);

  WiFi.begin("YOUR_WIFI", "YOUR_WIFI_PASSWORD");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi connected");

  secureClient.setInsecure();

  if (!SD.begin()) {
    Serial.println("SD init failed");
  }

  mailer.begin("sender@gmail.com", "your_app_password", &SD);
  mailer.setSmtpServer("smtp.gmail.com", 465);

  SMTPAttachment attachments[1] = {
    { "/report.csv" }
  };

  bool ok = mailer.sendEmail(
    "recipient@example.com",
    "SmtpMini test",
    "Hello! This is a test email.",
    false,
    attachments,
    1
  );

  if (ok) {
    Serial.println("Email sent successfully");
  } else {
    Serial.printf("SMTP error: %d\n", mailer.getLastError());
  }
}

void loop() {}
```

### Example with STARTTLS on port 587

> This example is only valid if `SMTP_USE_STARTTLS` is enabled in `SmtpMini.h`.

```cpp
#include <WiFi.h>
#include <SmtpMini.h>

WiFiClient tcpClient;
ESP_SSLClient sslClient(tcpClient);
SmtpMini mailer(sslClient);

void setup() {
  Serial.begin(115200);

  WiFi.begin("YOUR_WIFI", "YOUR_WIFI_PASSWORD");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  mailer.begin("sender@gmail.com", "your_app_password");
  mailer.setSmtpServer("smtp.gmail.com", 587);

  bool ok = mailer.sendEmail(
    "recipient@example.com",
    "STARTTLS test",
    "Hello from STARTTLS!",
    false
  );

  if (ok) Serial.println("Email sent successfully");
  else    Serial.printf("SMTP error: %d\n", mailer.getLastError());
}

void loop() {}
```

---

## API

### `begin()`

```cpp
bool begin(const char* email, const char* appPassword, fs::FS* fileSystem = nullptr);
```

Initializes the sender credentials and optionally the filesystem used for file-based attachments.

- `email`: sender email address
- `appPassword`: SMTP/app password
- `fileSystem`: optional filesystem pointer

Examples:

```cpp
mailer.begin(mail, pass, &SD);
mailer.begin(mail, pass, &LittleFS);
mailer.begin(mail, pass);
```

If `fileSystem` is omitted, only RAM attachments can be sent.

---

### `setSmtpServer()`

```cpp
void setSmtpServer(const char* host, uint16_t port = 465);
```

Allows using a different SMTP server and port.

Example:

```cpp
mailer.setSmtpServer("smtp.example.com", 587);
```

> The string is not copied internally, so use a string literal or a value that stays valid for the whole program.

---

### `sendEmail()`

```cpp
bool sendEmail(
  const char* to,
  const char* subject,
  const char* body,
  bool isHtml = false,
  SMTPAttachment* attachments = nullptr,
  size_t attachmentCount = 0
);
```

Sends the email message.

Parameters:

- `to`: destination email address
- `subject`: email subject
- `body`: message body
- `isHtml`: set `true` for HTML body
- `attachments`: array of attachments
- `attachmentCount`: number of attachments in the array

---

### `getLastError()`

```cpp
int getLastError() const;
```

Returns the last SMTP/network error code detected by the library.

---

## Attachment structure

```cpp
struct SMTPAttachment {
  char nomeFile[64] = { "\0" };
  const void* bufferRAM = nullptr;
  size_t lunghezzaRAM = 0;
};
```

### File attachment

```cpp
SMTPAttachment a1 = { "/folder/data.csv" };
```

### RAM attachment

```cpp
const char text[] = "Generated in RAM";
SMTPAttachment a2 = { "log.txt", (const void*)text, strlen(text) };
```

Behavior:

- If `bufferRAM != nullptr` and `lunghezzaRAM > 0`, the attachment is sent from RAM.
- Otherwise the library tries to open the file from the configured filesystem.
- If the filename starts with `/`, the leading slash is removed from the display name.
- Only the last path component is used as the name shown in the email.

---

## Error codes

| Code | Meaning |
| :--- | :--- |
| `-1` | Timeout waiting for server response |
| `-2` | TCP/TLS connection failed |
| `220` | SMTP greeting / connection response |
| `235` | Authentication succeeded |
| `334` | Authentication challenge / continuation |
| `354` | Start mail input |
| `535` | Authentication failed |
| `550` | Invalid recipient |
| other | SMTP reply code from the server |

If a filesystem attachment cannot be opened, the library logs the issue in debug mode and continues with the remaining email flow.

---

## Debug

Enable debug output in `SmtpMini.h`:

```cpp
#define SMTP_MINI_DEBUG
```

When enabled, the library prints SMTP dialogue and attachment progress to the serial monitor.

For production builds, comment it out.

---

## Security note

`secureClient.setInsecure()` disables certificate validation. The connection is still encrypted, but the server identity is not verified.

For stronger security, set the CA certificate with `secureClient.setCACert(...)` when possible.

---

## Example sketch

See `examples/testsmail/testsmail.ino` for a complete example using:
- Wi-Fi connection
- SD card
- simple mail
- mixed attachments from SD and RAM
- secure SMTP connection

---

## License

MIT
