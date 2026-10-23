/*
 Banc de test 433 MHz pour la telecommande du poele Piazzetta.
 Capte les trames OOK et les envoie au PC par l'USB ; reemet les trames
 que le PC lui demande. A utiliser avec la page test-bench/web/test-bench.html.

 Carte : "ESP32 Dev Module" (ESP32 DevKit) + Ra-02 (SX1278).
 Bibliotheque : RadioLib (deja installee). Pas besoin de rtl_433_ESP ni de
 build_opt.h pour ce sketch.

 Moniteur serie : 115200 bauds. Protocole (une ligne par message) :
   ESP -> PC
     F <ms> <rssi> <n> <d1>,<d2>,...   trame captee : durees en us, en
                                       alternance haut, bas, haut... ;
                                       n = nombre de durees
     I <texte>                         information
     E <texte>                         erreur
     OK <commande>                     commande executee
   PC -> ESP
     T <rep> <pause_ms> <d1>,<d2>,...  emettre (durees haut, bas, haut...)
     S <dBm>                           seuil RSSI de capture (ex. -78)
     P <dBm>                           puissance d'emission (2 a 17)
     Q <MHz>                           frequence (ex. 433.92)
     ?                                 etat
*/

#include <SPI.h>
#include <RadioLib.h>
#include "soc/gpio_reg.h"

// --- Brochage (identique au recepteur rtl_433_ESP) ---
#define PIN_SCK 18
#define PIN_MISO 19
#define PIN_MOSI 23
#define PIN_CS 5
#define PIN_RST 14
#define PIN_DIO0 26
#define PIN_DIO1 33
#define PIN_DIO2 32  // donnees OOK : sortie en reception, entree en emission
#define PIN_LED 2

// --- Reglages ---
#define FIN_TRAME_US 40000   // silence qui termine une trame : la synchro fait ~8 ms et le poele marque
                             // parfois une pause au milieu de ses ecrans de 172 ms (coupes en deux a 15 ms)
#define TRAME_MAX_US 1500000 // une trame plus longue est coupee
#define MAX_DUREES 1600
#define MIN_IMPULSIONS 8
#define TAILLE_FRONTS 4096   // tampon circulaire des fronts (puissance de 2)

SPIClass spiRadio(VSPI);
Module moduleRadio(PIN_CS, PIN_DIO0, PIN_RST, PIN_DIO1, spiRadio);
SX1278 radio(&moduleRadio);

float frequence = 433.92;
int seuilRssi = -78;
int puissance = 10;

// Fronts captes par l'interruption : instant (us) et niveau apres le front
struct Front {
  uint32_t t;
  uint8_t niveau;
};
volatile Front fronts[TAILLE_FRONTS];
volatile uint32_t teteFronts = 0;
uint32_t queueFronts = 0;

// Trame en cours d'assemblage
bool enTrame = false;
uint32_t debutTrame = 0;
uint32_t dernierFront = 0;
uint8_t dernierNiveau = 0;
uint32_t durees[MAX_DUREES];
int nbDurees = 0;
int nbImpulsions = 0;
float rssiMax = -200;
uint32_t dernierRssiUs = 0;
uint32_t dernierFortUs = 0;  // dernier instant ou le RSSI depassait le seuil

// Bruit de fond, mesure hors trame
float bruit = -100;
uint32_t dernierBruitMs = 0;
uint32_t dernierInfoBruitMs = 0;

// Ligne recue du PC
char ligne[20000];
int lgLigne = 0;

void IRAM_ATTR surFront() {
  uint32_t t = micros();
  uint8_t niveau = (REG_READ(GPIO_IN1_REG) >> (PIN_DIO2 - 32)) & 1;
  uint32_t i = teteFronts & (TAILLE_FRONTS - 1);
  fronts[i].t = t;
  fronts[i].niveau = niveau;
  teteFronts++;
}

void verifier(int16_t etat, const char* etape) {
  if (etat != RADIOLIB_ERR_NONE) {
    Serial.printf("E %s : code %d\n", etape, etat);
  }
}

void configurerRadio() {
  verifier(radio.beginFSK(), "beginFSK");
  verifier(radio.setFrequency(frequence), "setFrequency");
  verifier(radio.setOOK(true), "setOOK");
  verifier(radio.setCrcFiltering(false), "setCrcFiltering");
  // Reglages repris de rtl_433_ESP, valides sur ce montage le 23/10/2026
  verifier(radio.setDataShapingOOK(2), "setDataShapingOOK");
  verifier(radio.setOokThresholdType(RADIOLIB_SX127X_OOK_THRESH_PEAK), "setOokThresholdType");
  verifier(radio.setOokPeakThresholdDecrement(RADIOLIB_SX127X_OOK_PEAK_THRESH_DEC_1_1_CHIP), "setOokPeakThresholdDecrement");
  verifier(radio.setOokPeakThresholdStep(RADIOLIB_SX127X_OOK_PEAK_THRESH_STEP_0_5_DB), "setOokPeakThresholdStep");
  verifier(radio.setOokFixedOrFloorThreshold(0x50), "setOokFixedOrFloorThreshold");
  verifier(radio.setBitRate(1.2), "setBitRate");
  verifier(radio.setRxBandwidth(250), "setRxBandwidth");
  verifier(radio.setRSSIConfig(RADIOLIB_SX127X_RSSI_SMOOTHING_SAMPLES_2, RADIOLIB_SX127X_OOK_AVERAGE_OFFSET_0_DB), "setRSSIConfig");
  verifier(moduleRadio.SPIsetRegValue(RADIOLIB_SX127X_REG_PREAMBLE_DETECT, RADIOLIB_SX127X_PREAMBLE_DETECTOR_OFF), "preambleDetectOff");
  verifier(radio.setDirectSyncWord(0, 0), "setDirectSyncWord");
  verifier(radio.disableBitSync(), "disableBitSync");
  verifier(radio.setOutputPower(puissance), "setOutputPower");
}

void ecouter() {
  pinMode(PIN_DIO2, INPUT);
  verifier(radio.receiveDirect(), "receiveDirect");
  enTrame = false;
  queueFronts = teteFronts;
  attachInterrupt(digitalPinToInterrupt(PIN_DIO2), surFront, CHANGE);
}

void finirTrame() {
  enTrame = false;
  digitalWrite(PIN_LED, LOW);
  // La derniere duree est un "haut" sans fin connue : on la ferme au dernier front
  if (nbImpulsions < MIN_IMPULSIONS || rssiMax < seuilRssi || nbDurees < 2) return;
  int n = nbDurees;
  if (n % 2 == 0) n--;  // finir sur un "haut"
  Serial.printf("F %lu %d %d ", (unsigned long)(debutTrame / 1000), (int)rssiMax, n);
  for (int i = 0; i < n; i++) {
    if (i) Serial.print(',');
    Serial.print(durees[i]);
  }
  Serial.print('\n');
}

void traiterFront(uint32_t t, uint8_t niveau) {
  if (!enTrame) {
    if (niveau != 1) return;  // une trame commence par un front montant
    // Le comparateur OOK bavarde dans le bruit : on ne demarre que sur un signal assez fort
    float r = radio.getRSSI(false, true);
    if (r < seuilRssi) return;
    enTrame = true;
    debutTrame = t;
    dernierFront = t;
    dernierNiveau = 1;
    nbDurees = 0;
    nbImpulsions = 1;
    rssiMax = r;
    dernierRssiUs = micros();
    dernierFortUs = dernierRssiUs;
    digitalWrite(PIN_LED, HIGH);
    return;
  }
  if (niveau == dernierNiveau) return;  // front perdu : on ignore
  if (nbDurees < MAX_DUREES) durees[nbDurees++] = t - dernierFront;
  if (niveau == 1) nbImpulsions++;
  dernierFront = t;
  dernierNiveau = niveau;
  if (nbDurees >= MAX_DUREES || t - debutTrame > TRAME_MAX_US) finirTrame();
}

void emettre(int rep, int pauseMs, uint32_t* d, int n) {
  detachInterrupt(digitalPinToInterrupt(PIN_DIO2));
  if (enTrame) finirTrame();
  digitalWrite(PIN_LED, HIGH);
  // En emission continue, le SX127x echantillonne DIO2 au rythme du debit binaire (horloge DCLK).
  // A 1,2 kbit/s (reglage de reception), nos bits de 416 us seraient deformes : on passe au
  // debit OOK maximal (echantillon toutes les ~30 us) et sans mise en forme, le temps d'emettre.
  verifier(radio.setBitRate(32.768), "setBitRate TX");
  verifier(radio.setDataShapingOOK(0), "setDataShapingOOK TX");
  verifier(radio.transmitDirect(), "transmitDirect");
  pinMode(PIN_DIO2, OUTPUT);
  digitalWrite(PIN_DIO2, LOW);
  delay(2);  // laisser l'emetteur demarrer
  for (int r = 0; r < rep; r++) {
    int64_t cible = esp_timer_get_time();
    for (int i = 0; i < n; i++) {
      digitalWrite(PIN_DIO2, (i % 2 == 0) ? HIGH : LOW);
      cible += d[i];
      while (esp_timer_get_time() < cible) {}
    }
    digitalWrite(PIN_DIO2, LOW);
    if (r < rep - 1) delay(pauseMs);
  }
  verifier(radio.standby(), "standby");
  // Le 23/10/2026, apres une emission, le recepteur est reste desensibilise (~35 dB de moins)
  // et captait du bruit en continu : on repart d'une radio entierement reconfiguree.
  configurerRadio();
  digitalWrite(PIN_LED, LOW);
  ecouter();
}

void traiterLigne(char* s) {
  while (*s == ' ') s++;
  if (*s == 0) return;
  char cmd = s[0];
  char* args = s + 1;
  if (cmd == 'T') {
    char* p = args;
    int rep = strtol(p, &p, 10);
    int pauseMs = strtol(p, &p, 10);
    static uint32_t d[MAX_DUREES];
    int n = 0;
    while (*p && n < MAX_DUREES) {
      while (*p == ' ' || *p == ',') p++;
      if (!*p) break;
      d[n++] = strtoul(p, &p, 10);
    }
    rep = constrain(rep, 1, 20);
    pauseMs = constrain(pauseMs, 0, 2000);
    if (n < 3) {
      Serial.println("E T : trop peu de durees");
      return;
    }
    emettre(rep, pauseMs, d, n);
    Serial.printf("OK T %d x %d durees\n", rep, n);
  } else if (cmd == 'S') {
    seuilRssi = atoi(args);
    Serial.printf("OK S %d\n", seuilRssi);
  } else if (cmd == 'P') {
    puissance = constrain(atoi(args), 2, 17);
    verifier(radio.setOutputPower(puissance), "setOutputPower");
    ecouter();
    Serial.printf("OK P %d\n", puissance);
  } else if (cmd == 'Q') {
    float f = atof(args);
    if (f < 410 || f > 525) {
      Serial.println("E Q : frequence hors bande du SX1278");
      return;
    }
    frequence = f;
    verifier(radio.setFrequency(frequence), "setFrequency");
    ecouter();
    Serial.printf("OK Q %.3f\n", frequence);
  } else if (cmd == '?') {
    Serial.printf("I etat frequence=%.3f seuil=%d puissance=%d bruit=%.0f\n", frequence, seuilRssi, puissance, bruit);
  } else {
    Serial.printf("E commande inconnue : %c\n", cmd);
  }
}

void setup() {
  Serial.setRxBufferSize(16384);
  Serial.setTxBufferSize(8192);
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  delay(300);
  spiRadio.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
  configurerRadio();
  ecouter();
  Serial.printf("I PRET banc poele 433 frequence=%.3f seuil=%d puissance=%d\n", frequence, seuilRssi, puissance);
}

void loop() {
  // 1. Fronts captes par l'interruption
  while (queueFronts != teteFronts) {
    if (teteFronts - queueFronts > TAILLE_FRONTS) {
      queueFronts = teteFronts - TAILLE_FRONTS;  // debordement : on perd les plus anciens
      static uint32_t dernierMessage = 0;
      if (millis() - dernierMessage > 5000) {  // un message toutes les 5 s au plus
        Serial.println("E tampon de fronts plein (la radio capte du bruit en continu ?)");
        dernierMessage = millis();
      }
    }
    uint32_t i = queueFronts & (TAILLE_FRONTS - 1);
    traiterFront(fronts[i].t, fronts[i].niveau);
    queueFronts++;
  }

  uint32_t maintenant = micros();
  if (enTrame) {
    // 2. RSSI pendant la trame (on garde le maximum)
    if (maintenant - dernierRssiUs > 1000) {
      float r = radio.getRSSI(false, true);
      if (r > rssiMax) rssiMax = r;
      if (r >= seuilRssi) dernierFortUs = maintenant;
      dernierRssiUs = maintenant;
    }
    // 3. Fin de trame : silence sur la broche, ou plus de signal fort (le bruit continue de basculer)
    if (dernierNiveau == 0 && maintenant - dernierFront > FIN_TRAME_US) finirTrame();
    else if (maintenant - dernierFortUs > FIN_TRAME_US) finirTrame();
    else if (maintenant - debutTrame > TRAME_MAX_US) finirTrame();
  } else if (millis() - dernierBruitMs > 200) {
    // 4. Bruit de fond (moyenne glissante)
    float r = radio.getRSSI(false, true);
    bruit = (bruit < -150) ? r : bruit * 0.95 + r * 0.05;
    dernierBruitMs = millis();
    if (millis() - dernierInfoBruitMs > 10000) {
      Serial.printf("I bruit=%.0f\n", bruit);
      dernierInfoBruitMs = millis();
    }
  }

  // 5. Commandes du PC
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      ligne[lgLigne] = 0;
      if (lgLigne) traiterLigne(ligne);
      lgLigne = 0;
    } else if (lgLigne < (int)sizeof(ligne) - 1) {
      ligne[lgLigne++] = c;
    }
  }
}
