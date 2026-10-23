# Piazzetta 433 MHz → ESPHome / Home Assistant

**Piloter un poêle à granulés Piazzetta depuis Home Assistant en lui parlant par radio, exactement comme le fait sa télécommande d'origine.**
Aucun câblage dans le poêle, aucune modification du poêle : un ESP32 et un module radio 433 MHz prennent la place de la télécommande.

🇬🇧 **English version: [README.md](README.md)** — la documentation détaillée (`docs/`) est en anglais.

---

> [!WARNING]
> **Projet expérimental, à utiliser à vos risques et périls.**
> Il pilote un **appareil de chauffage à combustion**. Il a été obtenu par rétro-ingénierie sur **un** poêle et **une** télécommande, par un bricoleur, sans aucune documentation du fabricant. Il n'est ni affilié à Piazzetta / Gruppo Piazzetta, ni approuvé ou soutenu par la marque.
> Gardez la télécommande d'origine, respectez la notice et les consignes de sécurité du poêle, ne retirez aucune de ses protections, et ne comptez pas sur ce projet pour une fonction de sécurité. Vous êtes seul responsable de ce que vous envoyez à votre poêle.

> [!NOTE]
> **Réalisé avec Claude Code, en toute transparence.**
> Je suis un bricoleur qui a quelques notions d'électronique, pas un ingénieur radio ou logiciel. Tout ce projet a été produit avec [Claude Code](https://claude.com/claude-code), l'agent de développement IA d'Anthropic : la rétro-ingénierie du protocole, les firmwares, le composant ESPHome, la configuration Home Assistant **et toute la documentation de ce dépôt**. Moi, j'ai fait le câblage, les soudures, les appuis sur les boutons, l'écoute du poêle et les essais. Claude Code a fait l'analyse, le code et la rédaction, qui dépassent mon niveau technique.
> Arriver jusque-là, en partant de « est-ce que je peux écouter la télécommande de mon poêle ? », a été un grand pas pour moi. J'espère que ce dépôt servira de **base solide à quelqu'un d'autre**, idéalement avec son propre agent IA, pour connecter lui aussi son poêle Piazzetta. Ces poêles sont ultra robustes, je n'ai jamais eu le moindre souci avec le mien, et la seule chose qui me manquait, c'était de pouvoir le piloter à distance. C'est chose faite.
> Pour reproduire le projet avec un agent IA, voir **[docs/ai-assistant-guide.md](docs/ai-assistant-guide.md)**.

---

## Ce que ça fait

* **Lit l'état du poêle en continu** : le poêle envoie par radio le texte de l'écran de la télécommande (`NIVEAU P1`, `ALLUMAGE PHASE I`, `NETTOYAGE BRASERO`, `OFF`…), plus la température ambiante, la puissance et la vitesse du ventilateur.
* **Commande le poêle** : puissance 1 à 4, ventilateur 1 à 4, **marche/arrêt** (appui long simulé sur la touche ⏻). Chaque commande est **vérifiée** grâce à la réponse du poêle.
* **S'intègre à Home Assistant via ESPHome** : les entités apparaissent toutes seules (état, phase, température, puissance, ventilateur, marche/arrêt, diagnostic radio).
* **Fournit un thermostat** : un *thermostat générique* Home Assistant allume et éteint le poêle autour d'une consigne, avec une hystérésis et une durée minimale de cycle, à partir de n'importe quelle sonde de température de la pièce.
* **Comprend un banc de test** (firmware ESP32 + page web locale) pour **capturer, décoder, comparer et réémettre** les trames. C'est lui qui a permis la rétro-ingénierie, et c'est lui qui vous servira à vérifier votre propre installation.

## Le principe

La télécommande est un **simple terminal** : c'est le poêle qui gère les menus et qui envoie le texte à afficher. Quand elle est réveillée, la télécommande interroge le poêle toutes les quelques secondes et envoie les codes des touches. Le module parle exactement le même langage.
Le protocole complet est décrit dans **[docs/protocol.md](docs/protocol.md)**.

## Matériel (environ 15 €)

| Pièce | Remarques |
| --- | --- |
| ESP32 DevKit (ESP32-WROOM-32) | l'ESP32 classique, carte testée |
| Module Ra-02 (AI-Thinker, **SX1278**, 433 MHz) | il doit gérer l'**OOK** ; la SX1262 des cartes Heltec V3 ne le fait **pas** |
| Antenne 433 MHz | une vraie antenne 433 MHz ; **ne jamais émettre sans antenne** |
| Alimentation USB | le module reste alimenté en permanence |

Câblage et conseils : **[docs/hardware.md](docs/hardware.md)**.

## Compatibilité

* **Testé** avec la télécommande Piazzetta **F0203-1480V3** (bidirectionnelle, écran LCD) et le poêle Piazzetta qui l'accompagne (puissance P1 à P4, ventilateur 1 à 4).
* **Inconnu** : le premier octet des trames de la télécommande (`0xC5` ici, option `remote_id`) est-il le même pour toutes les télécommandes, ou propre à chaque couple télécommande/poêle ? Le composant signale dans ses journaux toute télécommande entendue avec un autre identifiant. **Merci de partager vos résultats.**

## Pour démarrer

1. **Monter le matériel** : [docs/hardware.md](docs/hardware.md).
2. **Vérifier son installation avec le banc de test** : flasher `test-bench/firmware/test_bench`, ouvrir la page web, capturer les trames de sa télécommande, trouver son `remote_id` ([docs/capture-guide.md](docs/capture-guide.md)).
3. **Installer le composant ESPHome** : [docs/esphome.md](docs/esphome.md).
4. **Configurer Home Assistant** (thermostat, tableau de bord, alerte, collecte de données) : [docs/home-assistant.md](docs/home-assistant.md).
5. Lire **[docs/lessons-learned.md](docs/lessons-learned.md)** : tous les pièges rencontrés, pour vous les éviter.

Les commentaires du code et l'interface du banc de test sont en français, la langue d'origine du projet.

## Contribuer

Bienvenue, en particulier pour :

* **votre `remote_id`** et le modèle de votre télécommande ou de votre poêle ;
* **les touches et écrans non encore décodés** : thermomètre, horloge/programmation, ★, messages de défaut ;
* **d'autres modèles Piazzetta** compatibles, ou pas.

## Licence

[MIT](LICENSE). « Piazzetta » est une marque de son propriétaire, citée uniquement pour indiquer la compatibilité.
