
#include <Wire.h>
#include "MAX30105.h"//Librería del sensor https://github.com/sparkfun/SparkFun_MAX3010x_Sensor_Library
#include "spo2_algorithm.h"//algoritmo de cálculo de spo2
#include "lcdgfx.h"//Librería de la Oled https://github.com/lexus2k/lcdgfx


DisplaySSD1306_128x64_I2C display(-1);//Objeto Oled
MAX30105 SensorPulso;//Objeto sensor de pulso

#define MAX_BRIGHTNESS 255 //Definimos el brillo del LED
#define boton D3//Pin del botón de reseteo
#define botonLectura D4//Botón que usaremos para comenzar la toma de datos



uint32_t irBuffer[100]; //aquí guardaremos los datos del led infrarrojo
uint32_t redBuffer[100];  //aquí guardaremos los datos del led 


int32_t bufferLength; //Longitud del buffer
int32_t spo2; //valor de saturación de oxigeno
int32_t heartRate; //valor de ritmo cardiaco
//Las siguientes 2 variables solo las usaremos para depuración por puerto serial. Si la Oled no funciona, pero el sensor si, lo podremos saber por serial
int8_t validSPO2; //nos indica si es válida la lectura de spo2
int8_t validHeartRate; //nos indica si es válida la lectura de ritmo cardiaco

bool x=false;//bandera que usaremos para refrescar la pantalla solo una vez en caso de que haya que cambiar el texto impreso
bool error=false;//bandera que usaremos para saber si se ha dado un error de lectura
bool unaVez=true;//bandera que usaremos para refrescar la pantalla solo una vez en caso de que haya que cambiar el texto impreso



unsigned long FreqCardiacaMax=250;//valor máximo de frecuencia cardiaca que será considerado válido y no un error de lectura
unsigned long Spo2Max=100;//valor máximo de satuaración de oxígeno que será considerado válido y no un error de lectura

int EstadoBotonReset =1;//Apagado
int EstadoBotonLectura =1;//Apagado

//Función que usaremos para controlar el reseteo por botón
void ControlReset(){
    EstadoBotonReset=digitalRead(boton);
    if(EstadoBotonReset=!EstadoBotonReset){
      ESP.reset();
    }
}

//Función que usaremos para controlar la toma de datos mediante el pulsado de un botón
void ControlLectura(){
  EstadoBotonLectura=digitalRead(botonLectura);
  if(EstadoBotonLectura!=EstadoBotonLectura){
    EstadoBotonLectura=0;
  }
}

//Función para escribir texto por display
void escribir_texto(byte x_pos, byte y_pos, char *text, byte text_size) {
  display.setFixedFont(ssd1306xled_font6x8);///Seleccionamos una fuente
  if (text_size == 1) {
    display.printFixed (x_pos,  y_pos, text, STYLE_NORMAL);
  }
  else if (text_size == 2) {
    display.printFixedN (x_pos, y_pos, text, STYLE_NORMAL, FONT_SIZE_2X);
  }
}

void setup()
{
  Serial.begin(115200);//Iniciamos el puerto serie (línea de depuración)
  
  //Definimos los pines de los botones
  pinMode(boton, INPUT);
  pinMode(botonLectura, INPUT);
  
  display.begin();//inciamos el display
  display.fill(0x00);//Limpiamos el display
  escribir_texto(0,0, "Coloque el ",2);
  escribir_texto(0,16,"   dedo", 2);
  escribir_texto(0,32, " por favor", 2);

  //Inicializamos el sensor de pulso
  if (!SensorPulso.begin(Wire, I2C_SPEED_FAST)) //usa el puerto I2C por defecto, 400kHz 
  {
    Serial.println(F("No se ha encontrado el sensor. Revise las conexiones, por favor"));
    while (1);
  }
  
  

  Serial.println(F("Introduzca el dedo en el dispositivo"));


  byte ledBrightness = 60; //brillo del led. opciones: 0=apagado hasta 255=50mA
  byte sampleAverage = 4; //promedio de muestras. opciones: 1, 2, 4, 8, 16, 32
  byte ledMode = 2; //modo del led. opciones: 1 = rojo solo, 2 = rojo + IR, 3 = rojo + IR + verde(solo disponible en los max30105
  byte sampleRate = 100; //frecuencia de muestreo. opciones: 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411; //ancho de pulso. opciones: 69, 118, 215, 411
  int adcRange = 4096; //rango del adc. opciones: 2048, 4096, 8192, 16384

  SensorPulso.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange); //configuramos el sensor con las opciones elegidas
}

void loop()
{
  ControlLectura();//Esperamos que se haya pulsado el botón (indicativo de que el dispositivo está ya colocado en el dedo)
  if(EstadoBotonLectura==0){//Cuando se pulsa..
    
  bufferLength = 100; //el buffer de longitud 100 almacena 4 segundos de muestras que se ejecutan a 25 muestras por segundo

  //Leemos las primeras 100 muestras y determina el rango de la señal.
  for (byte i = 0 ; i < bufferLength ; i++)
  {
    while (SensorPulso.available() == false) //mientras no tengamos un dato nuevo..
      SensorPulso.check(); //seguimos checkeando el sensor

    redBuffer[i] = SensorPulso.getRed();//Tomamos el valor leido del led rojo y lo almacenamos en el buffer correspondiente
    irBuffer[i] = SensorPulso.getIR();//tomamos el valor leido del led infrarrojo y lo almacenamos en el buffer correspondiente
    SensorPulso.nextSample(); //una vez almacenado, pasamos a la siguiente muestra

    //Mostramos los valores leidos del led rojo e infrarrojo. Solo para depurar
    Serial.print(F("red="));
    Serial.print(redBuffer[i], DEC);
    Serial.print(F(", ir="));
    Serial.println(irBuffer[i], DEC);

    ControlReset();//llamamos a la función que controla que si mientras estamos tomando muestras se pulsa el botón de reset, se resetea el micro
    
    if(!x){//Imprimimos una sola vez por pantalla que estamos procesando las lecturas
      display.fill(0x00);
      escribir_texto(0,0, "Procesando...", 1);
      x=true;
    }
  }
  
  //Calculamos la frecuencia cardiaca y la saturación de oxigeno una vez que tenemos las primeras 100 muestras (4 segundos de muestras)
  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);

  //A partir de este momento,  tomamos muestras continuamente del sensor. La frecuencia cardiaca y la saturación de oxígeno se calculan cada segundo
  while (1)
  {
    
    if(x){//Limpiamos una vez la pantalla
      display.fill(0x00);
      x=false;
    }
    //desplazamos los datos 25 a 100 a las posiciones 0 a 75 correspondientemente 
    for (byte i = 25; i < 100; i++)
    {
      redBuffer[i - 25] = redBuffer[i];
      irBuffer[i - 25] = irBuffer[i];
    }

    //tomamos las 25 últimas muestras antes de calcular la frecuencia cardiaca y la saturación de oxigeno
    for (byte i = 75; i < 100; i++)
    {
      ControlReset();//En todo momento estamos atentos por si se pulsa el botón de reset
      
      while (SensorPulso.available() == false) //mientras no tengamos una nueva muestra..
        SensorPulso.check(); //..seguimos comprobando el sensor

      redBuffer[i] = SensorPulso.getRed();//Guardamos las muestras en el buffer correspondiente
      irBuffer[i] = SensorPulso.getIR();//Guardamos las muestras en el buffer correspondiente
      SensorPulso.nextSample(); //una vez almacenados vamos a por la siguiente muestra

      //mandamos los valores de los LED´s y de la frecuencia cardiaca y de la saturación de oxigeno por puerto serial a modo de depuración
      Serial.print(F("red="));
      Serial.print(redBuffer[i], DEC);
      Serial.print(F(", ir="));
      Serial.print(irBuffer[i], DEC);

      Serial.print(F(", HR="));
      Serial.print(heartRate, DEC);

      Serial.print(F(", HRvalid="));
      Serial.print(validHeartRate, DEC);

      Serial.print(F(", SPO2="));
      Serial.print(spo2, DEC);

      Serial.print(F(", SPO2Valid="));
      Serial.println(validSPO2, DEC);

      //Vamos a convertir los valores de frecuencia y saturación (int) y los vamos a convertir a char. De esta manera podremos trabajar con la función escribir_texto()
      char hr[20];
      sprintf(hr, "%lu", heartRate);
      char Spo2[20];
      sprintf(Spo2, "%lu", spo2);
      
      //Comprobamos que la frecuencia y la saturación no superen los límites que le hemos marcado
      if(heartRate<=FreqCardiacaMax&&spo2<Spo2Max){
       if(error){//Si provenimos de imprimir un mensaje de error..
        display.fill(0x00);//..limpiamos la pantalla
        error=false;//bajamos la bandera de que venimos de un error
        unaVez=true;//Y levantamos la bandera de que ya hemos limpiado una vez la pantalla
       }
       //Escribimos los valores de frecuencia y saturación en la pantalla oled
       escribir_texto(0,0, "Freq. card. ",2);
       escribir_texto(40,20,hr,2);        
       escribir_texto(0,33, "Saturacion", 2);
       escribir_texto(40,48, Spo2,2);
      } 

      //Si los valores de frecuencia y saturación estan por encima de los límites que hemos establecidos
      else if(heartRate>FreqCardiacaMax||spo2>=Spo2Max){
        if(unaVez){//comprobamos si provenimos de mostrar el mensaje de error o de mostrar los datos
          display.fill(0x00);//Si venimos de mostrar los datos, hay que limpiar la pantalla antes de escribir el mensaje de error
          unaVez=false;//Bajamos la bandera          
        }

        escribir_texto(0,16," ERROR  DE ",2); 
        escribir_texto(0,40,"  LECTURA",2);
        error=true;
      }
    }

    //Tras haber guardado las 25 últimas muestras, recalculamos la frecuencia cardiaca y la saturación de oxigeno
    maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);
  }
}
}