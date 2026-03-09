# Coupure automatique d'un relais par SMS (SIM800L + Arduino + LCD I2C)

Ce dépôt contient un exemple **prêt à tester** pour ton besoin :

- tu envoies un SMS au SIM800L avec une durée au format `HH:MM:SS` (ex: `03:00:00`),
- l'Arduino affiche le temps (`3h 00m 00s`) sur l'écran LCD I2C,
- le compte à rebours démarre,
- et à `00:00:00` le relais se coupe automatiquement.

## Fichier principal

- `relay_timer_sms.ino`

## Matériel

- Arduino UNO/Nano (ou compatible)
- Module SIM800L
- Écran LCD I2C (16x2)
- Module relais
- Alimentation SIM800L **stable** (très important)

## Bibliothèques Arduino

- `LiquidCrystal_I2C`
- `SoftwareSerial` (intégrée Arduino AVR)

## Câblage utilisé dans l'exemple

- **SIM800L TX** -> Arduino D7 (RX SoftwareSerial)
- **SIM800L RX** -> Arduino D8 (TX SoftwareSerial)
- **Relais IN** -> Arduino D4
- **LCD I2C SDA/SCL** -> SDA/SCL Arduino

> ⚠️ Le SIM800L fonctionne en logique/alimentation spécifiques (souvent ~4V et pics de courant élevés). Utilise une alimentation adaptée et un niveau logique sûr côté RX SIM800L.

## Utilisation

1. Flasher `relay_timer_sms.ino`.
2. Ouvrir le moniteur série à `9600` bauds.
3. Attendre l'initialisation GSM.
4. Envoyer un SMS vers la SIM : `03:00:00`.
5. Le relais s'active, l'écran affiche le temps restant, puis le relais se coupe à la fin.

## Commandes SMS supportées

- `HH:MM:SS` (ex: `00:10:30`) : lance le minuteur et active le relais.
- `STOP` : arrête immédiatement le minuteur et coupe le relais.

Si le format est invalide, le module répond par SMS avec un message d'erreur.
