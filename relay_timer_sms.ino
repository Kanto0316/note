#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>

// ------------------ Configuration broches ------------------
const uint8_t SIM800_RX_PIN = 7;   // Arduino lit SIM800 TX
const uint8_t SIM800_TX_PIN = 8;   // Arduino écrit vers SIM800 RX
const uint8_t RELAY_PIN = 4;

// ------------------ LCD I2C ------------------
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ------------------ SIM800 ------------------
SoftwareSerial sim800(SIM800_RX_PIN, SIM800_TX_PIN);
String gsmBuffer;
String lastSender;

// ------------------ Timer ------------------
bool timerActive = false;
unsigned long remainingSeconds = 0;
unsigned long lastTickMs = 0;

void sendAT(const char *cmd, unsigned long waitMs = 400) {
  sim800.println(cmd);
  delay(waitMs);
  while (sim800.available()) {
    Serial.write(sim800.read());
  }
}

void sendSmsTo(const String &number, const String &message) {
  if (number.length() == 0) return;

  sim800.println("AT+CMGF=1");
  delay(300);
  sim800.print("AT+CMGS=\"");
  sim800.print(number);
  sim800.println("\"");
  delay(300);
  sim800.print(message);
  sim800.write(26); // Ctrl+Z
  delay(700);
}

bool parseTimeToSeconds(const String &text, unsigned long &secondsOut) {
  if (text.length() != 8) return false;
  if (text[2] != ':' || text[5] != ':') return false;

  for (int i = 0; i < 8; i++) {
    if (i == 2 || i == 5) continue;
    if (!isDigit(text[i])) return false;
  }

  int hh = text.substring(0, 2).toInt();
  int mm = text.substring(3, 5).toInt();
  int ss = text.substring(6, 8).toInt();

  if (mm > 59 || ss > 59) return false;

  secondsOut = (unsigned long)hh * 3600UL + (unsigned long)mm * 60UL + (unsigned long)ss;
  return secondsOut > 0;
}

void displayRemaining(unsigned long totalSec) {
  unsigned int hh = totalSec / 3600UL;
  unsigned int mm = (totalSec % 3600UL) / 60UL;
  unsigned int ss = totalSec % 60UL;

  char line1[17];
  char line2[17];
  snprintf(line1, sizeof(line1), "Temps restant:");
  snprintf(line2, sizeof(line2), "%02uh %02um %02us", hh, mm, ss);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);

  Serial.print("[TIMER] ");
  Serial.println(line2);
}

void startTimer(unsigned long totalSec) {
  remainingSeconds = totalSec;
  timerActive = true;
  lastTickMs = millis();

  digitalWrite(RELAY_PIN, HIGH); // Relais ON
  displayRemaining(remainingSeconds);
}

void stopTimer(const String &reason) {
  timerActive = false;
  remainingSeconds = 0;

  digitalWrite(RELAY_PIN, LOW); // Relais OFF

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Relais coupe");
  lcd.setCursor(0, 1);
  lcd.print(reason);

  Serial.print("[INFO] ");
  Serial.println(reason);
}

void processSmsBody(const String &bodyRaw) {
  String body = bodyRaw;
  body.trim();
  body.toUpperCase();

  Serial.print("[SMS] Message: ");
  Serial.println(body);

  if (body == "STOP") {
    stopTimer("Stop par SMS");
    sendSmsTo(lastSender, "Timer arrete. Relais coupe.");
    return;
  }

  unsigned long seconds = 0;
  if (!parseTimeToSeconds(body, seconds)) {
    sendSmsTo(lastSender, "Format invalide. Utilise HH:MM:SS, ex: 03:00:00");
    return;
  }

  startTimer(seconds);
  sendSmsTo(lastSender, "Timer demarre: " + body);
}

void handleGsmLine(String line) {
  line.trim();
  if (line.length() == 0) return;

  Serial.print("[GSM] ");
  Serial.println(line);

  if (line.startsWith("+CMT:")) {
    int firstQuote = line.indexOf('"');
    int secondQuote = line.indexOf('"', firstQuote + 1);
    if (firstQuote >= 0 && secondQuote > firstQuote) {
      lastSender = line.substring(firstQuote + 1, secondQuote);
      Serial.print("[SMS] Expediteur: ");
      Serial.println(lastSender);
    }

    // Le corps SMS est sur la ligne suivante
    unsigned long timeout = millis() + 3000;
    while (millis() < timeout) {
      if (sim800.available()) {
        String body = sim800.readStringUntil('\n');
        processSmsBody(body);
        break;
      }
    }
  }
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  Serial.begin(9600);
  sim800.begin(9600);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Init systeme...");

  delay(1500);
  sendAT("AT");
  sendAT("AT+CMGF=1");      // Mode texte SMS
  sendAT("AT+CNMI=2,2,0,0,0"); // Push direct des nouveaux SMS

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Pret SMS HH:MM:SS");
  lcd.setCursor(0, 1);
  lcd.print("Relais: OFF");

  Serial.println("[READY] Envoie un SMS HH:MM:SS");
}

void loop() {
  // Lecture non bloquante du flux GSM
  while (sim800.available()) {
    char c = (char)sim800.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (gsmBuffer.length() > 0) {
        handleGsmLine(gsmBuffer);
        gsmBuffer = "";
      }
    } else {
      gsmBuffer += c;
      if (gsmBuffer.length() > 180) {
        gsmBuffer = ""; // sécurité mémoire
      }
    }
  }

  if (timerActive) {
    unsigned long now = millis();
    if (now - lastTickMs >= 1000UL) {
      lastTickMs += 1000UL;

      if (remainingSeconds > 0) {
        remainingSeconds--;
      }

      displayRemaining(remainingSeconds);

      if (remainingSeconds == 0) {
        stopTimer("Temps termine");
        sendSmsTo(lastSender, "Temps termine. Relais coupe automatiquement.");
      }
    }
  }
}
