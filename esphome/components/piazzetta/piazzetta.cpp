#include "piazzetta.h"

#include <cmath>

#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include "esp_timer.h"
#include "soc/gpio_reg.h"

#include "esphome/core/log.h"

namespace esphome {
namespace piazzetta {

static const char *const TAG = "piazzetta";

// --- Protocole (voir telecommande-poele-433-protocole.md) ---
static const float UNITE_US = 416.67f;          // 2 400 bauds
static const uint8_t ETAT_REPOS = 0x3D, ETAT_MENU = 0xBD;
static const uint8_t TOUCHE_INTERROGER = 0x00, TOUCHE_PLUS = 0x88, TOUCHE_MOINS = 0x8C;
static const uint8_t TOUCHE_VENTILATEUR = 0x84, TOUCHE_PUISSANCE = 0x98;
static const uint8_t TOUCHE_MARCHE = 0x81, TOUCHE_MAINTIEN = 0x61;
static const uint8_t MARQUE_VENTILATEUR = 0x00, MARQUE_PUISSANCE = 0x50;

// --- Reception ---
static const uint32_t FIN_TRAME_US = 40000;     // le poele marque une pause au milieu de ses ecrans
static const uint32_t TRAME_MAX_US = 1500000;
static const size_t MAX_DUREES = 1600;
static const int MIN_IMPULSIONS = 8;

// --- Taches ---
static const uint32_t DELAI_REPONSE_MS = 1500;
static const int ESSAIS_MAX = 4;
static const int PAS_MAX = 8;
// Appui long sur ⏻ calque sur la vraie telecommande : 81, puis 11 trames 61 « bouton maintenu »
// avec cet octet d'etat. Valide depuis le banc : allumage lance par l'ESP.
static const uint8_t ETATS_MAINTIEN[] = {0xBE, 0xBE, 0xBE, 0xBE, 0x3E, 0x3E, 0x3E, 0x3E, 0xBE, 0xBE, 0x3E};
static const int APPUIS_MAINTIEN = sizeof(ETATS_MAINTIEN);
static const uint32_t PERIODE_MAINTIEN_MS = 350;
static const uint32_t SILENCE_TELECOMMANDE_MS = 10000;
static const uint32_t DELAI_JOIGNABLE_MS = 120000;
// Le poele n'envoie l'ecran complet (B5) que si son contenu a change ou apres une touche ;
// sinon il repond l'heure seule (B9). Au demarrage, si aucun ecran principal n'arrive,
// on ouvre le menu ventilateur sans rien regler : il se referme seul, et l'interrogation
// suivante ramene l'ecran principal complet (constate sur le poele de reference).
static const uint32_t DELAI_AVANT_SYNCHRO_MS = 20000;
static const uint32_t DUREE_SYNCHRO_MS = 12000;
// Meme synchronisation en cours de route : une trame d'ecran manquee laissait HA fige sur une
// phase perimee. Ecran principal pas revu depuis 5 min en phase de transition,
// 30 min en marche ou a l'arret, ou 45 s apres une marche/arret.
static const uint32_t RESYNCHRO_TRANSITION_MS = 5 * 60000;
static const uint32_t RESYNCHRO_STABLE_MS = 30 * 60000;
static const uint32_t RESYNCHRO_APRES_MARCHE_ARRET_MS = 45000;
// Anti-rebond : l'etat ne change qu'apres plusieurs secondes, et chaque appui long bascule.
// Trois appuis en 47 s ont eteint puis relance le poele de reference.
static const uint32_t ANTI_REBOND_MARCHE_ARRET_MS = 90000;
// Verrou jusqu'a confirmation : apres une bascule, aucune autre tant qu'un ecran principal recu
// depuis n'a pas montre un Etat different de celui d'avant (plafond 20 min). Sans lui, un ecran
// fige faisait renvoyer la bascule par le thermostat toutes les 5 min (constate sur le poele de reference).
// En attendant, resynchronisation toutes les 2 min pour obtenir la confirmation.
static const uint32_t VERROU_BASCULE_MAX_MS = 20 * 60000;
static const uint32_t RESYNCHRO_BASCULE_MS = 2 * 60000;
// Diagnostics publies dans HA (age de l'ecran, compteurs de trames)
static const uint32_t PERIODE_DIAGNOSTIC_MS = 60000;
// Fusion des parasites : segments plus courts que 0,45 unite (187 us). La sortie DIO2 est decoupee
// en pas de 64 us : un parasite de 2 pas (128 us) passait l'ancien seuil de 0,3 unite (125 us).
// Valide sur plus de 1 000 trames enregistrees (aucune regression) et par injection de parasites :
// parasites de 128 us recuperes a 99 % (contre 11 %), aucune trame fausse acceptee.
static const float SEUIL_FUSION = 0.45f;

// Fronts horodates par l'interruption (tampon circulaire)
static const uint32_t TAILLE_FRONTS = 4096;
static volatile uint32_t s_t[TAILLE_FRONTS];
static volatile uint8_t s_niveau[TAILLE_FRONTS];
static volatile uint32_t s_tete = 0;
static uint32_t s_queue = 0;
static int s_pin = 32;

static SPIClass *s_spi = nullptr;
static Module *s_module = nullptr;
static SX1278 *s_radio = nullptr;

static void IRAM_ATTR isr_front() {
  uint32_t t = (uint32_t) esp_timer_get_time();
  uint32_t niveau = s_pin < 32 ? (REG_READ(GPIO_IN_REG) >> s_pin) & 1 : (REG_READ(GPIO_IN1_REG) >> (s_pin - 32)) & 1;
  uint32_t i = s_tete & (TAILLE_FRONTS - 1);
  s_t[i] = t;
  s_niveau[i] = niveau;
  s_tete = s_tete + 1;
}

static uint32_t maintenant_us() { return (uint32_t) esp_timer_get_time(); }

static void verifier(int16_t etat, const char *etape) {
  if (etat != RADIOLIB_ERR_NONE)
    ESP_LOGW(TAG, "Radio : %s a echoue (code %d)", etape, etat);
}

static uint8_t somme(const std::vector<uint8_t> &o, size_t n) {
  uint8_t s = 0;
  for (size_t i = 0; i < n; i++)
    s += o[i];
  return s;
}

static std::string hexa(const std::vector<uint8_t> &o) {
  std::string s;
  char b[4];
  for (auto v : o) {
    snprintf(b, sizeof(b), "%02X ", v);
    s += b;
  }
  return s;
}

static std::string rogner(const std::string &s) {
  size_t a = s.find_first_not_of(' ');
  if (a == std::string::npos)
    return "";
  size_t b = s.find_last_not_of(' ');
  std::string r = s.substr(a, b - a + 1);
  // espaces multiples -> un seul
  std::string out;
  for (char c : r)
    if (!(c == ' ' && !out.empty() && out.back() == ' '))
      out += c;
  return out;
}

// Caractere de l'afficheur : chiffres envoyes bruts (0-9), bit 7 = attribut, 5E = degre
static std::string car_lcd(uint8_t v) {
  uint8_t c = v & 0x7F;
  if (c <= 9)
    return std::string(1, char('0' + c));
  if (c == 0x5E)
    return "\xC2\xB0";
  if (c >= 32 && c < 127)
    return std::string(1, char(c));
  return "?";
}

static bool est_chiffre(uint8_t v) { return (v & 0x7F) <= 9; }

// Grandes categories pour l'historique HA (phases observees ; libelle exact du defaut a confirmer)
static std::string etat_de(const std::string &phase) {
  auto a = [&](const char *mot) { return phase.find(mot) != std::string::npos; };
  if (a("DEFAUT") || a("ALARM") || a("ERREUR") || a("ERR"))
    return "Défaut";
  if (a("OFF"))
    return "Arrêt";
  if (a("CONTROLE") || a("ALLUMAGE"))
    return "Allumage";
  if (a("NIVEAU"))
    return "Marche";
  if (a("NETTOYAGE"))
    return "Nettoyage";
  if (a("EXTINCTION") || a("REFROID"))
    return "Extinction";
  return "Autre";
}

// Defauts d'un octet recu
static const uint8_t PARITE_FAUSSE = 1, STOP_FAUX = 2;

// Bits NRZ (1 unite = 1 bit) -> octets : liaison serie inversee, 8 bits poids faible d'abord, parite paire.
// `faux` recoit, pour chaque octet, ses defauts (PARITE_FAUSSE, STOP_FAUX, 0 si l'octet est juste).
static std::vector<uint8_t> octets_de(const std::vector<uint32_t> &durees, std::vector<uint8_t> &faux) {
  faux.clear();
  // Fusion des parasites, le plus court d'abord : un segment trop court (impulsion dans un silence,
  // ou trou dans une impulsion) est fusionne avec ses deux voisins, ce qui garde l'alternance.
  std::vector<uint32_t> d(durees.begin(), durees.end());
  const uint32_t seuil = (uint32_t) (SEUIL_FUSION * UNITE_US);
  while (d.size() >= 3) {
    size_t k = 0;
    for (size_t i = 1; i + 1 < d.size(); i++)
      if (d[i] < seuil && (k == 0 || d[i] < d[k]))
        k = i;
    if (k == 0)
      break;
    d[k - 1] += d[k] + d[k + 1];
    d.erase(d.begin() + k, d.begin() + k + 2);
  }
  std::string bits;
  for (size_t i = 0; i < d.size(); i++) {
    int n = std::max(1, (int) lroundf(d[i] / UNITE_US));
    if (i % 2)
      n = std::min(n, 40);
    bits.append(n, (i % 2) ? '0' : '1');
  }
  // Synchro = 12 zeros ou plus, sauf une fausse synchro suivie du preambule
  size_t debut = std::string::npos, pos = 0;
  while ((pos = bits.find("000000000000", pos)) != std::string::npos) {
    size_t fin = bits.find('1', pos);
    if (fin == std::string::npos)
      break;
    if (bits.compare(fin, 6, "101010") != 0 || bits.find("000000000000", fin) > fin + 16) {
      debut = fin;
      break;
    }
    pos = fin;
  }
  std::vector<uint8_t> out;
  if (debut == std::string::npos)
    return out;
  bits.append(12, '0');
  size_t i = debut;
  while (i + 11 <= bits.size()) {
    if (bits[i] != '1') {
      i++;
      continue;
    }
    uint8_t v = 0;
    int uns = 0;
    for (int k = 0; k < 8; k++)
      if (bits[i + 1 + k] == '1') {
        v |= 1 << k;
        uns++;
      }
    if (bits[i + 9] == '1')
      uns++;
    faux.push_back((uns % 2 != 0 ? PARITE_FAUSSE : 0) | (bits[i + 10] != '0' ? STOP_FAUX : 0));
    out.push_back(v);
    i += 11;
  }
  while (out.size() > 1 && out.back() == 0) {
    out.pop_back();  // zeros de fin = silence
    faux.pop_back();
  }
  return out;
}

// Vrai si les n premiers octets ont une parite et un bit de stop justes. La somme sur 8 bits seule
// a laisse passer des ecrans corrompus (« NIVEA? P1 » a 9 °C, texte illisible a 186 °C).
static bool octets_justes(const std::vector<uint8_t> &faux, size_t n) {
  for (size_t i = 0; i < n && i < faux.size(); i++)
    if (faux[i] != 0)
      return false;
  return true;
}

// Defaut le plus frequent avec une alimentation bruitee : une impulsion parasite pendant un silence
// ajoute UN bit a 1 dans un octet (08 -> 48, 20 -> 50, constate sur le poele de reference).
// On le retire si les deux controles designent le meme bit : un seul octet faux parmi les n utiles,
// par sa parite seule, et un ecart de somme egal a un bit a 1 de cet octet. Sinon on ne touche a rien.
static bool corriger_bit_parasite(std::vector<uint8_t> &o, std::vector<uint8_t> &faux, size_t n) {
  if (o.size() < n || faux.size() < n)
    return false;
  size_t k = n;
  for (size_t i = 0; i < n; i++) {
    if (faux[i] == 0)
      continue;
    if (faux[i] != PARITE_FAUSSE || k != n)
      return false;
    k = i;
  }
  if (k == n || k == 0)  // rien a corriger, ou l'octet de type lui-meme
    return false;
  uint8_t s = somme(o, n - 1);
  // octet de donnees : la somme calculee depasse la somme recue du bit parasite ;
  // octet de somme : c'est la somme recue qui depasse
  uint8_t ecart = k < n - 1 ? (uint8_t) (s - o[n - 1]) : (uint8_t) (o[n - 1] - s);
  if (ecart == 0 || (ecart & (ecart - 1)) != 0 || (o[k] & ecart) == 0)
    return false;
  o[k] &= ~ecart;
  faux[k] = 0;
  return true;
}

// ---------------------------------------------------------------- Entites

void PiazzettaNumber::control(float value) {
  if (this->parent_ != nullptr)
    this->parent_->demander_reglage(this->reglage_, (int) lroundf(value));
}

void PiazzettaButton::press_action() {
  if (this->parent_ != nullptr)
    this->parent_->demander_action(this->action_);
}

// ---------------------------------------------------------------- Radio

void PiazzettaComponent::configurer_radio_() {
  verifier(s_radio->beginFSK(), "beginFSK");
  verifier(s_radio->setFrequency(this->frequence_), "setFrequency");
  verifier(s_radio->setOOK(true), "setOOK");
  verifier(s_radio->setCrcFiltering(false), "setCrcFiltering");
  // Reglages repris de rtl_433_ESP, valides sur ce montage
  verifier(s_radio->setDataShapingOOK(2), "setDataShapingOOK");
  verifier(s_radio->setOokThresholdType(RADIOLIB_SX127X_OOK_THRESH_PEAK), "setOokThresholdType");
  verifier(s_radio->setOokPeakThresholdDecrement(RADIOLIB_SX127X_OOK_PEAK_THRESH_DEC_1_1_CHIP), "setOokPeakThresholdDecrement");
  verifier(s_radio->setOokPeakThresholdStep(RADIOLIB_SX127X_OOK_PEAK_THRESH_STEP_0_5_DB), "setOokPeakThresholdStep");
  verifier(s_radio->setOokFixedOrFloorThreshold(0x50), "setOokFixedOrFloorThreshold");
  verifier(s_radio->setBitRate(1.2), "setBitRate");
  verifier(s_radio->setRxBandwidth(250), "setRxBandwidth");
  verifier(s_radio->setRSSIConfig(RADIOLIB_SX127X_RSSI_SMOOTHING_SAMPLES_2, RADIOLIB_SX127X_OOK_AVERAGE_OFFSET_0_DB), "setRSSIConfig");
  verifier(s_module->SPIsetRegValue(RADIOLIB_SX127X_REG_PREAMBLE_DETECT, RADIOLIB_SX127X_PREAMBLE_DETECTOR_OFF), "preambleDetectOff");
  verifier(s_radio->setDirectSyncWord(0, 0), "setDirectSyncWord");
  verifier(s_radio->disableBitSync(), "disableBitSync");
  verifier(s_radio->setOutputPower(this->puissance_emission_), "setOutputPower");
}

void PiazzettaComponent::ecouter_() {
  pinMode(this->dio2_, INPUT);
  verifier(s_radio->receiveDirect(), "receiveDirect");
  this->en_trame_ = false;
  s_queue = s_tete;
  attachInterrupt(digitalPinToInterrupt(this->dio2_), isr_front, CHANGE);
}

void PiazzettaComponent::emettre_octets_(std::vector<uint8_t> octets) {
  octets.push_back(somme(octets, octets.size()));
  // Preambule 1010101, synchro de ~21 bits, puis les octets (start 1, 8 bits LSB d'abord, parite paire, stop 0)
  std::string bits = "1010101" + std::string(21, '0');
  for (uint8_t v : octets) {
    int uns = 0;
    bits += '1';
    for (int k = 0; k < 8; k++) {
      bool b = (v >> k) & 1;
      bits += b ? '1' : '0';
      uns += b;
    }
    bits += (uns % 2) ? '1' : '0';
    bits += '0';
  }
  while (!bits.empty() && bits.back() == '0')
    bits.pop_back();
  std::vector<uint32_t> d;
  for (size_t i = 0; i < bits.size();) {
    size_t j = i;
    while (j < bits.size() && bits[j] == bits[i])
      j++;
    d.push_back((uint32_t) lroundf((j - i) * UNITE_US));
    i = j;
  }
  ESP_LOGD(TAG, "Emission : %s", hexa(octets).c_str());

  detachInterrupt(digitalPinToInterrupt(this->dio2_));
  this->en_trame_ = false;
  // En emission continue, le SX127x echantillonne DIO2 au rythme du debit binaire :
  // debit OOK maximal et sans mise en forme, sinon la trame est deformee (constate sur le poele de reference).
  verifier(s_radio->setBitRate(32.768), "setBitRate TX");
  verifier(s_radio->setDataShapingOOK(0), "setDataShapingOOK TX");
  verifier(s_radio->transmitDirect(), "transmitDirect");
  pinMode(this->dio2_, OUTPUT);
  digitalWrite(this->dio2_, LOW);
  delay(2);
  int64_t cible = esp_timer_get_time();
  for (size_t i = 0; i < d.size(); i++) {
    digitalWrite(this->dio2_, (i % 2 == 0) ? HIGH : LOW);
    cible += d[i];
    while (esp_timer_get_time() < cible) {
    }
  }
  digitalWrite(this->dio2_, LOW);
  verifier(s_radio->standby(), "standby");
  // Apres une emission, le recepteur peut rester desensibilise : radio entierement reconfiguree
  this->configurer_radio_();
  this->ecouter_();
}

void PiazzettaComponent::finir_trame_() {
  this->en_trame_ = false;
  if (this->nb_impulsions_ < MIN_IMPULSIONS || this->rssi_max_ < this->seuil_rssi_ || this->durees_.size() < 2)
    return;
  if (this->durees_.size() % 2 == 0)
    this->durees_.pop_back();  // finir sur un « haut »
  this->traiter_trame_(this->durees_, (int) this->rssi_max_);
}

void PiazzettaComponent::traiter_fronts_() {
  while (s_queue != s_tete) {
    if (s_tete - s_queue > TAILLE_FRONTS) {
      s_queue = s_tete - TAILLE_FRONTS;
      ESP_LOGW(TAG, "Tampon de fronts plein (bruit radio continu ?)");
    }
    uint32_t i = s_queue & (TAILLE_FRONTS - 1);
    uint32_t t = s_t[i];
    uint8_t niveau = s_niveau[i];
    s_queue++;

    if (!this->en_trame_) {
      if (niveau != 1)
        continue;
      // Le comparateur OOK bavarde dans le bruit : on ne demarre que sur un signal assez fort
      float r = s_radio->getRSSI(false, true);
      if (r < this->seuil_rssi_)
        continue;
      this->en_trame_ = true;
      this->debut_trame_ = t;
      this->dernier_front_ = t;
      this->dernier_niveau_ = 1;
      this->durees_.clear();
      this->nb_impulsions_ = 1;
      this->rssi_max_ = r;
      this->dernier_rssi_us_ = maintenant_us();
      this->dernier_fort_ = this->dernier_rssi_us_;
      continue;
    }
    if (niveau == this->dernier_niveau_)
      continue;
    if (this->durees_.size() < MAX_DUREES)
      this->durees_.push_back(t - this->dernier_front_);
    if (niveau == 1)
      this->nb_impulsions_++;
    this->dernier_front_ = t;
    this->dernier_niveau_ = niveau;
    if (this->durees_.size() >= MAX_DUREES || t - this->debut_trame_ > TRAME_MAX_US)
      this->finir_trame_();
  }
}

// ---------------------------------------------------------------- Decodage

void PiazzettaComponent::traiter_trame_(const std::vector<uint32_t> &durees, int rssi) {
  std::vector<uint8_t> faux;
  std::vector<uint8_t> o = octets_de(durees, faux);
  if (o.size() < 3)
    return;
  // Longueur utile selon le type de trame (ecran 32, heure 6, telecommande 5) : les octets
  // parasites apres la somme ne comptent pas
  size_t utile = o[0] == 0xB5 ? 32 : o[0] == 0xB9 ? 6 : 5;
  bool corrigee = (o[0] == 0xB5 || o[0] == 0xB9 || o[0] == this->remote_id_) && corriger_bit_parasite(o, faux, utile);
  bool somme_ok = somme(o, o.size() - 1) == o.back();
  bool octets_ok = octets_justes(faux, utile);
  ESP_LOGD(TAG, "Trame %d dBm : %s%s%s%s", rssi, hexa(o).c_str(), somme_ok ? "" : "(somme fausse)",
           corrigee ? " (bit parasite corrige)" : "", octets_ok ? "" : " (octet faux, rejetee)");
  // Compteurs de diagnostic : une trame d'au moins 5 octets non acceptee ci-dessous est rejetee
  if (o.size() >= 5)
    this->nb_trames_rejetees_++;
  if (!octets_ok)
    return;

  // Trame de telecommande (5 octets, somme juste, 2e octet 3D/BD/3E/BE) avec un autre identifiant :
  // on l'annonce, pour qu'un nouvel utilisateur sache quelle valeur mettre dans remote_id.
  if (o.size() >= 5 && somme(o, 4) == o[4] && (o[1] & 0x7C) == 0x3C && o[0] != this->remote_id_ && o[0] != 0xB5 &&
      o[0] != 0xB9) {
    ESP_LOGW(TAG, "Telecommande entendue avec l'identifiant 0x%02X (remote_id configure : 0x%02X)", o[0],
             this->remote_id_);
  }
  if (o[0] == this->remote_id_ && o.size() >= 5 && somme(o, 4) == o[4]) {
    this->nb_trames_rejetees_ -= o.size() >= 5;
    this->nb_trames_corrigees_ += corrigee;
    this->derniere_telecommande_ms_ = millis();
    return;
  }
  if (o[0] == 0xB5 && o.size() >= 32 && somme(o, 31) == o[31]) {
    this->nb_trames_rejetees_--;
    this->nb_trames_corrigees_ += corrigee;
    if (this->rssi_ != nullptr)
      this->rssi_->publish_state(rssi);
    this->derniere_reponse_ms_ = millis();
    this->traiter_ecran_(o);
    return;
  }
  if (o[0] == 0xB9 && o.size() >= 6 && somme(o, 5) == o[5]) {
    // Etat court = heure seule (la telecommande endormie le demande toutes les ~44 s)
    this->nb_trames_rejetees_--;
    this->nb_trames_corrigees_ += corrigee;
    this->derniere_reponse_ms_ = millis();
    if (this->heure_ != nullptr && est_chiffre(o[1]) && est_chiffre(o[2]) && est_chiffre(o[3]) && est_chiffre(o[4])) {
      char h[12];
      snprintf(h, sizeof(h), "%d%d:%d%d", o[1] & 0x7F, o[2] & 0x7F, o[3] & 0x7F, o[4] & 0x7F);
      if (this->heure_->state != h)
        this->heure_->publish_state(h);
    }
  }
}

void PiazzettaComponent::traiter_ecran_(const std::vector<uint8_t> &o) {
  Ecran e;
  for (int i = 10; i < 19; i++)
    e.ligne1 += car_lcd(o[i]);
  for (int i = 19; i < 28; i++)
    e.ligne2 += car_lcd(o[i]);
  for (int i = 1; i < 10; i++)
    e.bas += car_lcd(o[i]);

  e.menu = o[1] == 0x20 && e.ligne1.find("REGLER") != std::string::npos;
  if (e.menu) {
    e.menu_marque = o[4];
    e.menu_valeur = o[5] & 0x7F;
  } else {
    if (est_chiffre(o[1]) && est_chiffre(o[2]) && est_chiffre(o[3]) && est_chiffre(o[4])) {
      e.heure_h = (o[1] & 0x7F) * 10 + (o[2] & 0x7F);
      e.heure_m = (o[3] & 0x7F) * 10 + (o[4] & 0x7F);
    }
    int t = 0, n = 0;
    for (int i = 5; i < 10; i++)
      if (est_chiffre(o[i]) && o[i] != 0x20) {
        t = t * 10 + (o[i] & 0x7F);
        n++;
      }
    if (n > 0 && e.heure_h >= 0)
      e.temperature = t;
    // « NIVEAU / P<n> » : le chiffre qui suit P est la puissance
    for (int i = 19; i < 27; i++)
      if (o[i] == 'P' && (o[i + 1] & 0x7F) >= 1 && (o[i + 1] & 0x7F) <= 4 && e.ligne1.find("NIVEAU") != std::string::npos)
        e.puissance = o[i + 1] & 0x7F;
  }
  // Icones : bargraphe de puissance (bits bas de o[29]) et ventilateur (o[30])
  if (e.puissance == 0) {
    int barres = __builtin_popcount(o[29] & 0x0F);
    if (barres >= 1 && barres <= 4)
      e.puissance = barres;
  }
  switch (o[30]) {
    case 0x08: e.ventilateur = 1; break;
    case 0x18: e.ventilateur = 2; break;
    case 0x39: e.ventilateur = 3; break;
    case 0x7B: e.ventilateur = 4; break;
    default: break;
  }
  if (e.menu && e.menu_marque == MARQUE_PUISSANCE)
    e.puissance = e.menu_valeur;
  if (e.menu && e.menu_marque == MARQUE_VENTILATEUR)
    e.ventilateur = e.menu_valeur;

  // Publication. Les lignes d'ecran suivent tout (menus compris) pour la telecommande deportee ;
  // « Phase » et « Etat » ne suivent que l'ecran principal, pour un historique lisible.
  auto publier = [](text_sensor::TextSensor *t, const std::string &v) {
    if (t != nullptr && t->state != v)
      t->publish_state(v);
  };
  publier(this->ligne1_, rogner(e.ligne1));
  publier(this->ligne2_, rogner(e.ligne2));
  std::string bas = rogner(e.bas);
  if (!e.menu && e.heure_h >= 0) {
    char b[32];
    if (e.temperature > -1000)
      snprintf(b, sizeof(b), "%02d:%02d   %d°", e.heure_h, e.heure_m, e.temperature);
    else
      snprintf(b, sizeof(b), "%02d:%02d", e.heure_h, e.heure_m);
    bas = b;
  }
  publier(this->bas_, bas);
  std::string phase = rogner(e.ligne1 + " " + e.ligne2);
  if (!e.menu) {
    publier(this->phase_, phase);
    publier(this->etat_, etat_de(phase));
  }
  if (this->temperature_ != nullptr && e.temperature > -1000 &&
      (!this->temperature_->has_state() || this->temperature_->state != e.temperature))
    this->temperature_->publish_state(e.temperature);
  if (this->heure_ != nullptr && e.heure_h >= 0) {
    char h[12];
    snprintf(h, sizeof(h), "%02d:%02d", e.heure_h, e.heure_m);
    if (this->heure_->state != h)
      this->heure_->publish_state(h);
  }
  if (this->puissance_ != nullptr && e.puissance > 0 && (!this->puissance_->has_state() || this->puissance_->state != e.puissance))
    this->puissance_->publish_state(e.puissance);
  if (this->ventilateur_ != nullptr && e.ventilateur > 0 &&
      (!this->ventilateur_->has_state() || this->ventilateur_->state != e.ventilateur))
    this->ventilateur_->publish_state(e.ventilateur);

  this->dernier_ecran_ = e;
  this->ecran_recu_ = true;
  if (!e.menu) {
    this->ecran_principal_recu_ = true;
    this->dernier_ecran_principal_ms_ = millis();
    const std::string etat = etat_de(phase);
    // Confirmation d'une bascule : un Etat connu, different de celui d'avant l'appui
    if (this->bascule_en_attente_ && etat != "Autre" && etat != this->etat_avant_bascule_) {
      this->bascule_en_attente_ = false;
      ESP_LOGI(TAG, "Bascule confirmee : %s -> %s", this->etat_avant_bascule_.c_str(), etat.c_str());
      this->publier_bascule_();
    }
    this->dernier_etat_ = etat;
    const bool stable = etat == "Marche" || etat == "Arrêt";
    this->prochaine_synchro_ms_ =
        millis() + (this->bascule_en_attente_ ? RESYNCHRO_BASCULE_MS : stable ? RESYNCHRO_STABLE_MS : RESYNCHRO_TRANSITION_MS);
  }
  ESP_LOGI(TAG, "Ecran : %s", phase.c_str());

  // Suite de la tache de reglage en cours
  if (this->etape_ != Etape::ATTENTE_MENU && this->etape_ != Etape::ATTENTE_VALEUR)
    return;
  const bool puissance = this->reglage_ == Reglage::PUISSANCE;
  const uint8_t marque = puissance ? MARQUE_PUISSANCE : MARQUE_VENTILATEUR;
  const uint8_t touche_menu = puissance ? TOUCHE_PUISSANCE : TOUCHE_VENTILATEUR;
  int valeur = puissance ? e.puissance : e.ventilateur;

  if (e.menu && e.menu_marque == marque) {
    if (valeur == this->cible_) {
      this->terminer_tache_("cible atteinte");
    } else if (this->pas_ >= PAS_MAX) {
      this->terminer_tache_("trop de pas, abandon");
    } else {
      this->pas_++;
      this->emettre_octets_({this->remote_id_, ETAT_MENU, 0x00, valeur < this->cible_ ? TOUCHE_PLUS : TOUCHE_MOINS});
      this->etape_ = Etape::ATTENTE_VALEUR;
      this->echeance_ms_ = millis() + DELAI_REPONSE_MS;
    }
    return;
  }
  // Ecran principal (ou autre menu) : deja a la cible ? Sinon (r)ouvrir notre menu.
  // La touche de menu bascule : elle referme le menu si le poele le croit ouvert, d'ou les essais.
  if (!e.menu && valeur == this->cible_) {
    this->terminer_tache_("deja a la cible");
    return;
  }
  if (++this->essais_ > ESSAIS_MAX) {
    this->terminer_tache_("menu impossible a ouvrir, abandon");
    return;
  }
  this->emettre_octets_({this->remote_id_, ETAT_REPOS, 0x00, touche_menu});
  this->etape_ = Etape::ATTENTE_MENU;
  this->echeance_ms_ = millis() + DELAI_REPONSE_MS;
}

// ---------------------------------------------------------------- Taches

void PiazzettaComponent::demander_reglage(Reglage reglage, int cible) {
  const char *nom = reglage == Reglage::PUISSANCE ? "puissance" : "ventilateur";
  if (this->etape_ != Etape::REPOS) {
    ESP_LOGW(TAG, "Reglage %s -> %d ignore : une commande est deja en cours", nom, cible);
    return;
  }
  ESP_LOGI(TAG, "Reglage %s -> %d", nom, cible);
  this->reglage_ = reglage;
  this->cible_ = cible;
  this->essais_ = 0;
  this->pas_ = 0;
  // Une touche provoque toujours un ecran complet, contrairement a une interrogation (heure seule
  // si rien n'a change) : on commence donc par la touche de menu, l'ecran recu guide la suite.
  this->emettre_octets_({this->remote_id_, ETAT_REPOS, 0x00,
                         reglage == Reglage::PUISSANCE ? TOUCHE_PUISSANCE : TOUCHE_VENTILATEUR});
  this->etape_ = Etape::ATTENTE_MENU;
  this->echeance_ms_ = millis() + DELAI_REPONSE_MS;
}

void PiazzettaComponent::demander_action(Action action) {
  if (this->etape_ != Etape::REPOS) {
    ESP_LOGW(TAG, "Action ignoree : une commande est deja en cours");
    return;
  }
  if (action == Action::INTERROGER) {
    this->emettre_octets_({this->remote_id_, ETAT_REPOS, 0x00, TOUCHE_INTERROGER});
    this->derniere_interrogation_ms_ = millis();
    return;
  }
  // Marche/arret = appui long sur ⏻ : touche 81, puis 61 repetee tant que le bouton est « maintenu ».
  // Valide : extinction et allumage declenches par l'ESP sur le poele de reference.
  const uint32_t ecart = millis() - this->dernier_marche_arret_ms_;
  if (this->marche_arret_envoye_ && ecart < ANTI_REBOND_MARCHE_ARRET_MS) {
    ESP_LOGW(TAG, "Marche/arret ignore : appui precedent il y a %u s (anti-rebond %u s)", (unsigned) (ecart / 1000),
             (unsigned) (ANTI_REBOND_MARCHE_ARRET_MS / 1000));
    return;
  }
  if (this->bascule_en_attente_ && ecart < VERROU_BASCULE_MAX_MS) {
    ESP_LOGW(TAG, "Marche/arret ignore : la bascule d'il y a %u s n'est pas encore confirmee par l'ecran",
             (unsigned) (ecart / 1000));
    return;
  }
  this->marche_arret_envoye_ = true;
  this->dernier_marche_arret_ms_ = millis();
  this->bascule_en_attente_ = true;
  this->etat_avant_bascule_ = this->dernier_etat_;
  this->publier_bascule_();
  ESP_LOGW(TAG, "Marche/arret : appui long simule");
  this->emettre_octets_({this->remote_id_, 0x3E, 0x00, TOUCHE_MARCHE});
  this->pas_ = 0;
  this->etape_ = Etape::APPUI_LONG;
  this->echeance_ms_ = millis() + PERIODE_MAINTIEN_MS;
}

void PiazzettaComponent::avancer_tache_() {
  if (this->etape_ == Etape::REPOS || (int32_t) (millis() - this->echeance_ms_) < 0)
    return;
  if (this->etape_ == Etape::SYNCHRO) {
    // Le menu ventilateur s'est referme : l'ecran principal a « change », il revient en entier
    this->etape_ = Etape::REPOS;
    this->derniere_interrogation_ms_ = millis();
    this->emettre_octets_({this->remote_id_, ETAT_REPOS, 0x00, TOUCHE_INTERROGER});
    return;
  }
  if (this->etape_ == Etape::APPUI_LONG) {
    if (this->pas_ >= APPUIS_MAINTIEN) {
      this->terminer_tache_("appui long termine");
      this->emettre_octets_({this->remote_id_, ETAT_REPOS, 0x00, TOUCHE_INTERROGER});
      this->prochaine_synchro_ms_ = millis() + RESYNCHRO_APRES_MARCHE_ARRET_MS;
      return;
    }
    this->emettre_octets_({this->remote_id_, ETATS_MAINTIEN[this->pas_], 0x00, TOUCHE_MAINTIEN});
    this->pas_++;
    this->echeance_ms_ = millis() + PERIODE_MAINTIEN_MS;
    return;
  }
  // Pas de reponse : on renvoie la touche de menu ; l'ecran complet qu'elle provoque relancera la tache
  if (++this->essais_ > ESSAIS_MAX) {
    this->terminer_tache_("pas de reponse du poele, abandon");
    return;
  }
  ESP_LOGD(TAG, "Pas de reponse, nouvel appui sur la touche de menu (essai %d)", this->essais_);
  this->emettre_octets_({this->remote_id_, ETAT_REPOS, 0x00,
                         this->reglage_ == Reglage::PUISSANCE ? TOUCHE_PUISSANCE : TOUCHE_VENTILATEUR});
  this->etape_ = Etape::ATTENTE_MENU;
  this->echeance_ms_ = millis() + DELAI_REPONSE_MS;
}

void PiazzettaComponent::terminer_tache_(const char *bilan) {
  ESP_LOGI(TAG, "Fin de commande : %s", bilan);
  this->etape_ = Etape::REPOS;
  this->derniere_interrogation_ms_ = millis();
  // Remet l'affichage HA sur la valeur reellement connue (utile en cas d'echec)
  if (this->puissance_ != nullptr && this->dernier_ecran_.puissance > 0)
    this->puissance_->publish_state(this->dernier_ecran_.puissance);
  if (this->ventilateur_ != nullptr && this->dernier_ecran_.ventilateur > 0)
    this->ventilateur_->publish_state(this->dernier_ecran_.ventilateur);
}

// ---------------------------------------------------------------- Cycle de vie

void PiazzettaComponent::setup() {
  s_pin = this->dio2_;
  s_spi = new SPIClass(VSPI);
  s_spi->begin(this->sck_, this->miso_, this->mosi_, this->cs_);
  s_module = new Module(this->cs_, this->dio0_, this->rst_, this->dio1_, *s_spi);
  s_radio = new SX1278(s_module);
  this->durees_.reserve(MAX_DUREES);
  this->configurer_radio_();
  this->ecouter_();
  this->boucle_rapide_.start();
  // la boucle doit tourner souvent pour suivre le RSSI des trames
  this->demarrage_ms_ = millis();
  this->prochaine_synchro_ms_ = millis() + DELAI_AVANT_SYNCHRO_MS;
  this->publier_bascule_();
  // Premiere interrogation peu apres le demarrage, pour se resynchroniser
  this->derniere_interrogation_ms_ = millis() - this->intervalle_ms_ + 5000;
}

void PiazzettaComponent::loop() {
  this->traiter_fronts_();

  uint32_t t = maintenant_us();
  if (this->en_trame_) {
    // RSSI lu seulement pendant une impulsion : une lecture SPI dans un silence fait apparaitre une
    // impulsion parasite sur DIO2 (presque tous les parasites observes tombaient a la cadence de ces lectures)
    if (this->dernier_niveau_ == 1 && t - this->dernier_rssi_us_ > 1000) {
      float r = s_radio->getRSSI(false, true);
      if (r > this->rssi_max_)
        this->rssi_max_ = r;
      if (r >= this->seuil_rssi_)
        this->dernier_fort_ = t;
      this->dernier_rssi_us_ = t;
    }
    // Fin de trame : silence sur la broche, ou plus de signal fort (le bruit continue de basculer)
    if (this->dernier_niveau_ == 0 && t - this->dernier_front_ > FIN_TRAME_US)
      this->finir_trame_();
    else if (t - this->dernier_fort_ > FIN_TRAME_US)
      this->finir_trame_();
    else if (t - this->debut_trame_ > TRAME_MAX_US)
      this->finir_trame_();
    return;  // rien d'autre pendant une reception
  }

  this->avancer_tache_();

  uint32_t ms = millis();
  // Synchronisation si l'ecran principal n'a pas ete revu a temps (au demarrage, ou trame manquee) :
  // chaque ecran principal recu repousse l'echeance
  if (this->etape_ == Etape::REPOS && (int32_t) (ms - this->prochaine_synchro_ms_) >= 0 &&
      ms - this->derniere_telecommande_ms_ > SILENCE_TELECOMMANDE_MS) {
    ESP_LOGI(TAG, "Synchronisation : ouverture du menu ventilateur pour obtenir l'ecran complet");
    this->emettre_octets_({this->remote_id_, ETAT_REPOS, 0x00, TOUCHE_VENTILATEUR});
    this->etape_ = Etape::SYNCHRO;
    this->echeance_ms_ = ms + DUREE_SYNCHRO_MS;
    this->prochaine_synchro_ms_ = ms + RESYNCHRO_TRANSITION_MS;  // nouvel essai si l'ecran ne revient pas
    return;
  }
  // Interrogation periodique, sauf si la vraie telecommande parle (ses reponses suffisent)
  if (this->etape_ == Etape::REPOS && ms - this->derniere_interrogation_ms_ > this->intervalle_ms_ &&
      ms - this->derniere_telecommande_ms_ > SILENCE_TELECOMMANDE_MS) {
    this->derniere_interrogation_ms_ = ms;
    this->emettre_octets_({this->remote_id_, ETAT_REPOS, 0x00, TOUCHE_INTERROGER});
  }
  if (this->joignable_ != nullptr && ms - this->dernier_joignable_ms_ > 5000) {
    this->dernier_joignable_ms_ = ms;
    bool ok = this->derniere_reponse_ms_ != 0 && ms - this->derniere_reponse_ms_ < DELAI_JOIGNABLE_MS;
    if (!this->joignable_->has_state() || this->joignable_->state != ok)
      this->joignable_->publish_state(ok);
  }
  // Verrou de bascule expire sans confirmation : la commande est probablement restee sans effet
  if (this->bascule_en_attente_ && ms - this->dernier_marche_arret_ms_ >= VERROU_BASCULE_MAX_MS) {
    this->bascule_en_attente_ = false;
    ESP_LOGW(TAG, "Bascule jamais confirmee par l'ecran en %u min : verrou leve", (unsigned) (VERROU_BASCULE_MAX_MS / 60000));
    this->publier_bascule_();
  }
  if (ms - this->dernier_diagnostic_ms_ >= PERIODE_DIAGNOSTIC_MS) {
    this->dernier_diagnostic_ms_ = ms;
    // Age de l'ecran principal (depuis le demarrage s'il n'en est encore venu aucun)
    if (this->age_ecran_ != nullptr)
      this->age_ecran_->publish_state(
          (ms - (this->ecran_principal_recu_ ? this->dernier_ecran_principal_ms_ : this->demarrage_ms_)) / 1000);
    if (this->trames_rejetees_ != nullptr)
      this->trames_rejetees_->publish_state(this->nb_trames_rejetees_);
    if (this->trames_corrigees_ != nullptr)
      this->trames_corrigees_->publish_state(this->nb_trames_corrigees_);
  }
}

void PiazzettaComponent::publier_bascule_() {
  if (this->bascule_attente_ != nullptr)
    this->bascule_attente_->publish_state(this->bascule_en_attente_);
}

void PiazzettaComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Poele Piazzetta (radio SX1278)");
  ESP_LOGCONFIG(TAG, "  Broches : SCK %d, MISO %d, MOSI %d, CS %d, RST %d, DIO0 %d, DIO1 %d, DIO2 %d", this->sck_,
                this->miso_, this->mosi_, this->cs_, this->rst_, this->dio0_, this->dio1_, this->dio2_);
  ESP_LOGCONFIG(TAG, "  Identifiant telecommande : 0x%02X", this->remote_id_);
  ESP_LOGCONFIG(TAG, "  Frequence : %.3f MHz, emission %d dBm, seuil %d dBm, interrogation toutes les %u s",
                this->frequence_, this->puissance_emission_, this->seuil_rssi_, (unsigned) (this->intervalle_ms_ / 1000));
}

}  // namespace piazzetta
}  // namespace esphome
