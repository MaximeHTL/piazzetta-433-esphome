# Petit serveur local du banc poele 433.
# - sert la page : http://localhost:8765/test-bench.html (Web Serial fonctionne sur localhost)
# - recoit le journal en direct de la page (POST /log) et l'ajoute a un fichier JSONL,
#   que Claude lit pendant les essais.
#
#   python serve.py                 # journal dans Telechargements\banc-poele-live.jsonl
#   python serve.py --port 8765 --journal C:\chemin\fichier.jsonl

import argparse
import http.server
import os
import threading

DOSSIER = os.path.dirname(os.path.abspath(__file__))

parser = argparse.ArgumentParser()
parser.add_argument("--port", type=int, default=8765)
parser.add_argument("--journal", default=os.path.join(os.path.expanduser("~"), "Downloads", "banc-poele-live.jsonl"))
args = parser.parse_args()
verrou = threading.Lock()


class Gestionnaire(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=DOSSIER, **k)

    def do_POST(self):
        if self.path != "/log":
            self.send_error(404)
            return
        n = int(self.headers.get("Content-Length", 0))
        corps = self.rfile.read(n)
        with verrou, open(args.journal, "ab") as f:
            f.write(corps)
        self.send_response(204)
        self.end_headers()

    def end_headers(self):
        # La page doit toujours etre rechargee a jour
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    print(f"Banc poele 433 : http://localhost:{args.port}/test-bench.html")
    print(f"Journal en direct : {args.journal}")
    http.server.ThreadingHTTPServer(("127.0.0.1", args.port), Gestionnaire).serve_forever()
