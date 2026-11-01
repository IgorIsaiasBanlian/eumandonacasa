/*
 * True eXpert Remote Automation V4.0
 * ─────────────────────────────────────────────────────────────────────────────
 * Servidor web embarcado com controle de dois relés (pinos D8 e D9).
 * Front-end servido diretamente pelo Arduino com suporte a:
 *   - Login com "criptografia simétrica no frontend" (XOR + Base64 no JS/HTML)
 *   - Interface responsiva (mobile / tablet / desktop)
 *   - Tema claro e escuro
 *   - TODOS os comandos exigem o token ("&k=<token>"); só a página de login é pública
 *   - Agendamento diário (ligar/desligar) com hora real via NTP
 *     (fallback: hora do navegador) e agenda salva na EEPROM
 *
 * Hardware alvo : Arduino Uno / Mega + Shield Ethernet W5100
 * Bibliotecas   : Ethernet.h (nativa no IDE), avr/pgmspace.h (nativa)
 * Flash         : o Uno tem 32 KB; este sketch foi enxugado (sem printf, HTML
 *                 minificado). O Serial é opcional: veja DEBUG_SERIAL no topo.
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
#include <EEPROM.h>

// ─── Depuração pelo Monitor Serial ───────────────────────────────────────────
// 1 = mensagens no Monitor Serial (9600 baud); 0 = desligado.
// O Serial + os textos de diagnóstico ocupam cerca de 2 KB dos 32 KB de flash do
// Uno. Se a compilação estourar a flash, mude para 0.
// REGRA: cada linha de log é impressa de uma só vez, sem chamar no meio dela
// nenhuma função que também imprima (senão as linhas se misturam).
#define DEBUG_SERIAL 1
#if DEBUG_SERIAL
  #define DBG_BEGIN()  Serial.begin(9600)
  #define DBG(x)       Serial.print(x)
  #define DBGLN(x)     Serial.println(x)
#else
  #define DBG_BEGIN()
  #define DBG(x)
  #define DBGLN(x)
#endif

// ─── Configuração de rede ────────────────────────────────────────────────────
static const byte MAC_ADDR[] = { 0x90, 0xA2, 0xDA, 0x00, 0x9B, 0x36 };
static const IPAddress IP_ADDR  (192, 168,   1,  99);
static const IPAddress GATEWAY  (192, 168,   1,   1);
static const IPAddress SUBNET   (255, 255, 255,   0);
static const uint16_t  HTTP_PORT = 80;

// ─── Hora real (NTP) ─────────────────────────────────────────────────────────
// O Arduino consulta servidores NTP.br (e um do Google como reserva) por IP,
// sem precisar de DNS. Requer gateway correto para sair para a Internet.
static const uint8_t  NTP_IPS[][4] PROGMEM = {
  { 200, 160,   7, 186 },   // a.st1.ntp.br
  { 200, 160,   0,   8 },   // a.ntp.br
  { 216, 239,  35,   0 }    // time1.google.com
};
static const uint8_t  NTP_QTD            = 3;
static const uint16_t NTP_PORTA_LOCAL    = 8888;
static const uint16_t NTP_TIMEOUT_MS     = 800;            // espera máx. por resposta de cada servidor
static const uint32_t NTP_RESYNC_MS      = 3600000UL;      // re-sincroniza a cada 1 h
static const uint32_t NTP_RETRY_MS       = 60000UL;        // depois das rodadas rápidas: tenta de novo em 60 s
static const uint32_t NTP_RETRY_RAPIDO_MS = 5000UL;        // nas primeiras falhas: tenta de novo em 5 s
static const uint8_t  NTP_RETRY_RAPIDAS  = 3;              // quantas rodadas rápidas antes de passar a 60 s
static const int32_t  FUSO_SEGUNDOS      = -3L * 3600L;    // Brasília (UTC-3, sem horário de verão)

enum FonteHora : uint8_t { FONTE_NENHUMA = 0, FONTE_NAVEGADOR = 1, FONTE_NTP = 2 };

// ─── Agenda na EEPROM ────────────────────────────────────────────────────────
static const uint8_t EEPROM_ENDERECO = 0;
static const uint8_t EEPROM_MAGIC    = 0xA7;   // muda este valor para "zerar" a agenda salva

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

// ═════════════════════════════════════════════════════════════════════════════
// LÓGICA PURA (sem dependência de hardware — testada no PC)
// ═════════════════════════════════════════════════════════════════════════════
// ==BEGIN_LOGICA_PURA==
struct Agenda {
  uint8_t ativo;          // 0 = desativada, 1 = ativa
  uint8_t hLig, mLig;     // horário de ligar
  uint8_t hDes, mDes;     // horário de desligar
};

static int8_t lerNum2(const char* p) {
  if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return -1;
  return (int8_t)((p[0] - '0') * 10 + (p[1] - '0'));
}

static uint16_t minutosDoDia(uint8_t h, uint8_t m) {
  return (uint16_t)h * 60u + m;
}

static bool agendaValida(const Agenda& a) {
  return a.ativo <= 1 && a.hLig < 24 && a.mLig < 60 && a.hDes < 24 && a.mDes < 60 &&
         minutosDoDia(a.hLig, a.mLig) != minutosDoDia(a.hDes, a.mDes);
}

// Formato esperado: "A-HHMM-HHMM"  (ex.: "1-1830-2300")
static bool parseAgenda(const char* p, Agenda& a) {
  if ((p[0] != '0' && p[0] != '1') || p[1] != '-') return false;
  int8_t hl = lerNum2(p + 2);
  int8_t ml = lerNum2(p + 4);
  if (hl < 0 || ml < 0 || p[6] != '-') return false;
  int8_t hd = lerNum2(p + 7);
  int8_t md = lerNum2(p + 9);
  if (hd < 0 || md < 0) return false;
  Agenda t = { (uint8_t)(p[0] - '0'), (uint8_t)hl, (uint8_t)ml, (uint8_t)hd, (uint8_t)md };
  if (!agendaValida(t)) return false;
  a = t;
  return true;
}

// true se "agora" está dentro da janela ligar→desligar (aceita virar a meia-noite)
static bool dentroDaJanela(uint16_t agora, uint16_t lig, uint16_t des) {
  if (lig == des) return false;
  if (lig < des) return agora >= lig && agora < des;
  return agora >= lig || agora < des;
}

// Converte epoch UTC + fuso em hora/minuto/segundo locais
static void dividirHora(uint32_t utc, int32_t fusoSeg, uint8_t& h, uint8_t& m, uint8_t& s) {
  // Trabalha só com o segundo-do-dia (sem estourar nem depender do sinal do fuso)
  int32_t t = (int32_t)(utc % 86400UL) + (fusoSeg % 86400L);
  if (t < 0)      t += 86400L;
  if (t >= 86400L) t -= 86400L;
  uint32_t sd = (uint32_t)t;
  h = (uint8_t)(sd / 3600UL);
  m = (uint8_t)((sd % 3600UL) / 60UL);
  s = (uint8_t)(sd % 60UL);
}

// Lê um inteiro decimal sem sinal (recusa se não houver dígitos ou se estourar 32 bits)
static bool lerUint32(const char* p, uint32_t& v) {
  uint32_t r = 0;
  uint8_t  n = 0;
  while (p[n] >= '0' && p[n] <= '9') {
    if (r > 429496729UL || (r == 429496729UL && p[n] > '5')) return false;
    r = r * 10 + (uint32_t)(p[n] - '0');
    n++;
  }
  if (n == 0) return false;
  v = r;
  return true;
}

static uint8_t valorHex(char c) {
  if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
  if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
  if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
  return 0;
}
// ==END_LOGICA_PURA==

// ─── Estado da hora e da agenda ──────────────────────────────────────────────
struct ConfigAgenda { uint8_t magic; Agenda ag[2]; };   // ag[0] = Quarto, ag[1] = Garagem
static ConfigAgenda cfg;

static uint32_t epochBase           = 0;      // segundos UTC no instante da sincronização
static uint32_t millisBase          = 0;      // millis() no instante da sincronização
static uint8_t  fonteHora           = FONTE_NENHUMA;
static uint32_t ultimaTentativaNTP  = 0;
static uint32_t intervaloNTP        = 0;      // 0 = tenta já na primeira passada do loop
static uint32_t ultimaChecagemAgenda = 0;
static uint16_t ultimoMinuto        = 0xFFFF; // último minuto já avaliado pela agenda
static bool     janelaInicialAplicada = false;
static uint8_t  ntpIndice           = 0;      // 1º servidor a tentar (o último que funcionou)
static uint8_t  falhasNTP           = 0;      // rodadas seguidas sem resposta de nenhum servidor
static EthernetUDP udp;

// ─── Servidor HTTP ───────────────────────────────────────────────────────────
EthernetServer servidor(HTTP_PORT);

// ─────────────────────────────────────────────────────────────────────────────
// HTML/CSS/JS servido em PROGMEM
// Dividido em partes para respeitar o limite de 32 KB por literal PROGMEM.
// O front-end completo está concatenado pelo servidor na função serveHTML().
// ─────────────────────────────────────────────────────────────────────────────

// NOTA: o HTML/CSS/JS abaixo está MINIFICADO (sem comentários/indentação) para
// caber nos 32 KB de flash do Arduino Uno. O fluxo de login/criptografia está
// documentado em validarToken() e no comentário da função xorCifrar abaixo.

// Parte 1 — <head> + CSS
const char HTML_HEAD[] PROGMEM = R"RAWHTML(<!DOCTYPE html><html lang="pt-BR"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Eu Mando na Casa</title> <style>:root{--bg:#f0f4f8;--surface:#fff;--border:#d1d9e0;--txt:#1a2332;--txt2:#4a5568;--accent:#2563eb;--accent-h:#1d4ed8;--on:rgba(37,99,235,.12);--off:rgba(100,116,139,.1);--ind-on:#16a34a;--ind-off:#94a3b8;--shadow:0 2px 12px rgba(0,0,0,.08);--radius:14px;--font:'Inter',system-ui,sans-serif}[data-theme=dark]{--bg:#0f172a;--surface:#1e293b;--border:#334155;--txt:#f1f5f9;--txt2:#94a3b8;--accent:#3b82f6;--accent-h:#2563eb;--on:rgba(59,130,246,.15);--off:rgba(148,163,184,.08);--ind-on:#22c55e;--ind-off:#475569;--shadow:0 2px 16px rgba(0,0,0,.4)}*{box-sizing:border-box;margin:0;padding:0}body{background:var(--bg);color:var(--txt);font-family:var(--font);min-height:100vh;transition:background .3s,color .3s}#login-screen{display:flex;flex-direction:column;align-items:center;justify-content:center;min-height:100vh;padding:20px}.login-card{background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);padding:36px 32px;width:100%;max-width:380px;box-shadow:var(--shadow)}.login-card h1{font-size:1.35rem;font-weight:700;margin-bottom:6px;text-align:center}.login-card p{font-size:.85rem;color:var(--txt2);text-align:center;margin-bottom:28px}.field{display:flex;flex-direction:column;gap:6px;margin-bottom:16px}.field label{font-size:.8rem;font-weight:600;color:var(--txt2);letter-spacing:.04em;text-transform:uppercase}.field input{background:var(--bg);border:1px solid var(--border);border-radius:8px;padding:10px 14px;font-size:.95rem;color:var(--txt);outline:none;transition:border .2s}.field input:focus{border-color:var(--accent)}.btn-login{width:100%;background:var(--accent);color:#fff;border:none;border-radius:8px;padding:12px;font-size:1rem;font-weight:600;cursor:pointer;transition:background .2s,transform .1s}.btn-login:hover{background:var(--accent-h)}.btn-login:active{transform:scale(.98)}.login-err{color:#ef4444;font-size:.82rem;text-align:center;margin-top:10px;min-height:18px}#dashboard{display:none;flex-direction:column;min-height:100vh}header{background:var(--surface);border-bottom:1px solid var(--border);padding:0 20px;height:60px;display:flex;align-items:center;justify-content:space-between;gap:12px;position:sticky;top:0;z-index:10;box-shadow:var(--shadow)}.brand{display:flex;align-items:center;gap:10px;font-weight:700;font-size:1.05rem}.brand-dot{width:10px;height:10px;border-radius:50%;background:var(--accent);animation:pulse 2s infinite}@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}.header-right{display:flex;align-items:center;gap:12px;flex-shrink:0}.btn-icon{background:none;border:1px solid var(--border);border-radius:8px;width:36px;height:36px;cursor:pointer;color:var(--txt2);font-size:1rem;display:flex;align-items:center;justify-content:center;transition:all .2s}.btn-icon:hover{border-color:var(--accent);color:var(--accent)}main{flex:1;padding:24px 20px;max-width:680px;width:100%;margin:0 auto}h2{font-size:1rem;font-weight:600;color:var(--txt2);letter-spacing:.06em;text-transform:uppercase;margin-bottom:16px}.cards{display:grid;gap:14px;grid-template-columns:repeat(auto-fit,minmax(min(100%,260px),1fr))}.card{min-width:0;background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);padding:22px;box-shadow:var(--shadow);transition:border-color .2s}.card.active{border-color:var(--accent)}.card-top{display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:18px}.card-label{font-weight:600;font-size:1rem}.indicator{width:10px;height:10px;border-radius:50%;transition:background .3s}.indicator.on{background:var(--ind-on);box-shadow:0 0 6px var(--ind-on)}.indicator.off{background:var(--ind-off)}.toggle{appearance:none;-webkit-appearance:none;width:48px;height:26px;border-radius:13px;cursor:pointer;background:var(--off);border:1px solid var(--border);position:relative;transition:background .25s,border-color .25s;outline:none}.toggle::after{content:'';position:absolute;top:3px;left:3px;width:18px;height:18px;border-radius:50%;background:#fff;box-shadow:0 1px 3px rgba(0,0,0,.2);transition:transform .25s}.toggle:checked{background:var(--accent);border-color:var(--accent)}.toggle:checked::after{transform:translateX(22px)}.status-text{font-size:.78rem;color:var(--txt2);margin-top:4px}footer{padding:16px 20px;text-align:center;font-size:.75rem;color:var(--txt2);border-top:1px solid var(--border)}.relogio{font-size:.85rem;font-weight:600;color:var(--txt2);font-variant-numeric:tabular-nums}.relogio.aviso{color:#ef4444}.agenda{margin-top:18px;padding-top:16px;border-top:1px solid var(--border)}.ag-head{display:flex;justify-content:space-between;align-items:center;margin-bottom:12px;font-size:.78rem;font-weight:600;color:var(--txt2);text-transform:uppercase;letter-spacing:.05em}.ag-times{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:10px;margin-bottom:12px}.ag-times label{display:flex;flex-direction:column;gap:4px;min-width:0;font-size:.75rem;color:var(--txt2)}.ag-times input{background:var(--bg);border:1px solid var(--border);border-radius:8px;padding:8px;font-size:.95rem;color:var(--txt);font-family:inherit;min-width:0;width:100%}[data-theme=dark] .ag-times input{color-scheme:dark}.btn-save{width:100%;background:var(--accent);color:#fff;border:none;border-radius:8px;padding:9px;font-size:.9rem;font-weight:600;cursor:pointer;transition:background .2s}.btn-save:hover{background:var(--accent-h)}.ag-msg{font-size:.75rem;color:var(--txt2);margin-top:6px;min-height:16px;text-align:center}@media(max-width:380px){header{padding:0 14px}main{padding:20px 14px}.card{padding:18px}}</style> </head> )RAWHTML";

// Parte 2 — <body> + JS
const char HTML_BODY[] PROGMEM = R"RAWHTML(<body data-theme="light"><div id="login-screen"><div class="login-card"><h1>&#128274; Automação</h1><p>Eu Mando na Casa &mdash; v4.0</p><div class="field"><label for="inp-user">Usuário</label><input id="inp-user" type="text" autocomplete="username" placeholder="seu usuário" onkeydown="enterLogin(event)"></div><div class="field"><label for="inp-pass">Senha</label><input id="inp-pass" type="password" autocomplete="current-password" placeholder="sua senha" onkeydown="enterLogin(event)"></div><button class="btn-login" onclick="tentarLogin()">Entrar</button><div class="login-err" id="login-err"></div></div></div><div id="dashboard"><header><div class="brand"><div class="brand-dot"></div> Eu Mando na Casa </div><div class="header-right"><span class="relogio aviso" id="relogio" title="Hora do Arduino">--:--</span><button class="btn-icon" id="btn-theme" onclick="toggleTheme()" title="Alternar tema">&#9788;</button><button class="btn-icon" onclick="logout()" title="Sair">&#10148;</button></div></header><main><h2>Dispositivos</h2><div class="cards"><div class="card" id="card2"><div class="card-top"><div><div class="card-label">)RAWHTML";

// Parte 3 — Labels dinâmicos e JS (gerados em runtime, não em PROGMEM)
// Veja serveHTML() abaixo.

const char HTML_CARD_MID[] PROGMEM = R"RAWHTML(</div><div class="status-text" id="st2"></div></div><div class="indicator" id="ind2"></div></div><input type="checkbox" class="toggle" id="tog2" onchange="acionar(2,this.checked)"></div><div class="card" id="card3"><div class="card-top"><div><div class="card-label">)RAWHTML";

const char HTML_TAIL[] PROGMEM = R"RAWHTML(</div><div class="status-text" id="st3"></div></div><div class="indicator" id="ind3"></div></div><input type="checkbox" class="toggle" id="tog3" onchange="acionar(3,this.checked)"></div></div></main><footer>Eu Mando na Casa &mdash; True eXpert Automação Residencial</footer></div> <script>
const CHAVE_XOR = "TrueExpert2026#";
function xorCifrar(texto, chave) {
let saida = [];
for (let i = 0; i < texto.length; i++) {
saida.push(texto.charCodeAt(i) ^ chave.charCodeAt(i % chave.length));
}
return btoa(String.fromCharCode(...saida));
}
const SESSION_KEY = "rc_auth";
let autenticado = false;
let tokenSessao = null;
function entrarNoDashboard(tk) {
tokenSessao = tk;
autenticado = true;
document.getElementById('login-screen').style.display = 'none';
document.getElementById('dashboard').style.display = 'flex';
atualizarEstado();
}
function tentarLogin() {
const u = document.getElementById('inp-user').value.trim();
const p = document.getElementById('inp-pass').value;
const token = xorCifrar(u + ':' + p, CHAVE_XOR);
fetch('/?auth=' + encodeURIComponent(token))
.then(r => {
if (r.ok) {
try { sessionStorage.setItem(SESSION_KEY, token); } catch(e) {}
mostrarErro('');
entrarNoDashboard(token);
} else {
mostrarErro('Usuário ou senha incorretos.');
}
})
.catch(() => mostrarErro('Não foi possível conectar ao dispositivo.'));
}
function enterLogin(e) { if (e.key === 'Enter') tentarLogin(); }
function mostrarErro(msg) { document.getElementById('login-err').textContent = msg; }
function logout() {
autenticado = false;
tokenSessao = null;
try { sessionStorage.removeItem(SESSION_KEY); } catch(e) {}
document.getElementById('login-screen').style.display = 'flex';
document.getElementById('dashboard').style.display = 'none';
document.getElementById('inp-pass').value = '';
}
(function restaurarSessao() {
let token = null;
try { token = sessionStorage.getItem(SESSION_KEY); } catch(e) {}
if (token) {
entrarNoDashboard(token);
}
})();
function api(caminho) {
return fetch(caminho + '&k=' + encodeURIComponent(tokenSessao)).then(r => {
if (r.status === 401) {
logout();
mostrarErro('Sessão recusada pelo dispositivo. Faça login novamente.');
throw new Error('401');
}
return r;
});
}
function acionar(porta, ligar) {
if (!autenticado) return;
const cmd = ligar ? 'l' : 'd';
api('/?' + cmd + porta)
.then(r => r.text())
.then(atualizarEstadoRaw)
.catch(console.error);
}
function atualizarEstado() {
api('/?status')
.then(r => r.text())
.then(atualizarEstadoRaw)
.catch(console.error);
}
let agendaCarregada = false;
let ultimoEnvioHora = 0;
function montarAgendas() {
[2, 3].forEach(p => {
const d = document.createElement('div');
d.className = 'agenda';
d.innerHTML =
'<div class="ag-head"><span>Agendamento</span>' +
'<input type="checkbox" class="toggle" id="ag' + p + 'a"></div>' +
'<div class="ag-times">' +
'<label>Ligar<input type="time" id="ag' + p + 'l" value="18:00"></label>' +
'<label>Desligar<input type="time" id="ag' + p + 'd" value="23:00"></label></div>' +
'<button class="btn-save" onclick="salvarAgenda(' + p + ')">Salvar</button>' +
'<div class="ag-msg" id="ag' + p + 'm"></div>';
document.getElementById('card' + p).appendChild(d);
});
}
montarAgendas();
function preencherAgenda(p, m) {
document.getElementById('ag' + p + 'a').checked = (m[1] === '1');
document.getElementById('ag' + p + 'l').value = m[2] + ':' + m[3];
document.getElementById('ag' + p + 'd').value = m[4] + ':' + m[5];
}
function salvarAgenda(p) {
if (!autenticado) return;
const at = document.getElementById('ag' + p + 'a').checked ? '1' : '0';
const l = document.getElementById('ag' + p + 'l').value.replace(':', '');
const d = document.getElementById('ag' + p + 'd').value.replace(':', '');
const msg = document.getElementById('ag' + p + 'm');
if (l.length !== 4 || d.length !== 4) { msg.textContent = 'Preencha os dois horários.'; return; }
api('/?s' + p + '=' + at + '-' + l + '-' + d)
.then(r => { msg.textContent = r.ok ? 'Salvo \u2713' : 'Horários inválidos (ligar e desligar não podem ser iguais).'; })
.catch(() => { msg.textContent = 'Falha de conexão.'; })
.finally(() => setTimeout(() => { msg.textContent = ''; }, 4000));
}
function atualizarRelogio(fonte, hora) {
const el = document.getElementById('relogio');
if (fonte === 0) { el.textContent = 'sem hora'; el.classList.add('aviso'); }
else { el.textContent = hora.slice(0, 5); el.classList.remove('aviso'); }
el.title = ['Hora NÃO sincronizada', 'Hora do navegador (sem NTP)', 'Hora sincronizada via NTP'][fonte];
if (fonte !== 2 && Date.now() - ultimoEnvioHora > 3600000) {
ultimoEnvioHora = Date.now();
api('/?ts=' + Math.floor(Date.now() / 1000))
.then(r => r.text()).then(atualizarEstadoRaw).catch(() => {});
}
}
function atualizarEstadoRaw(texto) {
const m = texto.match(/E2:(\d),E3:(\d)/);
if (!m) return;
atualizarCard(2, m[1] === '1');
atualizarCard(3, m[2] === '1');
const t = texto.match(/T=(\d\d:\d\d:\d\d)/);
const f = texto.match(/F=(\d)/);
if (t && f) atualizarRelogio(+f[1], t[1]);
if (!agendaCarregada) {
const re = /-?(\d)-(\d\d)(\d\d)-(\d\d)(\d\d)/;
const a2 = texto.match(/A2=(\d-\d{4}-\d{4})/);
const a3 = texto.match(/A3=(\d-\d{4}-\d{4})/);
if (a2 && a3) {
preencherAgenda(2, a2[1].match(re));
preencherAgenda(3, a3[1].match(re));
agendaCarregada = true;
}
}
}
function atualizarCard(porta, ligado) {
const tog = document.getElementById('tog' + porta);
const ind = document.getElementById('ind' + porta);
const card = document.getElementById('card' + porta);
const st = document.getElementById('st' + porta);
tog.checked = ligado;
ind.className = 'indicator ' + (ligado ? 'on' : 'off');
card.className = 'card' + (ligado ? ' active' : '');
st.textContent = ligado ? 'Ligado' : 'Desligado';
}
function toggleTheme() {
const novoTema = document.body.getAttribute('data-theme') === 'dark' ? 'light' : 'dark';
document.body.setAttribute('data-theme', novoTema);
document.getElementById('btn-theme').textContent = novoTema === 'dark' ? '☀' : '☾';
try { sessionStorage.setItem('tema', novoTema); } catch(e) {}
}
(function restaurarTema() {
let t = null;
try { t = sessionStorage.getItem('tema'); } catch(e) {}
if (t) {
document.body.setAttribute('data-theme', t);
document.getElementById('btn-theme').textContent = t === 'dark' ? '☀' : '☾';
}
})();
setInterval(() => { if (autenticado) atualizarEstado(); }, 5000);
</script> </body></html> )RAWHTML";

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
void    tarefasPeriodicas();
bool    requisicaoAutorizada();
void    tratarAgenda(EthernetClient& cliente, uint8_t idx, const char* chave);
void    tratarHoraNavegador(EthernetClient& cliente);
void    comandarRele(uint8_t idx, bool ligar);
void    carregarConfig();
void    gravarConfig();
void    definirHora(uint32_t utc, uint8_t fonte);
uint32_t agoraUTC();
bool    sincronizarNTP();
void    verificarAgenda();
void    aplicarJanelaInicial(uint16_t minutoDia);

// ─────────────────────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  DBG_BEGIN();

  // Configura pinos dos relés como saída e garante estado desligado
  pinMode(PIN_RELE_2, OUTPUT);
  pinMode(PIN_RELE_3, OUTPUT);
  digitalWrite(PIN_RELE_2, LOW);
  digitalWrite(PIN_RELE_3, LOW);

  // Inicia Ethernet com IP fixo. Assinatura: begin(mac, ip, DNS, GATEWAY, subnet).
  // (A versão de 4 argumentos tratava o 4º como gateway e deixava o Arduino
  //  sem rota para a Internet. O roteador também faz o papel de DNS.)
  Ethernet.begin(const_cast<byte*>(MAC_ADDR), IP_ADDR, GATEWAY, GATEWAY, SUBNET);
  delay(1000);   // tempo para o shield inicializar

  servidor.begin();

  carregarConfig();   // agenda salva na EEPROM

  DBG(F("Servidor iniciado em http://"));
  DBG(IP_ADDR); DBG(':'); DBGLN(HTTP_PORT);
  DBG(F("Rele 2 (D8 - Quarto)   modo: "));
  DBGLN(MODO_RELE_2 == MODO_PULSE ? F("PULSE") : F("TOGGLE"));
  DBG(F("Rele 3 (D9 - Garagem)  modo: "));
  DBGLN(MODO_RELE_3 == MODO_PULSE ? F("PULSE") : F("TOGGLE"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Loop principal
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  tarefasPeriodicas();   // sincronização de hora + execução da agenda

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

  // ── Página inicial (sem "?"): pública, precisa carregar para exibir o login ──
  if (!contemStr(reqBuf, "?")) {
    serveHTML(cliente);
    return;
  }

  // ── A PARTIR DAQUI, toda rota exige o token em "&k=<token>" ──
  if (!requisicaoAutorizada()) {
    enviarHTTP(cliente, 401, "text/plain", "Unauthorized");
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

  // ── Rotas: gravar agenda do Quarto (s2) / Garagem (s3) ──
  if (contemStr(reqBuf, "?s2=")) { tratarAgenda(cliente, 0, "?s2="); return; }
  if (contemStr(reqBuf, "?s3=")) { tratarAgenda(cliente, 1, "?s3="); return; }

  // ── Rota: acertar hora pelo navegador (só vale se NTP não estiver ativo) ──
  if (contemStr(reqBuf, "?ts=")) { tratarHoraNavegador(cliente); return; }

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
      DBG(F("Rele pino "));
      DBG(pino);
      DBGLN(F(" pulso ON"));
      delay(PULSE_MS);
      digitalWrite(pino, LOW);
      DBG(F("Rele pino "));
      DBG(pino);
      DBGLN(F(" pulso OFF"));
    }
    estado = false;   // sempre retorna como desligado após o pulso
  } else {
    // Modo toggle normal
    estado = ligar;
    digitalWrite(pino, ligar ? HIGH : LOW);
    DBG(F("Rele pino "));
    DBG(pino);
    DBGLN(ligar ? F(" ON") : F(" OFF"));
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
// Helpers de formatação (evitam puxar o printf do avr-libc, que é grande)
static char* apStr(char* p, PGM_P s) {            // acrescenta texto da flash
  char c;
  while ((c = (char)pgm_read_byte(s++)) != '\0') *p++ = c;
  return p;
}
static char* apNum2(char* p, uint8_t v) {         // acrescenta 2 dígitos (00–99)
  *p++ = (char)('0' + v / 10);
  *p++ = (char)('0' + v % 10);
  return p;
}
static char* apAgenda(char* p, const Agenda& a) { // "A-HHMM-HHMM"
  *p++ = (char)('0' + a.ativo);
  *p++ = '-';
  p = apNum2(p, a.hLig);  p = apNum2(p, a.mLig);
  *p++ = '-';
  p = apNum2(p, a.hDes);  p = apNum2(p, a.mDes);
  return p;
}

void serveEstado(EthernetClient& cliente) {
  char corpo[64];
  uint8_t h = 0, m = 0, s = 0;
  if (fonteHora != FONTE_NENHUMA) dividirHora(agoraUTC(), FUSO_SEGUNDOS, h, m, s);

  // Formato: E2:X,E3:Y;T=HH:MM:SS;F=n;A2=a-HHMM-HHMM;A3=a-HHMM-HHMM
  char* p = corpo;
  p = apStr(p, PSTR("E2:"));    *p++ = estadoRele2 ? '1' : '0';
  p = apStr(p, PSTR(",E3:"));   *p++ = estadoRele3 ? '1' : '0';
  p = apStr(p, PSTR(";T="));    p = apNum2(p, h); *p++ = ':'; p = apNum2(p, m); *p++ = ':'; p = apNum2(p, s);
  p = apStr(p, PSTR(";F="));    *p++ = (char)('0' + fonteHora);
  p = apStr(p, PSTR(";A2="));   p = apAgenda(p, cfg.ag[0]);
  p = apStr(p, PSTR(";A3="));   p = apAgenda(p, cfg.ag[1]);
  *p = '\0';

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
//   2. Cole o resultado em TOKEN_ARDUINO abaixo. É o ÚNICO lugar: o token não
//      existe mais na página, quem confere a senha é só o Arduino.
// ─────────────────────────────────────────────────────────────────────────────

// Token cifrado esperado (usuário:senha). Nunca é enviado ao navegador.
static const char TOKEN_ARDUINO[] = "NRYYDCtCAwAcHFMBAAU=";  // admin:senha123

// Decodifica %XX da URL (ex.: %3D → '=', %2B → '+')
static void urlDecode(const char* src, char* dst, uint8_t maxLen) {
  uint8_t di = 0;
  for (uint8_t i = 0; src[i] && di < maxLen - 1; i++) {
    if (src[i] == '%' && src[i+1] && src[i+2]) {
      dst[di++] = (char)((valorHex(src[i+1]) << 4) | valorHex(src[i+2]));
      i += 2;
    } else if (src[i] == '+') {
      dst[di++] = ' ';
    } else {
      dst[di++] = src[i];
    }
  }
  dst[di] = '\0';
}

// Comparação em tempo constante: não revela, pelo tempo de resposta, quantos
// caracteres do token estavam certos.
static bool igualConstante(const char* a, const char* b) {
  size_t la = strlen(a), lb = strlen(b);
  uint8_t diff = (la != lb) ? 1 : 0;
  for (size_t i = 0; i < lb; i++) {
    char ca = (i < la) ? a[i] : 0;
    diff |= (uint8_t)(ca ^ b[i]);
  }
  return diff == 0;
}

bool validarToken(const char* tokenRecebido) {
  char decoded[64] = {0};
  urlDecode(tokenRecebido, decoded, sizeof(decoded));
  return igualConstante(decoded, TOKEN_ARDUINO);
}

// Lê "&k=<token>" da requisição e valida. Sem token ou token errado → false.
bool requisicaoAutorizada() {
  char token[64] = {0};
  extrairParam(reqBuf, "&k=", token, sizeof(token));
  return validarToken(token);
}

// ═════════════════════════════════════════════════════════════════════════════
// HORA REAL + AGENDAMENTO
// ═════════════════════════════════════════════════════════════════════════════

// Segundos UTC atuais, calculados a partir do último acerto + millis().
// Como re-sincronizamos de hora em hora, o estouro do millis() (49 dias) não afeta.
uint32_t agoraUTC() {
  return epochBase + (millis() - millisBase) / 1000UL;
}

// Registra a hora recebida (NTP ou navegador). Na 1ª vez, aplica a janela da agenda.
void definirHora(uint32_t utc, uint8_t fonte) {
  epochBase  = utc;
  millisBase = millis();
  fonteHora  = fonte;

  if (!janelaInicialAplicada) {
    janelaInicialAplicada = true;
    uint8_t h, m, s;
    dividirHora(utc, FUSO_SEGUNDOS, h, m, s);
    aplicarJanelaInicial(minutosDoDia(h, m));
  }
}

#if DEBUG_SERIAL
// Linha de log do NTP completa, impressa de uma vez: "NTP <ip>: <mensagem>"
static void dbgNTP(const IPAddress& ip, const __FlashStringHelper* msg) {
  DBG(F("NTP ")); DBG(ip); DBG(F(": ")); DBGLN(msg);
}
#define DBG_NTP(ip, msg) dbgNTP(ip, msg)
#else
#define DBG_NTP(ip, msg)
#endif

// Pergunta a hora a UM servidor NTP (índice da tabela NTP_IPS). true = hora acertada.
static bool consultarNTP(uint8_t idx) {
  IPAddress alvo(pgm_read_byte(&NTP_IPS[idx][0]), pgm_read_byte(&NTP_IPS[idx][1]),
                 pgm_read_byte(&NTP_IPS[idx][2]), pgm_read_byte(&NTP_IPS[idx][3]));

  uint8_t pkt[48];
  memset(pkt, 0, sizeof(pkt));
  pkt[0] = 0x23;                       // LI=0, versão 4, modo 3 (cliente)

  uint32_t t0 = millis();

  if (!udp.begin(NTP_PORTA_LOCAL)) {   // sem socket livre agora
    DBG_NTP(alvo, F("sem socket livre"));
    return false;
  }
  udp.beginPacket(alvo, 123);
  udp.write(pkt, sizeof(pkt));
  udp.endPacket();

  bool recebeu = false;
  while (millis() - t0 < NTP_TIMEOUT_MS) {
    if (udp.parsePacket() >= 48) {
      udp.read(pkt, sizeof(pkt));
      recebeu = true;
      break;
    }
    delay(10);
  }
  udp.stop();

  if (!recebeu) {
    DBG_NTP(alvo, F("sem resposta"));
    return false;
  }
  // Valida: modo servidor, relógio do servidor sincronizado, stratum != 0
  if ((pkt[0] & 0x07) != 4 || (pkt[0] >> 6) == 3 || pkt[1] == 0) {
    DBG_NTP(alvo, F("resposta invalida"));
    return false;
  }

  uint32_t seg1900 = ((uint32_t)pkt[40] << 24) | ((uint32_t)pkt[41] << 16) |
                     ((uint32_t)pkt[42] << 8)  |  (uint32_t)pkt[43];
  const uint32_t DIFERENCA_1900_1970 = 2208988800UL;
  if (seg1900 < DIFERENCA_1900_1970) {
    DBG_NTP(alvo, F("hora invalida"));
    return false;
  }

  uint32_t utc = seg1900 - DIFERENCA_1900_1970;

  // Imprime a linha COMPLETA antes de aplicar a hora: definirHora() pode disparar a
  // agenda (que também imprime), e assim as mensagens saem em ordem e sem se misturar.
#if DEBUG_SERIAL
  {
    uint8_t h, m, s;
    dividirHora(utc, FUSO_SEGUNDOS, h, m, s);
    DBG(F("NTP ")); DBG(alvo); DBG(F(": OK em ")); DBG(millis() - t0); DBG(F(" ms - hora local "));
    if (h < 10) DBG('0');
    DBG(h); DBG(':');
    if (m < 10) DBG('0');
    DBG(m); DBG(':');
    if (s < 10) DBG('0');
    DBGLN(s);
  }
#endif

  definirHora(utc, FONTE_NTP);
  return true;
}

// Percorre a lista de servidores até um responder. Começa pelo último que funcionou.
bool sincronizarNTP() {
  for (uint8_t i = 0; i < NTP_QTD; i++) {
    uint8_t idx = (ntpIndice + i) % NTP_QTD;
    if (consultarNTP(idx)) {
      ntpIndice = idx;
      return true;
    }
  }
  return false;
}

// Chamada a cada volta do loop(): mantém a hora em dia e executa a agenda
void tarefasPeriodicas() {
  uint32_t agora = millis();
  if (agora - ultimaTentativaNTP >= intervaloNTP) {
    ultimaTentativaNTP = agora;
    if (sincronizarNTP()) {
      falhasNTP    = 0;
      intervaloNTP = NTP_RESYNC_MS;                  // tudo certo: confere de novo em 1 h
    } else {
      if (falhasNTP < 255) falhasNTP++;
      // Nas primeiras falhas insiste a cada 5 s; depois, a cada 60 s
      intervaloNTP = (falhasNTP <= NTP_RETRY_RAPIDAS) ? NTP_RETRY_RAPIDO_MS : NTP_RETRY_MS;
      DBG(F("NTP: nenhum servidor respondeu; nova rodada em "));
      DBG(intervaloNTP / 1000UL); DBGLN(F(" s"));
    }
  }
  verificarAgenda();
}

// Aciona o relé de índice 0 (Quarto) ou 1 (Garagem)
void comandarRele(uint8_t idx, bool ligar) {
  if (idx == 0) acionarRele(PIN_RELE_2, MODO_RELE_2, ligar, estadoRele2);
  else          acionarRele(PIN_RELE_3, MODO_RELE_3, ligar, estadoRele3);
}

// Dispara ligar/desligar quando o minuto atual coincide com o horário agendado.
// Só avalia 1x por minuto, então você ainda pode mudar manualmente entre um horário e outro.
void verificarAgenda() {
  if (fonteHora == FONTE_NENHUMA) return;

  uint32_t agora = millis();
  if (agora - ultimaChecagemAgenda < 1000UL) return;
  ultimaChecagemAgenda = agora;

  uint8_t h, m, s;
  dividirHora(agoraUTC(), FUSO_SEGUNDOS, h, m, s);
  uint16_t minutoDia = minutosDoDia(h, m);
  if (minutoDia == ultimoMinuto) return;
  ultimoMinuto = minutoDia;

  for (uint8_t i = 0; i < 2; i++) {
    const Agenda& a = cfg.ag[i];
    if (!a.ativo) continue;
    if (minutoDia == minutosDoDia(a.hLig, a.mLig)) {
      DBG(F("Agenda: ligar rele ")); DBGLN(i + 2);
      comandarRele(i, true);
    } else if (minutoDia == minutosDoDia(a.hDes, a.mDes)) {
      DBG(F("Agenda: desligar rele ")); DBGLN(i + 2);
      comandarRele(i, false);
    }
  }
}

// Após queda de energia/reinício: se agora está dentro de uma janela ativa, liga o relé.
void aplicarJanelaInicial(uint16_t minutoDia) {
  for (uint8_t i = 0; i < 2; i++) {
    const Agenda& a = cfg.ag[i];
    if (!a.ativo) continue;
    ModoRele modo = (i == 0) ? MODO_RELE_2 : MODO_RELE_3;
    if (modo == MODO_PULSE) continue;   // relé de pulso nunca é "mantido ligado"
    if (dentroDaJanela(minutoDia, minutosDoDia(a.hLig, a.mLig), minutosDoDia(a.hDes, a.mDes))) {
      DBG(F("Agenda: retomando rele ")); DBGLN(i + 2);
      comandarRele(i, true);
    }
  }
}

// ─── EEPROM ──────────────────────────────────────────────────────────────────
void carregarConfig() {
  EEPROM.get(EEPROM_ENDERECO, cfg);
  bool ok = (cfg.magic == EEPROM_MAGIC) && agendaValida(cfg.ag[0]) && agendaValida(cfg.ag[1]);
  if (!ok) {
    // Primeira execução (ou dados inválidos): agendas desativadas, 18:00 → 23:00
    cfg.magic = EEPROM_MAGIC;
    for (uint8_t i = 0; i < 2; i++) {
      cfg.ag[i].ativo = 0;
      cfg.ag[i].hLig = 18; cfg.ag[i].mLig = 0;
      cfg.ag[i].hDes = 23; cfg.ag[i].mDes = 0;
    }
    gravarConfig();
  }
}

// EEPROM.put() só escreve os bytes que mudaram, preservando a vida útil da memória
void gravarConfig() {
  EEPROM.put(EEPROM_ENDERECO, cfg);
}

// ─── Handlers HTTP ───────────────────────────────────────────────────────────
// Ex.: GET /?s2=1-1830-2300  → Quarto ativo, liga 18:30, desliga 23:00
void tratarAgenda(EthernetClient& cliente, uint8_t idx, const char* chave) {
  const char* p = strstr(reqBuf, chave);
  Agenda nova;
  if (p && parseAgenda(p + strlen(chave), nova)) {
    cfg.ag[idx] = nova;
    gravarConfig();
    DBG(F("Agenda gravada: rele ")); DBGLN(idx + 2);
    enviarHTTP(cliente, 200, "text/plain", "OK");
  } else {
    enviarHTTP(cliente, 400, "text/plain", "ERRO");
  }
}

// Ex.: GET /?ts=1790000000  → epoch UTC do navegador. Ignorado se o NTP já acertou a hora.
void tratarHoraNavegador(EthernetClient& cliente) {
  const char* p = strstr(reqBuf, "?ts=");
  if (p && fonteHora != FONTE_NTP) {
    uint32_t ts;
    if (lerUint32(p + 4, ts) && ts > 1704067200UL) {   // > 01/01/2024: descarta valores absurdos
      definirHora(ts, FONTE_NAVEGADOR);
    }
  }
  serveEstado(cliente);
}
