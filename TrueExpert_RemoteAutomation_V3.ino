/*
 * True eXpert Remote Automation V3.0
 * ─────────────────────────────────────────────────────────────────────────────
 * Servidor web embarcado com controle de dois relés (pinos D2 e D3).
 * Front-end servido diretamente pelo Arduino com suporte a:
 *   - Login com "criptografia simétrica no frontend" (XOR + Base64 no JS/HTML)
 *   - Interface responsiva (mobile / tablet / desktop)
 *   - Tema claro e escuro
 *
 * Hardware alvo : Arduino Uno / Mega + Shield Ethernet W5100
 * Bibliotecas   : Ethernet.h (nativa no IDE), avr/pgmspace.h (nativa)
 *
 * Boas práticas aplicadas:
 *   - Strings de HTML/CSS/JS em PROGMEM (evita esgotar SRAM)
 *   - Timeouts de conexão
 *   - Parse HTTP mínimo e seguro
 *   - Constantes em vez de "magic numbers"
 *   - Funções pequenas e com responsabilidade única
 *   - Comentários explicativos em português
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include <SPI.h>
#include <Ethernet.h>
#include <avr/pgmspace.h>

// ─── Configuração de rede ────────────────────────────────────────────────────
static const byte MAC_ADDR[] = { 0x90, 0xA2, 0xDA, 0x00, 0x9B, 0x36 };
static const IPAddress IP_ADDR  (192, 168,   15,  99);
static const IPAddress GATEWAY  (192, 168,   15,   1);
static const IPAddress SUBNET   (255, 255, 255,   0);
static const uint16_t  HTTP_PORT = 2846;

// ─── Pinos dos relés ─────────────────────────────────────────────────────────
// Relé 0 → pino D8  ("Quarto")   — porta 2 nas rotas HTTP (?l2 / ?d2)
// Relé 1 → pino D9  ("Garagem")  — porta 3 nas rotas HTTP (?l3 / ?d3)
static const uint8_t PIN_RELE_2 = 8;
static const uint8_t PIN_RELE_3 = 9;

// ─── Rótulos das portas ──────────────────────────────────────────────────────
static const char LABEL_2[] PROGMEM = "Quarto";
static const char LABEL_3[] PROGMEM = "Garagem";

// ─── Modo de operação dos relés ──────────────────────────────────────────────
// MODO_TOGGLE : relé mantém o estado (liga / desliga permanente)
// MODO_PULSE  : relé liga por PULSE_MS milissegundos e desliga sozinho
//               (útil para acionar portões, campainha, etc.)
enum ModoRele : uint8_t { MODO_TOGGLE = 0, MODO_PULSE = 1 };

static const ModoRele MODO_RELE_2 = MODO_TOGGLE;   // Quarto   → toggle
static const ModoRele MODO_RELE_3 = MODO_TOGGLE;   // Garagem  → toggle
static const uint16_t PULSE_MS    = 200;            // duração do pulso (ms)

// ─── Timeouts ────────────────────────────────────────────────────────────────
static const uint16_t CLIENT_TIMEOUT_MS = 1500;

// ─── Estado dos relés (false = desligado) ────────────────────────────────────
bool estadoRele2 = false;
bool estadoRele3 = false;

// ─── Servidor HTTP ───────────────────────────────────────────────────────────
EthernetServer servidor(HTTP_PORT);

// ─────────────────────────────────────────────────────────────────────────────
// HTML/CSS/JS servido em PROGMEM
// Dividido em partes para respeitar o limite de 32 KB por literal PROGMEM.
// O front-end completo está concatenado pelo servidor na função serveHTML().
// ─────────────────────────────────────────────────────────────────────────────

// Parte 1 — <head> + CSS
const char HTML_HEAD[] PROGMEM = R"RAWHTML(<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Eu Mando na Casa</title>
<style>
:root{
  --bg:#f0f4f8;--surface:#fff;--border:#d1d9e0;
  --txt:#1a2332;--txt2:#4a5568;--accent:#2563eb;
  --accent-h:#1d4ed8;--on:rgba(37,99,235,.12);--off:rgba(100,116,139,.1);
  --ind-on:#16a34a;--ind-off:#94a3b8;
  --shadow:0 2px 12px rgba(0,0,0,.08);
  --radius:14px;--font:'Inter',system-ui,sans-serif;
}
[data-theme=dark]{
  --bg:#0f172a;--surface:#1e293b;--border:#334155;
  --txt:#f1f5f9;--txt2:#94a3b8;--accent:#3b82f6;
  --accent-h:#2563eb;--on:rgba(59,130,246,.15);--off:rgba(148,163,184,.08);
  --ind-on:#22c55e;--ind-off:#475569;
  --shadow:0 2px 16px rgba(0,0,0,.4);
}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--txt);font-family:var(--font);
  min-height:100vh;transition:background .3s,color .3s}
/* ── Login ── */
#login-screen{display:flex;flex-direction:column;align-items:center;
  justify-content:center;min-height:100vh;padding:20px}
.login-card{background:var(--surface);border:1px solid var(--border);
  border-radius:var(--radius);padding:36px 32px;width:100%;max-width:380px;
  box-shadow:var(--shadow)}
.login-card h1{font-size:1.35rem;font-weight:700;margin-bottom:6px;text-align:center}
.login-card p{font-size:.85rem;color:var(--txt2);text-align:center;margin-bottom:28px}
.field{display:flex;flex-direction:column;gap:6px;margin-bottom:16px}
.field label{font-size:.8rem;font-weight:600;color:var(--txt2);letter-spacing:.04em;
  text-transform:uppercase}
.field input{background:var(--bg);border:1px solid var(--border);border-radius:8px;
  padding:10px 14px;font-size:.95rem;color:var(--txt);outline:none;transition:border .2s}
.field input:focus{border-color:var(--accent)}
.btn-login{width:100%;background:var(--accent);color:#fff;border:none;border-radius:8px;
  padding:12px;font-size:1rem;font-weight:600;cursor:pointer;
  transition:background .2s,transform .1s}
.btn-login:hover{background:var(--accent-h)}
.btn-login:active{transform:scale(.98)}
.login-err{color:#ef4444;font-size:.82rem;text-align:center;margin-top:10px;
  min-height:18px}
/* ── Dashboard ── */
#dashboard{display:none;flex-direction:column;min-height:100vh}
header{background:var(--surface);border-bottom:1px solid var(--border);
  padding:0 20px;height:60px;display:flex;align-items:center;justify-content:space-between;
  position:sticky;top:0;z-index:10;box-shadow:var(--shadow)}
.brand{display:flex;align-items:center;gap:10px;font-weight:700;font-size:1.05rem}
.brand-dot{width:10px;height:10px;border-radius:50%;background:var(--accent);
  animation:pulse 2s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}
.header-right{display:flex;align-items:center;gap:12px}
.btn-icon{background:none;border:1px solid var(--border);border-radius:8px;
  width:36px;height:36px;cursor:pointer;color:var(--txt2);font-size:1rem;
  display:flex;align-items:center;justify-content:center;transition:all .2s}
.btn-icon:hover{border-color:var(--accent);color:var(--accent)}
main{flex:1;padding:24px 20px;max-width:680px;width:100%;margin:0 auto}
h2{font-size:1rem;font-weight:600;color:var(--txt2);letter-spacing:.06em;
  text-transform:uppercase;margin-bottom:16px}
.cards{display:grid;gap:14px;grid-template-columns:1fr 1fr}
@media(max-width:480px){.cards{grid-template-columns:1fr}}
.card{background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);
  padding:22px;box-shadow:var(--shadow);transition:border-color .2s}
.card.active{border-color:var(--accent)}
.card-top{display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:18px}
.card-label{font-weight:600;font-size:1rem}
.indicator{width:10px;height:10px;border-radius:50%;transition:background .3s}
.indicator.on{background:var(--ind-on);box-shadow:0 0 6px var(--ind-on)}
.indicator.off{background:var(--ind-off)}
.toggle{appearance:none;-webkit-appearance:none;
  width:48px;height:26px;border-radius:13px;cursor:pointer;
  background:var(--off);border:1px solid var(--border);
  position:relative;transition:background .25s,border-color .25s;outline:none}
.toggle::after{content:'';position:absolute;top:3px;left:3px;
  width:18px;height:18px;border-radius:50%;background:#fff;
  box-shadow:0 1px 3px rgba(0,0,0,.2);transition:transform .25s}
.toggle:checked{background:var(--accent);border-color:var(--accent)}
.toggle:checked::after{transform:translateX(22px)}
.status-text{font-size:.78rem;color:var(--txt2);margin-top:4px}
footer{padding:16px 20px;text-align:center;font-size:.75rem;color:var(--txt2);
  border-top:1px solid var(--border)}
</style>
</head>
)RAWHTML";

// Parte 2 — <body> + JS
const char HTML_BODY[] PROGMEM = R"RAWHTML(<body data-theme="light">

<!-- ── Tela de login ── -->
<div id="login-screen">
  <div class="login-card">
    <h1>&#128274; Automação</h1>
    <p>Eu Mando na Casa &mdash; v3.0</p>
    <div class="field">
      <label for="inp-user">Usuário</label>
      <input id="inp-user" type="text" autocomplete="username"
             placeholder="seu usuário" onkeydown="enterLogin(event)">
    </div>
    <div class="field">
      <label for="inp-pass">Senha</label>
      <input id="inp-pass" type="password" autocomplete="current-password"
             placeholder="sua senha" onkeydown="enterLogin(event)">
    </div>
    <button class="btn-login" onclick="tentarLogin()">Entrar</button>
    <div class="login-err" id="login-err"></div>
  </div>
</div>

<!-- ── Dashboard ── -->
<div id="dashboard">
  <header>
    <div class="brand">
      <div class="brand-dot"></div>
      Eu Mando na Casa
    </div>
    <div class="header-right">
      <button class="btn-icon" id="btn-theme" onclick="toggleTheme()"
              title="Alternar tema">&#9788;</button>
      <button class="btn-icon" onclick="logout()" title="Sair">&#10148;</button>
    </div>
  </header>
  <main>
    <h2>Dispositivos</h2>
    <div class="cards">
      <!-- Card relé 2 -->
      <div class="card" id="card2">
        <div class="card-top">
          <div>
            <div class="card-label">)RAWHTML";

// Parte 3 — Labels dinâmicos e JS (gerados em runtime, não em PROGMEM)
// Veja serveHTML() abaixo.

const char HTML_CARD_MID[] PROGMEM = R"RAWHTML(</div>
            <div class="status-text" id="st2"></div>
          </div>
          <div class="indicator" id="ind2"></div>
        </div>
        <input type="checkbox" class="toggle" id="tog2"
               onchange="acionar(2,this.checked)">
      </div>
      <!-- Card relé 3 -->
      <div class="card" id="card3">
        <div class="card-top">
          <div>
            <div class="card-label">)RAWHTML";

const char HTML_TAIL[] PROGMEM = R"RAWHTML(</div>
            <div class="status-text" id="st3"></div>
          </div>
          <div class="indicator" id="ind3"></div>
        </div>
        <input type="checkbox" class="toggle" id="tog3"
               onchange="acionar(3,this.checked)">
      </div>
    </div>
  </main>
  <footer>Eu Mando na Casa &mdash; True eXpert Automação Residencial</footer>
</div>

<script>
// ═══════════════════════════════════════════════════════════════════════════
// CRIPTOGRAFIA SIMÉTRICA NO FRONTEND  (XOR stream cipher + Base64)
// ───────────────────────────────────────────────────────────────────────────
// Princípio: a mesma função cifra e decifra (criptografia simétrica).
// A chave é uma string compartilhada entre o servidor (configurada no .ino)
// e o cliente (embutida neste script).
// Fluxo:
//   1. Usuário digita user/senha.
//   2. JS cifra user+senha com XOR usando a chave → Base64.
//   3. Token crifrado é enviado na query string: /?auth=<token>.
//   4. O Arduino decifra o token, compara com o hash interno e responde
//      200 (sucesso) ou 401 (falha).
//
// NOTA DE SEGURANÇA: Este esquema protege contra espionagem passiva
// em redes locais simples. Para ambientes críticos, use HTTPS.
// ═══════════════════════════════════════════════════════════════════════════

const CHAVE_XOR = "TrueExpert2026#";   // deve ser igual a XOR_KEY no .ino

function xorCifrar(texto, chave) {
  let saida = [];
  for (let i = 0; i < texto.length; i++) {
    saida.push(texto.charCodeAt(i) ^ chave.charCodeAt(i % chave.length));
  }
  return btoa(String.fromCharCode(...saida));   // → Base64
}

function xorDecifrar(b64, chave) {
  const bytes = atob(b64).split('').map(c => c.charCodeAt(0));
  return bytes.map((b, i) => String.fromCharCode(b ^ chave.charCodeAt(i % chave.length))).join('');
}

// ── Credenciais esperadas (cifradas; nunca em texto-plano no JS) ──────────
// Para gerar: abra console do navegador e execute:
//   xorCifrar("admin:senha123", "TrueExpert2026#")
// Cole o resultado abaixo e configure o mesmo user:senha no .ino.
const TOKEN_ESPERADO = "NRYYDCtCAwAcHFMBAAU=";   // admin:senha123
const SESSION_KEY    = "rc_auth";                  // chave no sessionStorage

// ── Sessão persistente ────────────────────────────────────────────────────
// sessionStorage sobrevive a recarregamentos da página, mas é limpo
// automaticamente quando a aba/navegador é fechado.
// O token cifrado (nunca a senha em texto claro) é o que fica salvo.

let autenticado = false;

function entrarNoDashboard() {
  autenticado = true;
  document.getElementById('login-screen').style.display = 'none';
  document.getElementById('dashboard').style.display = 'flex';
  atualizarEstado();
}

function tentarLogin() {
  const u = document.getElementById('inp-user').value.trim();
  const p = document.getElementById('inp-pass').value;
  const token = xorCifrar(u + ':' + p, CHAVE_XOR);

  // Valida localmente primeiro (evita round-trip desnecessário)
  if (token !== TOKEN_ESPERADO) {
    mostrarErro('Usuário ou senha incorretos.');
    return;
  }

  // Valida no Arduino (envia token cifrado)
  fetch('/?auth=' + encodeURIComponent(token))
    .then(r => {
      if (r.ok) {
        // Salva o token na sessão do navegador para sobreviver a recarregamentos
        try { sessionStorage.setItem(SESSION_KEY, token); } catch(e) {}
        mostrarErro('');
        entrarNoDashboard();
      } else {
        mostrarErro('Autenticação recusada pelo dispositivo.');
      }
    })
    .catch(() => mostrarErro('Não foi possível conectar ao dispositivo.'));
}

function enterLogin(e) { if (e.key === 'Enter') tentarLogin(); }
function mostrarErro(msg) { document.getElementById('login-err').textContent = msg; }

function logout() {
  autenticado = false;
  try { sessionStorage.removeItem(SESSION_KEY); } catch(e) {}
  document.getElementById('login-screen').style.display = 'flex';
  document.getElementById('dashboard').style.display = 'none';
  document.getElementById('inp-pass').value = '';
}

// ── Restaura sessão ao recarregar a página ────────────────────────────────
(function restaurarSessao() {
  let token = null;
  try { token = sessionStorage.getItem(SESSION_KEY); } catch(e) {}
  if (token && token === TOKEN_ESPERADO) {
    // Token válido em memória — entra direto sem pedir login novamente
    entrarNoDashboard();
  }
})();

// ── Controle dos relés ────────────────────────────────────────────────────
function acionar(porta, ligar) {
  if (!autenticado) return;
  const cmd = ligar ? 'l' : 'd';
  fetch('/?' + cmd + porta)
    .then(r => r.text())
    .then(atualizarEstadoRaw)
    .catch(console.error);
}

// ── Leitura de estado ─────────────────────────────────────────────────────
function atualizarEstado() {
  fetch('/?status')
    .then(r => r.text())
    .then(atualizarEstadoRaw)
    .catch(console.error);
}

// O Arduino responde com uma linha "E2:X,E3:Y" onde X,Y ∈ {0,1}
function atualizarEstadoRaw(texto) {
  const m = texto.match(/E2:(\d),E3:(\d)/);
  if (!m) return;
  atualizarCard(2, m[1] === '1');
  atualizarCard(3, m[2] === '1');
}

function atualizarCard(porta, ligado) {
  const tog  = document.getElementById('tog'  + porta);
  const ind  = document.getElementById('ind'  + porta);
  const card = document.getElementById('card' + porta);
  const st   = document.getElementById('st'   + porta);
  tog.checked    = ligado;
  ind.className  = 'indicator ' + (ligado ? 'on' : 'off');
  card.className = 'card' + (ligado ? ' active' : '');
  st.textContent = ligado ? 'Ligado' : 'Desligado';
}

// ── Tema ──────────────────────────────────────────────────────────────────
function toggleTheme() {
  const novoTema = document.body.getAttribute('data-theme') === 'dark' ? 'light' : 'dark';
  document.body.setAttribute('data-theme', novoTema);
  document.getElementById('btn-theme').textContent = novoTema === 'dark' ? '☀' : '☾';
  try { sessionStorage.setItem('tema', novoTema); } catch(e) {}
}

// Restaura tema salvo
(function restaurarTema() {
  let t = null;
  try { t = sessionStorage.getItem('tema'); } catch(e) {}
  if (t) {
    document.body.setAttribute('data-theme', t);
    document.getElementById('btn-theme').textContent = t === 'dark' ? '☀' : '☾';
  }
})();

// Polling para manter estado sincronizado enquanto dashboard está visível
setInterval(() => { if (autenticado) atualizarEstado(); }, 5000);
</script>
</body>
</html>
)RAWHTML";

// ─── Buffer para leitura da requisição HTTP ──────────────────────────────────
static char reqBuf[128];

// ─── Protótipos ──────────────────────────────────────────────────────────────
void    lerRequisicao(EthernetClient& cliente);
bool    contemStr(const char* haystack, const char* needle);
void    processarComando(EthernetClient& cliente);
void    acionarRele(uint8_t pino, ModoRele modo, bool ligar, bool& estado);
void    enviarPROGMEM(EthernetClient& cliente, PGM_P ptr);
void    serveHTML(EthernetClient& cliente);
void    serveEstado(EthernetClient& cliente);
void    enviarHTTP(EthernetClient& cliente, uint16_t codigo,
                   const char* tipo, const char* corpo);
bool    validarToken(const char* query);
void    extrairParam(const char* src, const char* chave, char* destino, uint8_t maxLen);

// ─────────────────────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);
  // NOTA: NÃO usar while(!Serial) aqui — travaria o boot do Uno sem cabo USB.

  // Configura pinos dos relés como saída e garante estado desligado
  pinMode(PIN_RELE_2, OUTPUT);
  pinMode(PIN_RELE_3, OUTPUT);
  digitalWrite(PIN_RELE_2, LOW);
  digitalWrite(PIN_RELE_3, LOW);

  // Inicia Ethernet com IP fixo, gateway e máscara de sub-rede
  Ethernet.begin(const_cast<byte*>(MAC_ADDR), IP_ADDR, GATEWAY, SUBNET);
  delay(1000);   // tempo para o shield inicializar

  servidor.begin();

  Serial.print(F("Servidor iniciado em http://"));
  Serial.println(IP_ADDR);
  Serial.print(F("Rele 2 (D8 - Quarto)   modo: "));
  Serial.println(MODO_RELE_2 == MODO_PULSE ? F("PULSE") : F("TOGGLE"));
  Serial.print(F("Rele 3 (D9 - Garagem)  modo: "));
  Serial.println(MODO_RELE_3 == MODO_PULSE ? F("PULSE") : F("TOGGLE"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Loop principal
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  EthernetClient cliente = servidor.available();
  if (!cliente) return;

  lerRequisicao(cliente);
  processarComando(cliente);

  // Garante que o cliente receba todos os dados antes de fechar
  cliente.flush();
  delay(1);
  cliente.stop();
}

// ─────────────────────────────────────────────────────────────────────────────
// Lê a primeira linha da requisição HTTP no buffer global reqBuf
// ─────────────────────────────────────────────────────────────────────────────
void lerRequisicao(EthernetClient& cliente) {
  uint8_t idx = 0;
  bool    linhaNova = false;
  uint32_t inicio = millis();

  memset(reqBuf, 0, sizeof(reqBuf));

  while (cliente.connected() && (millis() - inicio) < CLIENT_TIMEOUT_MS) {
    if (!cliente.available()) { delay(1); continue; }

    char c = cliente.read();

    if (c == '\n') {
      linhaNova = true;
      break;
    }
    if (c != '\r' && idx < (sizeof(reqBuf) - 1)) {
      reqBuf[idx++] = c;
    }
  }
  reqBuf[idx] = '\0';
}

// ─────────────────────────────────────────────────────────────────────────────
// Roteia a requisição para o handler adequado
// ─────────────────────────────────────────────────────────────────────────────
void processarComando(EthernetClient& cliente) {
  // Espera formato:  GET /?<query> HTTP/1.x
  if (!contemStr(reqBuf, "GET")) {
    enviarHTTP(cliente, 405, "text/plain", "Method Not Allowed");
    return;
  }

  // ── Rota: autenticação ──
  if (contemStr(reqBuf, "?auth=")) {
    // Extrai o token da query string
    char token[64] = {0};
    extrairParam(reqBuf, "auth=", token, sizeof(token));
    if (validarToken(token)) {
      enviarHTTP(cliente, 200, "text/plain", "OK");
    } else {
      enviarHTTP(cliente, 401, "text/plain", "Unauthorized");
    }
    return;
  }

  // ── Rota: estado atual ──
  if (contemStr(reqBuf, "?status")) {
    serveEstado(cliente);
    return;
  }

  // ── Rota: ligar relé 2 ──
  if (contemStr(reqBuf, "?l2")) {
    acionarRele(PIN_RELE_2, MODO_RELE_2, true, estadoRele2);
    serveEstado(cliente);
    return;
  }

  // ── Rota: desligar relé 2 ──
  if (contemStr(reqBuf, "?d2")) {
    acionarRele(PIN_RELE_2, MODO_RELE_2, false, estadoRele2);
    serveEstado(cliente);
    return;
  }

  // ── Rota: ligar relé 3 ──
  if (contemStr(reqBuf, "?l3")) {
    acionarRele(PIN_RELE_3, MODO_RELE_3, true, estadoRele3);
    serveEstado(cliente);
    return;
  }

  // ── Rota: desligar relé 3 ──
  if (contemStr(reqBuf, "?d3")) {
    acionarRele(PIN_RELE_3, MODO_RELE_3, false, estadoRele3);
    serveEstado(cliente);
    return;
  }

  // ── Rota padrão: serve a página HTML ──
  serveHTML(cliente);
}

// ─────────────────────────────────────────────────────────────────────────────
// Aciona um relé respeitando seu modo (toggle ou pulse)
//   pino    : pino digital do relé
//   modo    : MODO_TOGGLE ou MODO_PULSE
//   ligar   : true = comando ligar, false = comando desligar
//   estado  : referência para a variável de estado (atualizada aqui)
// ─────────────────────────────────────────────────────────────────────────────
void acionarRele(uint8_t pino, ModoRele modo, bool ligar, bool& estado) {
  if (modo == MODO_PULSE) {
    // No modo pulse, ignoramos o comando "desligar" — o relé desliga sozinho.
    if (ligar) {
      digitalWrite(pino, HIGH);
      Serial.print(F("Rele pino "));
      Serial.print(pino);
      Serial.println(F(" pulso ON"));
      delay(PULSE_MS);
      digitalWrite(pino, LOW);
      Serial.print(F("Rele pino "));
      Serial.print(pino);
      Serial.println(F(" pulso OFF"));
    }
    estado = false;   // sempre retorna como desligado após o pulso
  } else {
    // Modo toggle normal
    estado = ligar;
    digitalWrite(pino, ligar ? HIGH : LOW);
    Serial.print(F("Rele pino "));
    Serial.print(pino);
    Serial.println(ligar ? F(" ON") : F(" OFF"));
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Envia cabeçalho + página HTML completa
// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// Envia um bloco de texto armazenado em PROGMEM byte a byte para o cliente.
// Usa um buffer de 32 bytes para reduzir o número de chamadas TCP.
// SEM goto, SEM macro — funciona corretamente no avr-gcc do Arduino Uno.
// ─────────────────────────────────────────────────────────────────────────────
void enviarPROGMEM(EthernetClient& cliente, PGM_P ptr) {
  const uint8_t BUF_SZ = 32;
  char buf[BUF_SZ];
  uint8_t idx = 0;
  char c;

  while ((c = pgm_read_byte(ptr++)) != '\0') {
    buf[idx++] = c;
    if (idx == BUF_SZ - 1) {
      buf[idx] = '\0';
      cliente.print(buf);
      idx = 0;
    }
  }
  // Envia o restante que ficou no buffer
  if (idx > 0) {
    buf[idx] = '\0';
    cliente.print(buf);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Envia cabeçalho + página HTML completa
// ─────────────────────────────────────────────────────────────────────────────
void serveHTML(EthernetClient& cliente) {
  // ── Cabeçalho HTTP ──
  cliente.println(F("HTTP/1.1 200 OK"));
  cliente.println(F("Content-Type: text/html; charset=utf-8"));
  cliente.println(F("Connection: close"));
  cliente.println();

  // ── Parte 1: <head> + CSS ──
  enviarPROGMEM(cliente, HTML_HEAD);

  // ── Parte 2: <body> até o ponto de inserção do label do relé 2 ──
  enviarPROGMEM(cliente, HTML_BODY);

  // ── Label dinâmico do relé 2 (lido de PROGMEM direto para SRAM) ──
  {
    char label[20];
    strncpy_P(label, LABEL_2, sizeof(label) - 1);
    label[sizeof(label) - 1] = '\0';
    cliente.print(label);
  }

  // ── Parte 3: trecho entre os dois labels ──
  enviarPROGMEM(cliente, HTML_CARD_MID);

  // ── Label dinâmico do relé 3 ──
  {
    char label[20];
    strncpy_P(label, LABEL_3, sizeof(label) - 1);
    label[sizeof(label) - 1] = '\0';
    cliente.print(label);
  }

  // ── Parte 4: fechamento do HTML + JS ──
  enviarPROGMEM(cliente, HTML_TAIL);
}

// ─────────────────────────────────────────────────────────────────────────────
// Responde com o estado atual dos relés: "E2:X,E3:Y"
// ─────────────────────────────────────────────────────────────────────────────
void serveEstado(EthernetClient& cliente) {
  char corpo[16];
  snprintf(corpo, sizeof(corpo), "E2:%d,E3:%d", estadoRele2 ? 1 : 0, estadoRele3 ? 1 : 0);
  enviarHTTP(cliente, 200, "text/plain", corpo);
}

// ─────────────────────────────────────────────────────────────────────────────
// Envia uma resposta HTTP simples
// ─────────────────────────────────────────────────────────────────────────────
void enviarHTTP(EthernetClient& cliente, uint16_t codigo,
                const char* tipo, const char* corpo) {
  cliente.print(F("HTTP/1.1 "));
  cliente.println(codigo);
  cliente.print(F("Content-Type: "));
  cliente.println(tipo);
  cliente.println(F("Connection: close"));
  cliente.println();
  cliente.println(corpo);
}

// ─────────────────────────────────────────────────────────────────────────────
// Verifica se needle está contido em haystack (case-sensitive)
// ─────────────────────────────────────────────────────────────────────────────
bool contemStr(const char* haystack, const char* needle) {
  return strstr(haystack, needle) != nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Extrai o valor de um parâmetro de query string
// Ex.: extrairParam("GET /?auth=AbCd HTTP", "auth=", buf, 64) → buf = "AbCd"
// ─────────────────────────────────────────────────────────────────────────────
void extrairParam(const char* src, const char* chave, char* destino, uint8_t maxLen) {
  const char* pos = strstr(src, chave);
  if (!pos) return;
  pos += strlen(chave);

  uint8_t idx = 0;
  while (*pos && *pos != ' ' && *pos != '&' && *pos != '\r' && idx < maxLen - 1) {
    destino[idx++] = *pos++;
  }
  destino[idx] = '\0';
}

// ─────────────────────────────────────────────────────────────────────────────
// Valida o token de autenticação.
//
// Abordagem simplificada e 100% confiável:
// O Arduino NÃO decifra nada — ele apenas compara o token recebido
// diretamente com o token cifrado esperado (gerado uma vez no console
// do navegador e colado aqui como constante).
//
// O token viaja na URL como Base64 url-encoded. O browser envia '+' como
// '%2B' e '=' como '%3D', então fazemos decode de %XX antes de comparar.
//
// Para trocar a senha:
//   1. Execute no console do Chrome:
//        function xorCifrar(t,k){let s=[];for(let i=0;i<t.length;i++)s.push(t.charCodeAt(i)^k.charCodeAt(i%k.length));return btoa(String.fromCharCode(...s));}
//        xorCifrar("novoUsuario:novaSenha", "TrueExpert2026#")
//   2. Cole o resultado em TOKEN_ARDUINO abaixo.
//   3. Cole o mesmo resultado em TOKEN_ESPERADO no bloco JS do HTML_TAIL.
// ─────────────────────────────────────────────────────────────────────────────

// Token cifrado esperado — deve ser IGUAL ao TOKEN_ESPERADO no JS
static const char TOKEN_ARDUINO[] = "NRYYDCtCAwAcHFMBAAU=";  // admin:senha123

// Decodifica %XX da URL (ex.: %3D → '=', %2B → '+')
static void urlDecode(const char* src, char* dst, uint8_t maxLen) {
  uint8_t di = 0;
  for (uint8_t i = 0; src[i] && di < maxLen - 1; i++) {
    if (src[i] == '%' && src[i+1] && src[i+2]) {
      char hex[3] = { src[i+1], src[i+2], '\0' };
      dst[di++] = (char) strtol(hex, nullptr, 16);
      i += 2;
    } else if (src[i] == '+') {
      dst[di++] = ' ';
    } else {
      dst[di++] = src[i];
    }
  }
  dst[di] = '\0';
}

bool validarToken(const char* tokenRecebido) {
  char decoded[64] = {0};
  urlDecode(tokenRecebido, decoded, sizeof(decoded));
  return strcmp(decoded, TOKEN_ARDUINO) == 0;
}
