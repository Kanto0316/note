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
bool modemConfigured = false;

// ------------------ Timer ------------------
bool timerActive = false;
unsigned long remainingSeconds = 0;
unsigned long lastTickMs = 0;
bool chargeModeActive = false;
bool pauseBlinkVisible = true;
unsigned long lastPauseBlinkMs = 0;

void formatClock(unsigned long totalSec, char *out, size_t outSize) {
  unsigned int hh = totalSec / 3600UL;
  unsigned int mm = (totalSec % 3600UL) / 60UL;
  unsigned int ss = totalSec % 60UL;
  snprintf(out, outSize, "%02u:%02u:%02u", hh, mm, ss);
}

void sendAT(const char *cmd, unsigned long waitMs = 400) {
  sim800.println(cmd);
  delay(waitMs);
  while (sim800.available()) {
    Serial.write(sim800.read());
  }
}

bool sendATExpect(const char *cmd, const char *expected, unsigned long timeoutMs = 2000) {
  sim800.println(cmd);

  String response;
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    while (sim800.available()) {
      char c = (char)sim800.read();
      response += c;
      Serial.write(c);
    }

    if (response.indexOf(expected) >= 0) {
      return true;
    }

    delay(10);
  }

  Serial.print("[WARN] Timeout commande: ");
  Serial.println(cmd);
  return false;
}

bool waitForModemReady() {
  // A froid, le SIM800 peut prendre plusieurs secondes avant de repondre.
  for (uint8_t attempt = 0; attempt < 15; attempt++) {
    if (sendATExpect("AT", "OK", 1000)) {
      return true;
    }
    delay(500);
  }

  Serial.println("[ERROR] SIM800 non pret");
  return false;
}

bool configureModemSms() {
  if (!waitForModemReady()) {
    return false;
  }

  // Evite les reponses en echo qui perturbent le parsing.
  sendATExpect("ATE0", "OK", 1000);

  bool ok = true;
  ok &= sendATExpect("AT+CMGF=1", "OK", 1500);        // Mode texte SMS
  ok &= sendATExpect("AT+CNMI=2,2,0,0,0", "OK", 1500); // Push direct des nouveaux SMS

  if (ok) {
    clearAllStoredSms();
    Serial.println("[READY] Modem SMS configure");
  }

  return ok;
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

void clearAllStoredSms() {
  // Supprime tous les SMS stockes dans la memoire du module.
  // 1,4 = efface tous les messages (lus, non lus, envoyes, brouillons).
  sim800.println("AT+CMGD=1,4");
  delay(800);
  while (sim800.available()) {
    Serial.write(sim800.read());
  }
  Serial.println("[SMS] Boite SMS videe");
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
  if (hh > 24) return false;
  if (hh == 24 && (mm > 0 || ss > 0)) return false;

  secondsOut = (unsigned long)hh * 3600UL + (unsigned long)mm * 60UL + (unsigned long)ss;
  return secondsOut > 0;
}

void displayRemaining(unsigned long totalSec) {
  char line2[17];
  formatClock(totalSec, line2, sizeof(line2));

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("MINUTEUR ACTIF ");
  lcd.setCursor(0, 1);
  lcd.print(line2);

  Serial.print("[TIMER] ");
  Serial.println(line2);
}

void displayPauseScreen(bool showValue) {
  char line2[17];
  if (showValue) {
    formatClock(remainingSeconds, line2, sizeof(line2));
  } else {
    snprintf(line2, sizeof(line2), "        ");
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("PAUSE - SMS PLAY");
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

void startTimer(unsigned long totalSec) {
  remainingSeconds = totalSec;
  timerActive = true;
  lastTickMs = millis();
  pauseBlinkVisible = true;

  digitalWrite(RELAY_PIN, HIGH); // Relais ON
  displayRemaining(remainingSeconds);
}

void stopTimer(const String &reason) {
  timerActive = false;

  digitalWrite(RELAY_PIN, LOW); // Relais OFF

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Relais coupe");
  lcd.setCursor(0, 1);
  lcd.print(reason);

  Serial.print("[INFO] ");
  Serial.println(reason);
}

void finishTimer(const String &reason) {
  stopTimer(reason);
  remainingSeconds = 0;
}

void showInitialScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SIM800 pret SMS");
  lcd.setCursor(0, 1);
  lcd.print("Cmd: HH:MM:SS");
}

void activateChargeMode() {
  chargeModeActive = true;
  digitalWrite(RELAY_PIN, HIGH); // Relais ON

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Mode CHARGE ON");
  lcd.setCursor(0, 1);
  lcd.print("Attente FULL");

  Serial.println("[INFO] Mode CHARGE active");
}

void deactivateChargeMode() {
  chargeModeActive = false;
  digitalWrite(RELAY_PIN, LOW); // Relais OFF
  showInitialScreen();

  Serial.println("[INFO] Mode CHARGE termine");
}

void processSmsBody(const String &bodyRaw) {
  String body = bodyRaw;
  body.trim();
  body.toUpperCase();

  Serial.print("[SMS] Message: ");
  Serial.println(body);

  if (body == "CHARGE FULL") {
    if (chargeModeActive) {
      deactivateChargeMode();
    } else {
      Serial.println("[INFO] CHARGE FULL ignore (mode CHARGE inactif)");
    }
    return;
  }

  if (body == "CHARGE") {
    // N'autoriser que si aucun timer actif ou en pause, et hors mode charge deja actif.
    if (!chargeModeActive && !timerActive && remainingSeconds == 0) {
      activateChargeMode();
    } else {
      Serial.println("[INFO] CHARGE refuse (timer actif/pause ou mode deja actif)");
    }
    return;
  }

  if (chargeModeActive) {
    Serial.println("[INFO] Mode CHARGE actif, commande ignoree (envoyer CHARGE FULL)");
    return;
  }

  if (body == "PAUSE") {
    if (timerActive) {
      stopTimer("Pause");
      pauseBlinkVisible = true;
      lastPauseBlinkMs = millis();
      displayPauseScreen(true);
    }
    return;
  }

  if (body == "STOP") {
    if (timerActive || remainingSeconds > 0) {
      finishTimer("Stop");
    }
    return;
  }

  if (body == "PLAY") {
    if (!timerActive && remainingSeconds > 0) {
      startTimer(remainingSeconds);
    }
    return;
  }

  // Si une execution est deja en cours, ignorer toute nouvelle demande
  // tant qu'un message PAUSE ou STOP n'a pas ete recu.
  if (timerActive) {
    Serial.println("[INFO] Minuteur deja actif, commande ignoree (envoyer PAUSE/STOP)");
    return;
  }

  unsigned long seconds = 0;
  if (!parseTimeToSeconds(body, seconds)) {
    return;
  }

  startTimer(seconds);
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
        clearAllStoredSms();
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
  modemConfigured = configureModemSms();

  showInitialScreen();

  Serial.println("[READY] Envoie un SMS HH:MM:SS");
}

void loop() {
  if (!modemConfigured) {
    modemConfigured = configureModemSms();
    if (!modemConfigured) {
      delay(1500);
      return;
    }
  }

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
        finishTimer("Temps termine");
      }
    }
  } else if (remainingSeconds > 0 && !chargeModeActive) {
    unsigned long now = millis();
    if (now - lastPauseBlinkMs >= 500UL) {
      lastPauseBlinkMs = now;
      pauseBlinkVisible = !pauseBlinkVisible;
      displayPauseScreen(pauseBlinkVisible);
    }
  }
}
