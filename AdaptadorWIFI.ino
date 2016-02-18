
#define ESPERANDO_FLANCO_ASCENDENTE_CLK 0
#define ESPERANDO_FLANCO_DESCENDENTE_CLK 1
#define MAX_MEMORIA_BOTON 20
#include <aJSON.h>


#define req  14 //mic REQ line goes to pin 5 through q1 (arduino high pulls request line low)
#define dat  4 //mic Data line goes to pin 2
#define clk 13 //mic Clock line goes to pin 3
#define boton 12 

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WebSocketsServer.h>

const char *ssid = "amariszi";
const char *password = "pipaypipo";
bool no_avise_conexion = true;

//ESP8266WebServer server ( 80 );
WebSocketsServer webSocket = WebSocketsServer(81, "", "mensaje_vortex");

int i = 0; 

int i_ultimo_bit_digito_instrumento = 0; 
int v_ultimo_digito_instrumento = 0;
int i_ultimo_digito_instrumento = 0;
int c_bits_desde_encabezado = 0;
bool descartar_medicion = false;

int estado_espera_clk = ESPERANDO_FLANCO_ASCENDENTE_CLK;
int cantidad_unos_seguidos = 0;
byte data_instrumento[9];


String ultima_lectura = "";
String lectura_anterior = "";
String lectura_para_informar = "";
//String mensaje = "";
String unidad_ultima_lectura = "";

int ms_ultima_medicion_tr = 0;

int ultimos_valores_boton[MAX_MEMORIA_BOTON];
int ultimo_valor_firme_boton = 1;
bool apreto_boton = false;

//void handleRoot() { 
//  server.send ( 200, "text/html", "root" );
//}


aJsonObject* mensaje_medicion_tr;

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) {

    switch(type) {
        case WStype_DISCONNECTED:
            Serial.printf("[%u] Disconnected!\n", num);
            break;
        case WStype_CONNECTED:
            {
                IPAddress ip = webSocket.remoteIP(num);
                Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        
                // send message to client
                //webSocket.sendTXT(num, "Connected");
            }
            break;
        case WStype_TEXT:
            Serial.printf("[%u] get Text: %s\n", num, payload);

            // send message to client
            // webSocket.sendTXT(num, "message here");

            // send data to all connected clients
            // webSocket.broadcastTXT("message here");
            break;
        case WStype_BIN:
            Serial.printf("[%u] get binary lenght: %u\n", num, lenght);
            //hexdump(payload, lenght);

            // send message to client
            // webSocket.sendBIN(num, payload, lenght);
            break;
    }
    
}

void setup(void){
  Serial.begin(115200);  
  pinMode(req, OUTPUT);
  pinMode(clk, INPUT);
  pinMode(dat, INPUT);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("adaptador_sime_111");

  IPAddress myIP = WiFi.softAPIP();
  Serial.println("AP IP address: ");
  Serial.println(myIP);
  
  WiFi.begin ( ssid, password );
  Serial.println ( "" );

  
  if ( MDNS.begin ( "esp8266" ) ) {
    Serial.println ( "MDNS responder started" );
  }

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println ( "WSockets server started" );

}

void leerBitInstrumento(void);
void verSiApretoBoton(void);

void loop(void){
  webSocket.loop();
  if(no_avise_conexion){
    if(WiFi.status() == WL_CONNECTED){
      Serial.println ( "Conectado a AP con IP: " );
      Serial.println ( WiFi.localIP() );
      no_avise_conexion = false;
    }
  }
	leerBitInstrumento();
  verSiApretoBoton();
 //informo memoria libre
	if((millis()-ms_ultima_medicion_tr)>5000){
		Serial.println(ESP.getFreeHeap());
		ms_ultima_medicion_tr = millis();
	}
 yield();
}

void verSiApretoBoton(){
  for(i=1;i<MAX_MEMORIA_BOTON; i++){
    ultimos_valores_boton[i-1] = ultimos_valores_boton[i];
  }
  ultimos_valores_boton[MAX_MEMORIA_BOTON-1] = digitalRead(boton);
  bool todos_cero = true;
  for(i=0;i<MAX_MEMORIA_BOTON; i++){
    if(ultimos_valores_boton[i] == 1) todos_cero = false;
  }
  if(todos_cero){
    if(ultimo_valor_firme_boton == 1){
      ultimo_valor_firme_boton = 0;
      apreto_boton = true;
      aJsonObject* mensaje_medicion = aJson.createObject();
      aJson.addStringToObject(mensaje_medicion, "tipoDeMensaje", "medicion");
      aJson.addStringToObject(mensaje_medicion, "valor", &ultima_lectura[0]);
      aJson.addStringToObject(mensaje_medicion, "unidad", &unidad_ultima_lectura[0]);
      
      //mensaje = "{\"tipoDeMensaje\":\"medicionTiempoReal\", \"valor\":\"" + ultima_lectura + "\", \"unidad\":\"" + unidad_ultima_lectura + "\"}";
      char *mensaje_json = aJson.print(mensaje_medicion);
      webSocket.broadcastTXT(mensaje_json);
      free(mensaje_json);
      aJson.deleteItem(mensaje_medicion);
  
      Serial.println("apreto boton");
    }
  }
  else{
    bool todos_uno = true;
    for(i=0;i<MAX_MEMORIA_BOTON; i++){
      if(ultimos_valores_boton[i] == 0) todos_uno = false;
    }
    if(todos_uno){
      if(ultimo_valor_firme_boton == 0){
        ultimo_valor_firme_boton = 1;
      }
    }
  }
}

void leerBitInstrumento(void){
	
	int lectura_clk = digitalRead(clk);
    if(estado_espera_clk == ESPERANDO_FLANCO_ASCENDENTE_CLK && lectura_clk==HIGH) estado_espera_clk = ESPERANDO_FLANCO_DESCENDENTE_CLK;
    if(estado_espera_clk == ESPERANDO_FLANCO_DESCENDENTE_CLK && lectura_clk==LOW){
      estado_espera_clk = ESPERANDO_FLANCO_ASCENDENTE_CLK;
      int bit_leido = digitalRead(dat);
      if(bit_leido == 1) cantidad_unos_seguidos++;
      //DETECTO ENCABEZADO    
      if(cantidad_unos_seguidos >= 16){ //RECIBIENDO ENCABEZADO
        i_ultimo_bit_digito_instrumento = 0;
        v_ultimo_digito_instrumento = 0;
        i_ultimo_digito_instrumento = 0;
        c_bits_desde_encabezado = 0;
        descartar_medicion = false;
        
        if(bit_leido == 0){
          c_bits_desde_encabezado++;
        }
      }
      if(bit_leido == 0) cantidad_unos_seguidos = 0;
      if(c_bits_desde_encabezado>0 && c_bits_desde_encabezado<=36){
        c_bits_desde_encabezado++;
        //Serial.print(bit_leido);
  
        bitWrite(v_ultimo_digito_instrumento, i_ultimo_bit_digito_instrumento, (bit_leido & 0x1));
        //Serial.print(bit_leido);
        i_ultimo_bit_digito_instrumento++;
        if(i_ultimo_bit_digito_instrumento>3){
          i_ultimo_bit_digito_instrumento = 0;
          data_instrumento[i_ultimo_digito_instrumento] = v_ultimo_digito_instrumento;
          v_ultimo_digito_instrumento = 0;
          i_ultimo_digito_instrumento++;
        }
        if(i_ultimo_digito_instrumento==9){ //TERMINE DE RECIBIR DATO
          ultima_lectura = "";
          int signo = data_instrumento[0]; 
          int decimal = data_instrumento[7];
          int unidad = data_instrumento[8];
          int i_string = 0;
          if(signo == 8) {
            ultima_lectura = '-';
          }
          if(signo != 8 && signo != 0) descartar_medicion = true;
          bool no_mas_ceros_izquierda= false;
          for(int i_buf=0;i_buf<6;i_buf++)  {
            if((data_instrumento[i_buf+1]!=0|| no_mas_ceros_izquierda) || (i_buf+decimal)>4){
              ultima_lectura+= data_instrumento[i_buf+1];
              no_mas_ceros_izquierda=true;
              if(data_instrumento[i_buf+1] > 9) descartar_medicion = true;
            }
            if((i_buf+decimal)==5) {
              ultima_lectura += '.';
            }
          }
          //ultima_lectura += ' ';
          if(unidad>1){
            descartar_medicion = true;
          }
          else if(unidad==0){
            unidad_ultima_lectura = "mm";
          }else if(unidad==1){
            unidad_ultima_lectura = "in";
          }
            
          
          if(descartar_medicion) ultima_lectura = lectura_anterior;
          if(ultima_lectura!=lectura_anterior) {
            Serial.println(ultima_lectura);
            aJsonObject* mensaje_medicion_tr = aJson.createObject();
            aJson.addStringToObject(mensaje_medicion_tr, "tipoDeMensaje", "medicionTiempoReal");
            aJson.addStringToObject(mensaje_medicion_tr, "valor", &ultima_lectura[0]);
            aJson.addStringToObject(mensaje_medicion_tr, "unidad", &unidad_ultima_lectura[0]);
            
            //mensaje = "{\"tipoDeMensaje\":\"medicionTiempoReal\", \"valor\":\"" + ultima_lectura + "\", \"unidad\":\"" + unidad_ultima_lectura + "\"}";
            char *mensaje_json = aJson.print(mensaje_medicion_tr);
            webSocket.broadcastTXT(mensaje_json);
            free(mensaje_json);
            aJson.deleteItem(mensaje_medicion_tr);

            lectura_anterior = ultima_lectura;
          } 
        }
      }
    }
}




