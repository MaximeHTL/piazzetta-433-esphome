#pragma once

// Poele Piazzetta pilote par radio 433 MHz, a la place de la telecommande F0203-1480V3.
// Le code radio reprend le banc de test (test-bench/firmware), valide sur le poele de reference.
// English: see docs/esphome.md and docs/protocol.md.

#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/number/number.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace piazzetta {

enum class Reglage : uint8_t { PUISSANCE, VENTILATEUR };
enum class Action : uint8_t { INTERROGER, MARCHE_ARRET };

class PiazzettaComponent;

class PiazzettaNumber : public number::Number {
 public:
  void set_parent(PiazzettaComponent *parent) { this->parent_ = parent; }
  void set_reglage(Reglage reglage) { this->reglage_ = reglage; }

 protected:
  void control(float value) override;
  PiazzettaComponent *parent_{nullptr};
  Reglage reglage_{Reglage::PUISSANCE};
};

class PiazzettaButton : public button::Button {
 public:
  void set_parent(PiazzettaComponent *parent) { this->parent_ = parent; }
  void set_action(Action action) { this->action_ = action; }

 protected:
  void press_action() override;
  PiazzettaComponent *parent_{nullptr};
  Action action_{Action::INTERROGER};
};

// Ecran envoye par le poele (32 octets) : B5 | bas gauche (4) | bas droite (5) | ligne 1 (9) | ligne 2 (9) | icones (3) | somme
struct Ecran {
  std::string ligne1, ligne2, bas;
  int heure_h{-1}, heure_m{-1};
  int temperature{-1000};
  int puissance{0};       // 1 a 4, 0 si inconnue
  int ventilateur{0};     // 1 a 4, 0 si inconnu
  bool menu{false};       // ecran « REGLER … »
  uint8_t menu_marque{0};  // 0x00 ventilateur, 0x50 puissance
  int menu_valeur{0};
};

class PiazzettaComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_broches(int sck, int miso, int mosi, int cs, int rst, int dio0, int dio1, int dio2) {
    sck_ = sck; miso_ = miso; mosi_ = mosi; cs_ = cs; rst_ = rst; dio0_ = dio0; dio1_ = dio1; dio2_ = dio2;
  }
  void set_frequence(float f) { frequence_ = f; }
  void set_remote_id(uint8_t id) { remote_id_ = id; }
  void set_puissance_emission(int p) { puissance_emission_ = p; }
  void set_seuil_rssi(int s) { seuil_rssi_ = s; }
  void set_intervalle(uint32_t ms) { intervalle_ms_ = ms; }
  void set_temperature(sensor::Sensor *s) { temperature_ = s; }
  void set_rssi(sensor::Sensor *s) { rssi_ = s; }
  void set_phase(text_sensor::TextSensor *t) { phase_ = t; }
  void set_heure(text_sensor::TextSensor *t) { heure_ = t; }
  void set_etat(text_sensor::TextSensor *t) { etat_ = t; }
  void set_ligne1(text_sensor::TextSensor *t) { ligne1_ = t; }
  void set_ligne2(text_sensor::TextSensor *t) { ligne2_ = t; }
  void set_bas(text_sensor::TextSensor *t) { bas_ = t; }
  void set_joignable(binary_sensor::BinarySensor *b) { joignable_ = b; }
  void set_puissance(PiazzettaNumber *n) { puissance_ = n; }
  void set_ventilateur(PiazzettaNumber *n) { ventilateur_ = n; }

  void demander_reglage(Reglage reglage, int cible);
  void demander_action(Action action);

 protected:
  // --- Radio ---
  void configurer_radio_();
  void ecouter_();
  void emettre_octets_(std::vector<uint8_t> octets);
  void traiter_fronts_();
  void finir_trame_();
  void traiter_trame_(const std::vector<uint32_t> &durees, int rssi);
  void traiter_ecran_(const std::vector<uint8_t> &o);

  // --- Taches ---
  enum class Etape : uint8_t { REPOS, ATTENTE_MENU, ATTENTE_VALEUR, APPUI_LONG, SYNCHRO };
  void avancer_tache_();
  void terminer_tache_(const char *bilan);

  int sck_, miso_, mosi_, cs_, rst_, dio0_, dio1_, dio2_;
  float frequence_{433.92f};
  uint8_t remote_id_{0xC5};  // premier octet des trames de la telecommande
  int puissance_emission_{10};
  int seuil_rssi_{-78};
  uint32_t intervalle_ms_{30000};

  sensor::Sensor *temperature_{nullptr};
  sensor::Sensor *rssi_{nullptr};
  text_sensor::TextSensor *phase_{nullptr};
  text_sensor::TextSensor *heure_{nullptr};
  text_sensor::TextSensor *etat_{nullptr};
  text_sensor::TextSensor *ligne1_{nullptr};
  text_sensor::TextSensor *ligne2_{nullptr};
  text_sensor::TextSensor *bas_{nullptr};
  binary_sensor::BinarySensor *joignable_{nullptr};
  PiazzettaNumber *puissance_{nullptr};
  PiazzettaNumber *ventilateur_{nullptr};

  HighFrequencyLoopRequester boucle_rapide_;

  // Trame en cours d'assemblage
  bool en_trame_{false};
  uint32_t debut_trame_{0}, dernier_front_{0}, dernier_fort_{0}, dernier_rssi_us_{0};
  uint8_t dernier_niveau_{0};
  std::vector<uint32_t> durees_;
  int nb_impulsions_{0};
  float rssi_max_{-200};

  // Etat connu
  uint32_t derniere_reponse_ms_{0};
  uint32_t derniere_telecommande_ms_{0};
  uint32_t derniere_interrogation_ms_{0};
  uint32_t dernier_joignable_ms_{0};
  bool ecran_recu_{false};
  bool ecran_principal_recu_{false};
  uint32_t prochaine_synchro_ms_{0};
  bool marche_arret_envoye_{false};
  uint32_t dernier_marche_arret_ms_{0};
  Ecran dernier_ecran_;

  // Tache en cours (une seule commande radio a la fois)
  Etape etape_{Etape::REPOS};
  Reglage reglage_{Reglage::PUISSANCE};
  int cible_{0};
  int essais_{0};
  int pas_{0};
  uint32_t echeance_ms_{0};
  bool reponse_attendue_{false};
};

}  // namespace piazzetta
}  // namespace esphome
