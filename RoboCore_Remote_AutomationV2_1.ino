#include <SPI.h>
#include <Ethernet.h>

byte mac[] = { 
  0x90, 0xA2, 0xDA, 0x00, 0x9B, 0x36 }; //physical mac address
byte ip[] = { 
  192, 168, 1, 99 }; // ip in lan
byte gateway[] = { 
  192, 168, 1, 1 }; // internet access via router
byte subnet[] = { 
  255, 255, 255, 0 }; //subnet mask
EthernetServer server(2846); //server port

String readString;

int pin[8];
char* nome_pin[8];
int estado_pin[8];
int tipo_pin[8];

//////////////////////

void setup(){

  //CONFIGURACAO DAS PORTAS EM QUE OS RELES ESTAO CONECTADOS:
  //Para conectar os reles ao Arduino, recomenda-se utilizar o Arduino Shield - ProtoGo
  //http://www.robocore.net/modules.php?name=GR_LojaVirtual&prod=494
  //Lembre-se tambem que é necessario utilizar um Arduino Ethernet
  //http://www.robocore.net/modules.php?name=GR_LojaVirtual&prod=266
  //OU um Arduino UNO + Shield Ethernet

  pin[0] = 0;
  pin[1] = 1;
  pin[2] = 4;
  pin[3] = 5;
  pin[4] = 6;
  pin[5] = 7;
  pin[6] = 8;
  pin[7] = 9;

  //NOME DOS BOTOES
  nome_pin[0] = "Reservado";
  nome_pin[1] = "Reservado";
  nome_pin[2] = "LED Amarelo";
  nome_pin[3] = "LED Verm. 1";
  nome_pin[4] = "LED Verm. 2";
  nome_pin[5] = "LED Verde";
  nome_pin[6] = "Rele 1";
  nome_pin[7] = "Rele 2";

//TIPO DOS BOTOES 0-> toggle, 1-> pulse
  tipo_pin[0] = 0;
  tipo_pin[1] = 0;
  tipo_pin[2] = 0;
  tipo_pin[3] = 0;
  tipo_pin[4] = 0;
  tipo_pin[5] = 0;
  tipo_pin[6] = 0;
  tipo_pin[7] = 0;

  //ESTADO INICIAL DOS BOTOES 0 -> desligado, 1 -> ligado:
  estado_pin[0] = 0;
  estado_pin[1] = 0;
  estado_pin[2] = 0;
  estado_pin[3] = 0;
  estado_pin[4] = 0;
  estado_pin[5] = 0;
  estado_pin[6] = 0;
  estado_pin[7] = 0;

  pinMode(pin[0], OUTPUT);
  pinMode(pin[1], OUTPUT);
  pinMode(pin[2], OUTPUT);
  pinMode(pin[3], OUTPUT);
  pinMode(pin[4], OUTPUT);
  pinMode(pin[5], OUTPUT);
  pinMode(pin[6], OUTPUT);
  pinMode(pin[7], OUTPUT);



  //start Ethernet
  Ethernet.begin(mac, ip, gateway, subnet);
  server.begin();
  //the pin for the servo co
  //enable serial data print
  Serial.begin(9600);
  Serial.println("Eu Mando na Casa V2.1"); // so I can keep track of what is loaded

}

void loop(){
  // Create a client connection
  EthernetClient client = server.available();
  if (client) {
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();

        //read char by char HTTP request
        if (readString.length() < 100) {

          //store characters to string
          readString += c;
          //Serial.print(c);
        }

        //if HTTP request has ended
        if (c == '\n') {

          ///////////////////// control arduino pin
          Serial.println(readString); //print to serial monitor for debuging

            char pesquisa[] = "?xx";
          for(int i=2 ; i <= 9 ; i++){
            pesquisa[2] = i + 48;

            pesquisa[1] = 'l';
            if(readString.indexOf(pesquisa) > 0){
              if(tipo_pin[i-2] == 1){
                digitalWrite(pin[i-2], HIGH);
                Serial.print("Rele ");
                Serial.print(i);
                Serial.println(" On");
                delay(200);
                digitalWrite(pin[i-2], LOW);
                Serial.print("Rele ");
                Serial.print(i);
                Serial.println(" Off");
              } else {
              digitalWrite(pin[i-2], HIGH);
                Serial.print("Rele ");
                Serial.print(i);
                Serial.println(" On");
              estado_pin[i-2] = 1;
              }
            }

            pesquisa[1] = 'd';
            if(readString.indexOf(pesquisa) > 0){
              digitalWrite(pin[i-2], LOW);
                Serial.print("Rele ");
                Serial.print(i);
                Serial.println(" Off");
              estado_pin[i-2] = 0;
            }

          }



          //clearing string for next read
          readString="";


          ///////////////

          client.println("HTTP/1.1 200 OK"); //send new page
          client.println("Content-Type: text/html");
          client.println();

          client.println("<html>");
          client.println("<head>");
          client.println("<title>Eu Mando na Casa V2.1</title>");
          client.println("<meta http-equiv='Content-Type' content='text/html; charset=ISO-8859-1'>");
          client.println("<link rel='stylesheet' type='text/css' href='http://www.robocore.net/upload/projetos/RemoteAutomationV2.0.css' />");
          client.println("<script type='text/javascript' src='http://www.robocore.net/upload/projetos/RemoteAutomationV2.0.js'/></script>");

          client.println("</head>");
          client.println("<body>");
          client.println("<div id='wrapper'>Eu Mando na Casa V2.1<br>");

          client.println("<div style='display: inline-block; width: 400px;'>");

          for(int i=0;i<=7;i++){
            client.print("<div id='porta");
            client.print(i+2);
            client.print("_estado'>");
            client.print(estado_pin[i]);
            client.println("</div>");
            client.print("<div id='porta");
            client.print(i+2);
            client.print("_titulo'>");
            client.print(nome_pin[i]);
            client.println("</div>");
            client.print("<div id='porta");
            client.print(i+2);
            client.println("_botao' style='position: relative;'></div>");

            if(i==3){
              client.println("</div><div style='display: inline-block; width: 400px;'>");
            }
          }
          client.println("</div>");

          client.println("</div>");

          client.println("<a href='https://garoa.net.br/'><img align='right' src='https://garoa.net.br/ghc/ghc_logo.png' alt='Garoa Hacker Clube'></a>");
          client.println("<a href='https://day.arduino.cc/'><img align='right' src='https://day.arduino.cc/assets/images/LogoArduinoDay_expanded.svg' alt='Arduino Day'></a>");
          client.println("<a href='http://www.centrocultural.sp.gov.br/'><img width='166px' height='40px' align='right' src='http://thalisantunes.com.br/fablivre/logo-ccsp-centro-cultural-sao-paulo.png' alt='CCSP'></a>");

          client.println("</body>");

          client.println("<script>VerificaEstado();</script>");

          client.println("</html>");


          delay(1);
          //stopping client
          client.stop();

        }
      }
    }
  }
}

