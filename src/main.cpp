#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>

namespace {

// Use the onboard BOOT button by default.
// On most ESP32-C3 SuperMini boards this is tied to GPIO9.
constexpr uint8_t SCORE_BUTTON_PIN = 9;

constexpr unsigned long DEBOUNCE_MS = 35;
constexpr unsigned long DOUBLE_CLICK_MS = 320;

constexpr char AP_SSID[] = "Marcador-ESP32";
constexpr int AP_CHANNEL = 1;
constexpr int AP_MAX_CONNECTIONS = 1;
constexpr uint16_t DNS_PORT = 53;

struct Button {
  uint8_t pin;
  bool stableLevel;
  bool lastReading;
  unsigned long lastChangeAt;
};

struct TeamScore {
  int games;
  int points;
};

DNSServer dnsServer;
WebServer server(80);

Button scoreButton{SCORE_BUTTON_PIN, HIGH, HIGH, 0};

TeamScore teamA{0, 0};
TeamScore teamB{0, 0};
bool singleClickPending = false;
unsigned long lastButtonReleaseAt = 0;

const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="ca">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Marcador ESP32</title>
  <style>
    :root {
      --bg-1: #07131f;
      --bg-2: #12304a;
      --card: rgba(7, 19, 31, 0.82);
      --line: rgba(166, 212, 255, 0.2);
      --text: #f5fbff;
      --muted: #9fb6c8;
      --accent-a: #ffb703;
      --accent-b: #8ecae6;
      --danger: #fb5607;
      --ok: #80ed99;
      font-family: "Trebuchet MS", "Segoe UI", sans-serif;
    }

    * {
      box-sizing: border-box;
    }

    body {
      margin: 0;
      min-height: 100vh;
      background:
        radial-gradient(circle at top left, rgba(255, 183, 3, 0.16), transparent 28%),
        radial-gradient(circle at top right, rgba(142, 202, 230, 0.18), transparent 30%),
        linear-gradient(145deg, var(--bg-1), var(--bg-2));
      color: var(--text);
      display: grid;
      place-items: center;
      padding: 20px;
    }

    .app {
      width: min(720px, 100%);
      background: var(--card);
      border: 1px solid var(--line);
      border-radius: 28px;
      backdrop-filter: blur(12px);
      box-shadow: 0 22px 70px rgba(0, 0, 0, 0.35);
      overflow: hidden;
    }

    .hero {
      padding: 22px 22px 16px;
      border-bottom: 1px solid var(--line);
    }

    .eyebrow {
      margin: 0 0 6px;
      color: var(--muted);
      font-size: 0.85rem;
      text-transform: uppercase;
      letter-spacing: 0.14em;
    }

    h1 {
      margin: 0;
      font-size: clamp(1.8rem, 6vw, 2.6rem);
      line-height: 1;
    }

    .subtitle {
      margin: 10px 0 0;
      color: var(--muted);
      font-size: 0.95rem;
    }

    .scoreboard {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 14px;
      padding: 18px;
    }

    .team {
      padding: 18px;
      border-radius: 22px;
      background: rgba(255, 255, 255, 0.03);
      border: 1px solid var(--line);
      min-height: 290px;
      display: grid;
      align-content: space-between;
    }

    .team-name {
      margin: 0;
      color: var(--muted);
      text-transform: uppercase;
      letter-spacing: 0.12em;
      font-size: 0.85rem;
    }

    .score {
      margin: 12px 0;
      font-size: clamp(4.8rem, 20vw, 7.5rem);
      line-height: 0.9;
      font-weight: 800;
    }

    .games-wrap {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 16px;
      margin-top: 16px;
      padding-top: 14px;
      border-top: 1px solid var(--line);
      color: var(--muted);
    }

    .games-label {
      text-transform: uppercase;
      letter-spacing: 0.12em;
      font-size: 0.78rem;
    }

    .games {
      font-size: 2rem;
      font-weight: 800;
      color: var(--text);
    }

    .score.a {
      color: var(--accent-a);
    }

    .score.b {
      color: var(--accent-b);
    }

    .controls {
      display: flex;
      gap: 10px;
      flex-wrap: wrap;
    }

    button {
      border: 0;
      border-radius: 999px;
      padding: 12px 18px;
      color: var(--bg-1);
      font-weight: 700;
      font-size: 1rem;
      cursor: pointer;
      transition: transform 120ms ease, opacity 120ms ease;
    }

    button:active {
      transform: scale(0.98);
    }

    .add-a {
      background: var(--accent-a);
    }

    .add-b {
      background: var(--accent-b);
    }

    .reset {
      width: calc(100% - 36px);
      margin: 0 18px 18px;
      background: var(--danger);
      color: white;
    }

    .footer {
      padding: 0 18px 20px;
      color: var(--muted);
      font-size: 0.9rem;
      display: flex;
      justify-content: space-between;
      gap: 14px;
      flex-wrap: wrap;
    }

    .status {
      color: var(--ok);
      font-weight: 700;
    }

    @media (max-width: 640px) {
      .scoreboard {
        grid-template-columns: 1fr;
      }

      .team {
        min-height: 210px;
      }

      .reset {
        width: calc(100% - 36px);
      }
    }
  </style>
</head>
<body>
  <main class="app">
    <section class="hero">
      <p class="eyebrow">ESP32-C3 SuperMini</p>
      <h1>Marcador de Padel</h1>
      <p class="subtitle">1 clic suma punt a la parella A. 2 clics rapids el sumen a la parella B.</p>
    </section>

    <section class="scoreboard">
      <article class="team">
        <div>
          <p class="team-name">Parella A</p>
          <div id="pointsA" class="score a">0</div>
          <div class="games-wrap">
            <span class="games-label">Jocs</span>
            <span id="gamesA" class="games">0</span>
          </div>
        </div>
        <div class="controls">
          <button class="add-a" data-team="a">Punt Parella A</button>
        </div>
      </article>

      <article class="team">
        <div>
          <p class="team-name">Parella B</p>
          <div id="pointsB" class="score b">0</div>
          <div class="games-wrap">
            <span class="games-label">Jocs</span>
            <span id="gamesB" class="games">0</span>
          </div>
        </div>
        <div class="controls">
          <button class="add-b" data-team="b">Punt Parella B</button>
        </div>
      </article>
    </section>

    <button class="reset" id="reset">Reset marcador</button>

    <section class="footer">
      <span>IP ESP32: <strong id="apIp">192.168.4.1</strong></span>
      <span id="status" class="status">Connectat</span>
    </section>
  </main>

  <script>
    const pointsA = document.getElementById("pointsA");
    const pointsB = document.getElementById("pointsB");
    const gamesA = document.getElementById("gamesA");
    const gamesB = document.getElementById("gamesB");
    const apIp = document.getElementById("apIp");
    const statusEl = document.getElementById("status");

    async function refreshScore() {
      try {
        const response = await fetch("/api/state");
        if (!response.ok) throw new Error("bad response");
        const data = await response.json();
        pointsA.textContent = data.teamAPoints;
        pointsB.textContent = data.teamBPoints;
        gamesA.textContent = data.teamAGames;
        gamesB.textContent = data.teamBGames;
        apIp.textContent = data.apIp;
        statusEl.textContent = "Connectat";
      } catch (error) {
        statusEl.textContent = "Sense connexio";
      }
    }

    async function increment(team) {
      await fetch(`/api/increment?team=${team}`, { method: "POST" });
      refreshScore();
    }

    async function resetScore() {
      await fetch("/api/reset", { method: "POST" });
      refreshScore();
    }

    document.querySelectorAll("[data-team]").forEach((button) => {
      button.addEventListener("click", () => increment(button.dataset.team));
    });

    document.getElementById("reset").addEventListener("click", resetScore);

    refreshScore();
    setInterval(refreshScore, 600);
  </script>
</body>
</html>
)HTML";

bool isPressed(const Button &button) {
  return button.stableLevel == LOW;
}

const char *pointLabel(const TeamScore &team, const TeamScore &other) {
  if (team.points >= 3 && other.points >= 3) {
    if (team.points == other.points) {
      return "40";
    }
    if (team.points == other.points + 1) {
      return "Ad";
    }
    return "40";
  }

  switch (team.points) {
    case 0:
      return "0";
    case 1:
      return "15";
    case 2:
      return "30";
    default:
      return "40";
  }
}

void resetPoints() {
  teamA.points = 0;
  teamB.points = 0;
}

void resetScores() {
  teamA.games = 0;
  teamA.points = 0;
  teamB.games = 0;
  teamB.points = 0;
  singleClickPending = false;
  lastButtonReleaseAt = 0;
}

void awardPoint(TeamScore &winner, TeamScore &loser) {
  winner.points++;

  if (winner.points >= 4 && (winner.points - loser.points) >= 2) {
    winner.games++;
    resetPoints();
  }
}

bool updateButton(Button &button) {
  const bool reading = digitalRead(button.pin);

  if (reading != button.lastReading) {
    button.lastReading = reading;
    button.lastChangeAt = millis();
  }

  if ((millis() - button.lastChangeAt) < DEBOUNCE_MS) {
    return false;
  }

  if (reading == button.stableLevel) {
    return false;
  }

  button.stableLevel = reading;
  if (button.stableLevel == HIGH) {
    return true;
  }

  return false;
}

void updateScoreButton() {
  if (updateButton(scoreButton)) {
    const unsigned long now = millis();
    if (singleClickPending && (now - lastButtonReleaseAt <= DOUBLE_CLICK_MS)) {
      awardPoint(teamB, teamA);
      singleClickPending = false;
      lastButtonReleaseAt = 0;
    } else {
      singleClickPending = true;
      lastButtonReleaseAt = now;
    }
  }

  if (singleClickPending && !isPressed(scoreButton) &&
      (millis() - lastButtonReleaseAt > DOUBLE_CLICK_MS)) {
    awardPoint(teamA, teamB);
    singleClickPending = false;
    lastButtonReleaseAt = 0;
  }
}

String jsonState() {
  IPAddress ip = WiFi.softAPIP();
  String json = "{";
  json += "\"teamAPoints\":\"";
  json += pointLabel(teamA, teamB);
  json += "\",\"teamAGames\":";
  json += teamA.games;
  json += ",\"teamBPoints\":\"";
  json += pointLabel(teamB, teamA);
  json += "\",\"teamBGames\":";
  json += teamB.games;
  json += ",\"apIp\":\"";
  json += ip.toString();
  json += "\"}";
  return json;
}

void handleIncrement() {
  const String team = server.arg("team");
  if (team == "a") {
    awardPoint(teamA, teamB);
  } else if (team == "b") {
    awardPoint(teamB, teamA);
  } else {
    server.send(400, "text/plain", "Missing or invalid team");
    return;
  }

  server.send(200, "application/json", jsonState());
}

void redirectToPortal() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

void configureRoutes() {
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", PAGE);
  });

  server.on("/generate_204", HTTP_GET, redirectToPortal);
  server.on("/gen_204", HTTP_GET, redirectToPortal);
  server.on("/hotspot-detect.html", HTTP_GET, redirectToPortal);
  server.on("/canonical.html", HTTP_GET, redirectToPortal);
  server.on("/success.txt", HTTP_GET, redirectToPortal);
  server.on("/ncsi.txt", HTTP_GET, redirectToPortal);
  server.on("/connecttest.txt", HTTP_GET, redirectToPortal);
  server.on("/redirect", HTTP_GET, redirectToPortal);

  server.on("/api/state", HTTP_GET, []() {
    server.send(200, "application/json", jsonState());
  });

  server.on("/api/increment", HTTP_POST, handleIncrement);

  server.on("/api/reset", HTTP_POST, []() {
    resetScores();
    server.send(200, "application/json", jsonState());
  });

  server.onNotFound([]() {
    redirectToPortal();
  });
}

void startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, nullptr, AP_CHANNEL, 0, AP_MAX_CONNECTIONS);
  esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  Serial.println();
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.println("Password: open network");
  Serial.print("Channel: ");
  Serial.println(AP_CHANNEL);
  Serial.println("Bandwidth: HT20");
  Serial.print("Open: http://");
  Serial.println(WiFi.softAPIP());
}

}  // namespace

void setup() {
  Serial.begin(115200);

  pinMode(scoreButton.pin, INPUT_PULLUP);

  startAccessPoint();
  configureRoutes();
  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  updateScoreButton();
}
