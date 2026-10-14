# SmtpMini

Minimal SMTP client for **ESP32** (Arduino core) with support for:

- plain text or HTML email bodies
- attachments from **RAM**
- attachments from **filesystem** (`SD`, `LittleFS`, or any `fs::FS` compatible filesystem)
- streaming Base64 encoding to keep memory usage low
- **SMTPS (port 465)** as the default mode
- **STARTTLS (port 587)** as an optional mode using `ESP_SSLClient`
- optional `Date:` header for better compatibility with SMTP servers expecting complete email headers

The goal of the library is to stay **small, simple, and practical** for Arduino projects that need to send emails without bringing in heavy dependencies.

---

## Features

- SMTP over a secure client
- secure email sending with TLS/SSL
- plain text or HTML body
- multiple attachments
- attachments from RAM and/or filesystem in the same email
- streaming Base64 encoding
- support for:
  - **implicit TLS / SMTPS** on port `465` as the default mode
  - **STARTTLS** on port `587` when enabled
- optional `Date:` header for better compatibility with stricter SMTP servers
- simple API:
  - `begin()`
  - `setSmtpServer()`
  - `setSMTPtime()`
  - `sendEmail()`
  - `getLastError()`
- optional debug logging via `SMTP_MINI_DEBUG`

---

## Requirements

- ESP32 board with Arduino core
- Wi-Fi connection
- SMTP server supporting either:
  - **SMTPS** on port `465`
  - **STARTTLS** on port `587`
- For Gmail, an **app password** is required

> **Important**
> - Default mode is SMTPS on port `465` using `WiFiClientSecure`.
> - STARTTLS on port `587` is available only when `SMTP_USE_STARTTLS` is enabled, using `ESP_SSLClient`.
> - The library can optionally include the `Date:` header to improve compatibility with stricter SMTP servers.
> - The destination email address is sent as a single `RCPT TO`.
> - The library does **not** close the client automatically; the sketch decides when to stop the connection.
> - `QUIT` is sent as a best-effort SMTP close command, but a successful message is already considered delivered once the server accepts it after `DATA`.

---

## Installation

Copy the library folder into your Arduino `libraries/` directory, or add it as a local library in PlatformIO.

---

## Quick start

### Default mode: SMTPS on port 465

```cpp
#include <WiFi.h>
#include <SmtpMini.h>
#include <SD.h>

void setup() {
  Serial.begin(115200);

  WiFi.begin("YOUR_WIFI", "YOUR_WIFI_PASSWORD");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi connected");

  if (!SD.begin()) {
    Serial.println("SD init failed");
  }

  WiFiClientSecure smtp_client;
  smtp_client.setInsecure();
  SmtpMini mailer(smtp_client);

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

  smtp_client.flush();
  smtp_client.stop();

  if (ok) {
    Serial.println("Email sent successfully");
  } else {
    Serial.printf("SMTP error: %d\n", mailer.getLastError());
  }
}

void loop() {}
```

### STARTTLS on port 587

> This example is only valid if `SMTP_USE_STARTTLS` is enabled in `SmtpMini.h`.

```cpp
#include <WiFi.h>
#include <SmtpMini.h>

void setup() {
  Serial.begin(115200);

  WiFi.begin("YOUR_WIFI", "YOUR_WIFI_PASSWORD");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

 WiFiClient basic_client;
 ESP_SSLClient smtp_client;
 SmtpMini mailer(smtp_client);

  mailer.begin("sender@gmail.com", "your_app_password");
  mailer.setSmtpServer("smtp.gmail.com", 587);

  bool ok = mailer.sendEmail(
    "recipient@example.com",
    "STARTTLS test",
    "Hello from STARTTLS!",
    false
  );

  smtp_client.flush();
  smtp_client.stop();

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

Sets sender credentials and, optionally, the filesystem used for file attachments.

```cpp
mailer.begin(mail, pass, &SD);
mailer.begin(mail, pass, &LittleFS);
mailer.begin(mail, pass);          // RAM attachments only
```

> The strings are **not copied**: they must stay valid for the whole program.

### `setSmtpServer()`

```cpp
void setSmtpServer(const char* host, uint16_t port = 465);
```

Overrides the default host and port. The host string is **not copied** (use a literal or a global).

### `setSMTPtime()`

```cpp
void setSMTPtime(const char* timestr);
```

Sets the optional `Date:` header value used by the message builder.

Example:

```cpp
configTime(3600, 3600, "pool.ntp.org");

struct tm t;
if (getLocalTime(&t)) {
  static char dateBuf[40];
  strftime(dateBuf, sizeof(dateBuf), "%a, %d %b %Y %H:%M:%S %z", &t);
  mailer.setSMTPtime(dateBuf);
}
```

If `setSMTPtime()` is never called, no `Date:` header is sent.

### `sendEmail()`

```cpp
bool sendEmail(const char* to, const char* subject, const char* body,
               bool isHtml = false,
               SMTPAttachment* attachments = nullptr,
               size_t attachmentCount = 0);
```

| Parameter | Description |
| :--- | :--- |
| `to` | Recipient (a single `RCPT TO`) |
| `subject` | Subject |
| `body` | Message body |
| `isHtml` | `true` = `text/html`, `false` = `text/plain` (UTF-8) |
| `attachments` | Array of `SMTPAttachment` (or `nullptr`) |
| `attachmentCount` | Number of elements in the array |

Returns `true` if the server accepted the message (`250` after `DATA`).

### `getLastError()`

```cpp
int getLastError() const;
```

Returns the last error code. On success, it returns `0`.

---

## Attachments

```cpp
struct SMTPAttachment {
  char nomeFile[64] = { "\0" };       // name (RAM) or path (filesystem)
  const void* bufferRAM = nullptr;    // leave nullptr for filesystem files
  size_t lunghezzaRAM = 0;            // leave 0 for filesystem files
};
```

### From the filesystem

```cpp
SMTPAttachment a;
strcpy(a.nomeFile, "/logs/data.csv");     // requires a filesystem in begin()
```

### From RAM

```cpp
const char text[] = "Generated in RAM";

SMTPAttachment b;
strcpy(b.nomeFile, "log.txt");            // name shown in the email
b.bufferRAM    = text;
b.lunghezzaRAM = strlen(text);
```

### Rules

- If `bufferRAM != nullptr` **and** `lunghezzaRAM > 0` → sent from RAM.
- Otherwise, if `nomeFile` is not empty → read from the filesystem passed to `begin()`.
- The name shown in the email is the last path component (`/logs/data.csv` → `data.csv`).
- `nomeFile` holds at most 63 characters.
- If the file cannot be opened or is empty, it is skipped.
- All attachments are sent as `application/octet-stream`.
- RAM and filesystem attachments can be mixed in the same email.

---

## Error codes

`getLastError()` returns:

| Code | Meaning |
| :--- | :--- |
| `0` | No error recorded / successful send |
| `-1` | Timeout or connection closed while waiting for a server reply |
| `-2` | TCP/TLS connection to the server failed |
| *other* | SMTP code received instead of the expected one (for example `535`, `554`) |

Common SMTP codes:

| Code | Meaning |
| :--- | :--- |
| `220` | Service ready / ready to start TLS |
| `221` | Service closing transmission channel |
| `235` | Authentication succeeded |
| `250` | Command accepted |
| `334` | Authentication challenge |
| `354` | Start mail input |
| `535` | Authentication failed |
| `550` | Mailbox unavailable / invalid recipient |

> **Note:** the code is stored whenever the reply does not match the expected one, including server greeting, authentication, `MAIL FROM`, `RCPT TO`, `DATA`, `STARTTLS`, and the final message acceptance after `DATA`. Enable `SMTP_MINI_DEBUG` to inspect the server dialogue.

---

## Client lifecycle

- The library does not close the client automatically.
- The sketch owns the client and decides when to stop it.
- `QUIT` is sent as a best-effort SMTP close command.
- A message is considered successfully sent once the server accepts it after `DATA` with `250`.

---

## Debug

Uncomment in `SmtpMini.h`:

```cpp
#define SMTP_MINI_DEBUG
```

When enabled, the library prints the SMTP dialogue (`C:` / `S:` lines), the authentication result and the attachment progress. Connection and SMTP error messages are printed on `Serial` regardless of this option.

---

## Security note

The library calls `setInsecure()` on the client inside `sendEmail()`: the connection is **encrypted**, but the **server certificate is not verified**.

---

## Limitations

- Authentication: `AUTH LOGIN` only
- One recipient per message
- Single body part (text **or** HTML)
- ESP32 only

---

## Example

See [`examples/testsmail.ino`](examples/testsmail.ino): Wi-Fi, SD card, simple email and mixed SD/RAM attachments.

---

## License

MIT
