
// модицикация програмы для опроса цифрового и аналогового входов и выдачи импульса на насос
// кнопки опрашиваются через прерывания

// замеченные недоделки и неудобства
// 1. если находишься внутри параметра, то все равно остается "невидимая" возможность переключаться между параметрами (изменять значение переменной menu)
//    лучше это запретить
// 2. После перезагрузки контроллера он должен попадать в режим часов (или демонтрации какого-то из значений), а после нажатия на "enter" - в режим ввода логина и пароля


/*  0b  0 0 0 0 0 0 0 0   */
/*      ^ ^ ^ ^ ^ ^ ^ ^   */
/*     dp G F E D C B A   */

/*      A                 */
/*     ---                */
/*  F |   | B             */
/*    | G |               */    
/*     ---                */
/*  E |   | C             */
/*    |   |               */
/*     ---  . dp          */
/*      D                 */

#include <TM1637Display.h>
#include <EEPROM.h>

#define BUTTON_PERIOD 400  // замедление опроса клавиш во избежание дребезга
unsigned long button_time = 0;  //время последнего опроса кнопок
/*
#define CLK 25  // Пин подключения CLK к TM1637
#define DIO 17  // Пин подключения DIO к TM1637
//определение пинов для кнопок
#define BUTTON_UP 36
#define BUTTON_DOWN 39
#define BUTTON_LEFT 34
#define BUTTON_RIGHT 35
#define BUTTON_ENTER 32
*/

#define CLK 25  // Пин подключения CLK к TM1637
#define DIO 33  // Пин подключения DIO к TM1637
//определение пинов для кнопок для макета
#define BUTTON_UP 5
#define BUTTON_DOWN 18
#define BUTTON_LEFT 16
#define BUTTON_RIGHT 17
#define BUTTON_ENTER 19

#define PULSE_PIN 0     // пин импульсного входа расходомера
#define LEVEL_SENSOR_PIN 0     // пин датчика уровня реактива
#define SOLENOID_PIN 27
// пин на управление соленоидом
#define ANALOG_INPUT_PIN 26     // пин аналогового входа расходомера

TM1637Display display(CLK, DIO);
                                                

const uint8_t P[] = {0b01110011};
const uint8_t U[] = {0b00111110};
const uint8_t t[] = {0b01111000};
const uint8_t O[] = {0b00111111};
const uint8_t N[] = {0b01010100};
const uint8_t F[] = {0b01110001};
const uint8_t dp[] = {0b10000000}; // точка
const uint8_t num[] = {
    0b00111111, // "0"
    0b00000110, // "1"
    0b01011011, // "2"
    0b01001111, // "3"
    0b01100110, // "4"
    0b01101101, // "5"
    0b01111101, // "6"
    0b00000111, // "7"
    0b01111111, // "8"
    0b01101111  // "9"
};


// Переменные для хранения параметров
bool stateAfterPowerOn = false; //состояние после включения питания
bool useLevelSensor = false; //Использование датчика уровня реагента
int operationMode = 0;  //Задание режима работы
bool inputTypeAuto = false; //выбор типа входа для автоматического режима: цифровой (false) или аналоговый (true) 
int maxPulsesPerMinute = 0; //Максимальное число импульсов в минуту
float dosagePercentageInteger = 0;  //Коэффициент пропорциональности дозирования целая часть (в %)  // должно быть float, а unsigned int
float dosagePercentageFractional = 0; //Коэффициент пропорциональности дозирования дробная часть (в %) // должно быть float, а unsigned int
float correctionCoefficient = 0.0;  //поправочный коэффициент
float pumpFlowRate = 0.1; //значение объемной производительности насоса при максимальном числе импульсов, л/час
float reagentDensity = 1.2; //значение плотности реагента, в кг/м3
float transferredVolume = 0;  //Объем перекачанного реагента (с момента последнего сброса) , л
float transferredMass = 0;  //масса перекачанного реагента (с момента последнего сброса) , кг
bool resetValues = false; //Сброс значения объема/массы перекачанного реагента
unsigned long pumpImpulsesSinceStartup = 5634;  //Число импульсов насоса с момента программирования системы управления (не сбрасывается) // записать в память и оттуда считывать!
unsigned long readPumpImpulses = 0; //Число импульсов насоса с момента программирования системы управления (не сбрасывается) из еепром  
bool changeOperatorPassword = false;  //Изменение пароля оператора
bool remoteAccessBlocked = false; //Блокировка удаленного доступа технолога
bool dosageRepresentationInMLPerM3 = false; //Вид представления дозировки реактива мг/л (false) или мл/м3 (true)
int pulseDuration = 300;  //Длительность импульса в мс
float waterPulseVolume = 5;  //Вес импульса расходомера в литрах реактива
float waterPulseMass = 5;  //Вес импульса расходомера в литрах реактива
float reagentPulseVolume = 0.001;  //Вес импульс насоса в литрах реактива
float reagentPulseMass = 0.001;  //Вес импульс насоса в килограммах реактива

bool reactive_level_low = false; // низкий уровень реактива
float waterPulseWeight = 0; // цена импульса в литрах воды 

float pumpVolumeForLoading = 20; //Объем реактива, закачиваемый насосом в режиме «закачка», в литрах
float pumpMassForLoading = 24;  //масса реактива, закачиваемая насосом в режиме «закачка», в килограммах
int menu = 1;  //номер текущего пункта меню
bool Enter_flag = true; //флаг кнопки ввод


long pulse_number_to_threat = 0; // число импульсов, которые должен закачать насос
float reagentVolume_to_go = 0; // объем реагента, который надо перекачать
float reagentMass_to_go = 0; // масса реагента, которую надо перекачать

// Объявление переменных для генерации импульсов
unsigned long previousMillis = 0; // Время предыдущего изменения состояния пина
const long interval = 50; // Интервал импульса в миллисекундах
bool pulseState = 0; // Состояние импульса

//флаги состояний кнопок
bool volatile buttonState_Up = false;  
bool volatile buttonState_Down = false;  
bool volatile buttonState_Left = false;    
bool volatile buttonState_Right = false;       
bool volatile buttonState_Enter = false;       

bool volatile button_pressed = true;  // флаг нажатия какой-то кнопки
bool button_delay = false; // флаг таймера замедления опроса кнопок
bool pulse_flag = false;



// программирование прерываний по входам
// прерывания кнопок
void IRAM_ATTR Up_fun(void);
void IRAM_ATTR Down_fun(void);
void IRAM_ATTR Left_fun(void);
void IRAM_ATTR Right_fun(void);
void IRAM_ATTR Enter_fun(void);

void IRAM_ATTR Level_fun(void); // функция контроля уровня реактива 
void IRAM_ATTR Pulse_fun(void); // функция определения импульса от расходомера 



void setup() {

  Serial.begin(115200);

  // Инициализация EEPROM
  EEPROM.begin(100);

  
  display.setBrightness(0x0f); // Установка яркости (от 0x00 до 0x0f)

//режимы работы входов и выходов
  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_LEFT, INPUT_PULLUP);
  pinMode(BUTTON_RIGHT, INPUT_PULLUP);
  pinMode(BUTTON_ENTER, INPUT_PULLUP);
  
//  pinMode(12, INPUT_PULLUP); //кнопка соленоида(мембраны насоса)
 


  
//  pinMode(PULSE_PIN, INPUT);
//  pinMode(LEVEL_SENSOR_PIN, INPUT);
  pinMode(SOLENOID_PIN, OUTPUT);
  
// инициализация прерываний для кнопок
attachInterrupt(BUTTON_UP, Up_fun, FALLING);
attachInterrupt(BUTTON_DOWN, Down_fun, FALLING);
attachInterrupt(BUTTON_LEFT, Left_fun, FALLING);
attachInterrupt(BUTTON_RIGHT, Right_fun, FALLING);
attachInterrupt(BUTTON_ENTER, Enter_fun, FALLING);

attachInterrupt(LEVEL_SENSOR_PIN, Level_fun, FALLING); // инициализация прерывания входа датчика уровня
attachInterrupt(PULSE_PIN, Pulse_fun, FALLING); // инициализация прерывания импульсного входя расходомера


// инициализация часов

}

void loop() {

  /*if (!digitalRead(12)){
      digitalWrite(SOLENOID_PIN, 1);
    } else {
     digitalWrite(SOLENOID_PIN, 0); 
    }*/

  EEPROM.put(0, pumpImpulsesSinceStartup);
  EEPROM.commit();  

  if(button_pressed && !button_delay) { // проверка, что любая кнопка нажата, взвод таймера задержки
   button_time = millis();
   button_delay = true;
  }

  if(millis()-button_time > BUTTON_PERIOD){
    button_delay = false;
  }
  
  if (buttonState_Enter) {  // проверка на нажатие кнопки Enter
      Enter_flag = !Enter_flag;
      buttonState_Enter = false;
  }
  
  if (Enter_flag){
      if (buttonState_Up) {
          menu--;
          //button_pressed = false;
          buttonState_Up = false;        
      }
      if (buttonState_Down) {
          menu++;
          //button_pressed = false;
          buttonState_Down = false;          
      }
  }
                                        //Serial.println(menu);
  if (button_pressed){
    button_pressed = false;
    switch(menu){
      case 0:  // корректировка номера меню, не должен быть меньше 1
        menu = 1;
      break;

      case 1:
      if (Enter_flag){
      display.clear();
      display.setSegments(P, 1, 0);
      display.setSegments(num + 0, 1, 1);
      display.setSegments(num + 1, 1, 2); 
        } else {
              if (buttonState_Right){stateAfterPowerOn = 1; buttonState_Right = false;}
              if (buttonState_Left){stateAfterPowerOn = 0; buttonState_Left = false;}
          display.clear();
          display.showNumberDec(stateAfterPowerOn);
      }
      Serial.print(" stateAfterPowerOn=");
      Serial.print(stateAfterPowerOn);
      Serial.println(" //состояние после включения питания");
    break;

      case 2:
      if (Enter_flag){
      display.clear();
      display.setSegments(P, 1, 0);
      display.setSegments(num + 0, 1, 1);
      display.setSegments(num + 2, 1, 2); 
        } else {
              if (buttonState_Right){useLevelSensor = 1; buttonState_Right = false;}
              if (buttonState_Left){useLevelSensor = 0; buttonState_Left = false;}
          display.clear();
          display.showNumberDec(useLevelSensor);
        }
        Serial.print(" useLevelSensor=");
        Serial.print(useLevelSensor);
        Serial.println(" //Использование датчика уровня реагента");
    break;

        case 3:
        if (Enter_flag){
            display.clear();
            display.setSegments(P, 1, 0);
            display.setSegments(num + 0, 1, 1);
            display.setSegments(num + 3, 1, 2); 
        } else {
              if (buttonState_Right){operationMode++; buttonState_Right = false;}
              if (buttonState_Left){operationMode--; buttonState_Left = false;}
              if (operationMode <= 0){operationMode = 0;}
              if (operationMode >= 3){operationMode = 3;}
              display.clear();
              display.showNumberDec(operationMode);
          }
        Serial.print(" operationMode=");
        Serial.print(operationMode);
        Serial.println(" //Задание режима работы");
    break;

        case 4:
        if (Enter_flag){
        display.clear();
        display.setSegments(P, 1, 0);
        display.setSegments(num + 0, 1, 1);
        display.setSegments(num + 4, 1, 2); 
        } else {
              if (buttonState_Right){inputTypeAuto = 1; buttonState_Right = false;}
              if (buttonState_Left){inputTypeAuto = 0; buttonState_Left = false;}
              display.clear();
              display.showNumberDec(inputTypeAuto);
          }
        Serial.print(" inputTypeAuto=");
        Serial.print(inputTypeAuto);
        Serial.println(" //выбор типа входа для автоматического режима");
    break;

        case 5:
        if (Enter_flag){
        display.clear();
        display.setSegments(P, 1, 0);
        display.setSegments(num + 0, 1, 1);
        display.setSegments(num + 5, 1, 2); 
        
        } else {
              if (buttonState_Right){maxPulsesPerMinute++; buttonState_Right = false;}
              if (buttonState_Left){maxPulsesPerMinute--; buttonState_Left = false;}
              if (maxPulsesPerMinute <= 0){maxPulsesPerMinute = 0;}
              if (maxPulsesPerMinute >= 250){maxPulsesPerMinute = 250;}
              display.clear();
              display.showNumberDec(maxPulsesPerMinute);
          }
        Serial.print(" maxPulsesPerMinute=");
        Serial.print(maxPulsesPerMinute);
        Serial.println(" //Максимальное число импульсов в минуту");
    break;

        case 6:
        if (Enter_flag){
        display.clear();
        display.setSegments(P, 1, 0);
        display.setSegments(num + 0, 1, 1);
        display.setSegments(num + 6, 1, 2); 
        
        } else {
              if (buttonState_Right){dosagePercentageInteger++; buttonState_Right = false;}
              if (buttonState_Left){dosagePercentageInteger--; buttonState_Left = false;}
              if (dosagePercentageInteger <= 0){dosagePercentageInteger = 0;}
              if (dosagePercentageInteger >= 100){dosagePercentageInteger = 100;}
                  int integerPart_dosagePercentageInteger = static_cast<int>(dosagePercentageInteger);
                display.clear();
                display.showNumberDec(integerPart_dosagePercentageInteger);   
          }
        Serial.print(" dosagePercentageInteger=");
        Serial.print(dosagePercentageInteger);
        Serial.println(" //Коэффициент пропорциональности дозирования целая часть");
    break;

        case 7:
        if (Enter_flag){
        display.clear();
        display.setSegments(P, 1, 0);
        display.setSegments(num + 0, 1, 1);
        display.setSegments(num + 7, 1, 2); 
        
        } else {
              if (buttonState_Right){dosagePercentageFractional = dosagePercentageFractional + 0.001; buttonState_Right = false;}
              if (buttonState_Left){dosagePercentageFractional = dosagePercentageFractional - 0.001; buttonState_Left = false;}
              if (dosagePercentageFractional <= 0){dosagePercentageFractional = 0;}
              if (dosagePercentageFractional >= 0.999){dosagePercentageFractional = 0.999;}
                  int integerPart_dosagePercentageFractional = static_cast<int>(dosagePercentageFractional);
                  int decimalPart_dosagePercentageFractional = static_cast<int>((dosagePercentageFractional - integerPart_dosagePercentageFractional) * 1000);
              display.clear();
              display.showNumberDec(decimalPart_dosagePercentageFractional, true, 4, 0);     
          }
          
        Serial.print(" dosagePercentageFractional=");
        Serial.print(dosagePercentageFractional, 3);
        Serial.println(" //Коэффициент пропорциональности дозирования дробная часть");
    break;

        case 8:
        if (Enter_flag){
        display.clear();
        display.setSegments(P, 1, 0);
        display.setSegments(num + 0, 1, 1);
        display.setSegments(num + 8, 1, 2); 
        
        } else {
              if (buttonState_Right){correctionCoefficient = correctionCoefficient + 0.1; buttonState_Right = false;}
              if (buttonState_Left){correctionCoefficient = correctionCoefficient - 0.1; buttonState_Left = false;}
              if (correctionCoefficient <= 0.1){correctionCoefficient = 0.1;}
              if (correctionCoefficient >= 9.9){correctionCoefficient = 9.9;}
              int integerPart_correctionCoefficient = static_cast<int>(correctionCoefficient);
              int decimalPart_correctionCoefficient = static_cast<int>((correctionCoefficient - integerPart_correctionCoefficient) * 10);
              display.clear();
              display.showNumberDec(integerPart_correctionCoefficient, false, 1, 2);
              display.showNumberDec(decimalPart_correctionCoefficient, false, 1, 3);  
          }
        Serial.print(" correctionCoefficient=");
        Serial.print(correctionCoefficient);
        Serial.println(" //поправочный коэффициент ");
    break;

        case 9:
        if (Enter_flag){
        display.clear();
        display.setSegments(P, 1, 0);
        display.setSegments(num + 0, 1, 1);
        display.setSegments(num + 9, 1, 2); 
        
        } else {
              if (buttonState_Right){pumpFlowRate = pumpFlowRate + 0.1; buttonState_Right = false;}
              if (buttonState_Left){pumpFlowRate = pumpFlowRate - 0.1; buttonState_Left = false;}
              if (pumpFlowRate <= 0.1){pumpFlowRate = 0.1;}
              if (pumpFlowRate >= 99.9){pumpFlowRate = 99.9;}
                  int integerPart_pumpFlowRate = static_cast<int>(pumpFlowRate);
                  int decimalPart_pumpFlowRate = static_cast<int>((pumpFlowRate - integerPart_pumpFlowRate) * 10);
                  if (pumpFlowRate <= 10){
                      display.clear();
                      display.showNumberDec(integerPart_pumpFlowRate, false, 1, 2);
                      display.showNumberDec(decimalPart_pumpFlowRate, false, 1, 3);
                  } else {
                    display.clear();
                    display.showNumberDec(integerPart_pumpFlowRate / 10, false, 1, 1);
                    display.showNumberDec(integerPart_pumpFlowRate % 10, false, 1, 2);
                    display.showNumberDec(decimalPart_pumpFlowRate, false, 1, 3);
                  }
          }
        Serial.print(" pumpFlowRate=");
        Serial.print(pumpFlowRate);
        Serial.println(" //значение объемной производительности насоса при максимальном числе импульсов, л/час");
    break;

        case 10:
        if (Enter_flag){
        display.clear();
        display.setSegments(P, 1, 0);
        display.setSegments(num + 1, 1, 1);
        display.setSegments(num + 0, 1, 2); 
        
        } else {
              if (buttonState_Right){reagentDensity = reagentDensity + 0.1; buttonState_Right = false;}
              if (buttonState_Left){reagentDensity = reagentDensity - 0.1; buttonState_Left = false;}
              if (reagentDensity <= 0.5){reagentDensity = 0.5;}
              if (reagentDensity >= 3){reagentDensity = 3;}
                  int integerPart_reagentDensity = static_cast<int>(reagentDensity);
                  int decimalPart_reagentDensity = static_cast<int>((reagentDensity - integerPart_reagentDensity) * 10);
              display.clear();
              display.showNumberDec(integerPart_reagentDensity, false, 1, 2);
              display.showNumberDec(decimalPart_reagentDensity, false, 1, 3);  
                  
          }
        Serial.print(" reagentDensity=");
        Serial.print(reagentDensity);
        Serial.println(" //значение плотности реагента, в кг/м3");
    break;
        case 11:
         if (Enter_flag){
         display.clear();
         display.setSegments(P, 1, 0);
         display.setSegments(num + 1, 1, 1);
         display.setSegments(num + 1, 1, 2); 
          
        } else {
                  int integerPart_transferredVolume = static_cast<int>(transferredVolume);
                  int decimalPart_transferredVolume = static_cast<int>((transferredVolume - integerPart_transferredVolume) * 100);
              display.clear();
              display.showNumberDec(integerPart_transferredVolume / 10, false, 1, 0);
              display.showNumberDec(integerPart_transferredVolume % 10, false, 1, 1);  
              display.showNumberDec(decimalPart_transferredVolume / 10, false, 1, 2);  
              display.showNumberDec(decimalPart_transferredVolume % 10, false, 1, 3);  
          }
        Serial.print(" transferredVolume=");
        Serial.print(transferredVolume);
        Serial.println(" //Объем перекачанного реагента (с момента последнего сброса) , л");
    break;

        case 12:
         if (Enter_flag){
         display.clear();
         display.setSegments(P, 1, 0);
         display.setSegments(num + 1, 1, 1);
         display.setSegments(num + 2, 1, 2); 
        
        } else {
                  int integerPart_transferredMass = static_cast<int>(transferredMass);
                  int decimalPart_transferredMass = static_cast<int>((transferredMass - integerPart_transferredMass) * 10);
              display.clear();
              display.showNumberDec(integerPart_transferredMass / 100, false, 1, 0);
              display.showNumberDec((integerPart_transferredMass % 100) / 10, false, 1, 1);  
              display.showNumberDec(integerPart_transferredMass % 10, false, 1, 2);  
              display.showNumberDec(decimalPart_transferredMass, false, 1, 3);  
                  
          }
        Serial.print(" transferredMass=");
        Serial.print(transferredMass);
        Serial.println(" //масса перекачанного реагента (с момента последнего сброса) , кг");
    break;

        case 13:
        if (Enter_flag){
         display.clear();
         display.setSegments(P, 1, 0);
         display.setSegments(num + 1, 1, 1);
         display.setSegments(num + 3, 1, 2); 
         
        } else {
              if (buttonState_Right){resetValues = 1; buttonState_Right = false;}
              if (buttonState_Left){resetValues = 0; buttonState_Left = false;}
        display.clear();
        display.showNumberDec(resetValues);        
        }
        Serial.print(" resetValues=");
        Serial.print(resetValues);
        Serial.println(" //Сброс значения объема/массы перекачанного реагента");
    break;

        case 14:
        if (Enter_flag){
         display.clear();
         display.setSegments(P, 1, 0);
         display.setSegments(num + 1, 1, 1);
         display.setSegments(num + 4, 1, 2); 
         
        } else {
        EEPROM.get(0, readPumpImpulses); 
        display.clear();
        display.showNumberDec(readPumpImpulses);        
       }
        Serial.print(" pumpImpulsesSinceStartup=");
        Serial.print(readPumpImpulses);
        Serial.println(" //Число импульсов насоса с момента программирования системы управления (не сбрасывается)");
    break;

        case 15:
        if (Enter_flag){
         display.clear();
         display.setSegments(P, 1, 0);
         display.setSegments(num + 1, 1, 1);
         display.setSegments(num + 5, 1, 2); 
        
        } else {
              if (buttonState_Right){changeOperatorPassword = 1; buttonState_Right = false;}
              if (buttonState_Left){changeOperatorPassword = 0; buttonState_Left = false;}
        display.clear();
        display.showNumberDec(changeOperatorPassword);
        }
        
        Serial.print(" changeOperatorPassword=");
        Serial.print(changeOperatorPassword);
        Serial.println(" //Изменение пароля оператора");
    break;

        case 16:
        if (Enter_flag){
         display.clear();
         display.setSegments(P, 1, 0);
         display.setSegments(num + 1, 1, 1);
         display.setSegments(num + 6, 1, 2); 
        
        } else {
              if (buttonState_Right){remoteAccessBlocked = 1; buttonState_Right = false;}
              if (buttonState_Left){remoteAccessBlocked = 0; buttonState_Left = false;}
        display.clear();
        display.showNumberDec(remoteAccessBlocked);
        }
        
        Serial.print(" remoteAccessBlocked=");
        Serial.print(remoteAccessBlocked);
        Serial.println(" //Блокировка удаленного доступа технолога");
    break;

        case 17:
        if (Enter_flag){
         display.clear();
         display.setSegments(P, 1, 0);
         display.setSegments(num + 1, 1, 1);
         display.setSegments(num + 7, 1, 2); 
        
        } else {
              if (buttonState_Right){dosageRepresentationInMLPerM3 = 1; buttonState_Right = false;}
              if (buttonState_Left){dosageRepresentationInMLPerM3 = 0; buttonState_Left = false;}
        display.clear();
        display.showNumberDec(dosageRepresentationInMLPerM3);
        }
        
        Serial.print(" dosageRepresentationInMLPerM3=");
        Serial.print(dosageRepresentationInMLPerM3);
        Serial.println(" //Вид представления дозировки реактива мг/л или мл/м3");
    break;

        case 18:
        if (Enter_flag){
         display.clear();
         display.setSegments(P, 1, 0);
         display.setSegments(num + 1, 1, 1);
         display.setSegments(num + 8, 1, 2); 
        
        } else {
              if (buttonState_Right){pulseDuration++; buttonState_Right = false;}
              if (buttonState_Left){pulseDuration--; buttonState_Left = false;}
              if (pulseDuration <= 0){pulseDuration = 0;}
              if (pulseDuration >= 1000){pulseDuration = 1000;}
        display.clear();
        display.showNumberDec(pulseDuration);
        }
        Serial.print(" pulseDuration=");
        Serial.print(pulseDuration);
        Serial.println(" //Длительность импульса в мс");
    break;

        case 19:
        if (Enter_flag){
         display.clear();
         display.setSegments(P, 1, 0);
         display.setSegments(num + 1, 1, 1);
         display.setSegments(num + 9, 1, 2); 
        
        } else {
              if (buttonState_Right){waterPulseWeight = waterPulseWeight + 1; buttonState_Right = false;}
              if (buttonState_Left){waterPulseWeight = waterPulseWeight - 1; buttonState_Left = false;}
              if (waterPulseWeight <= 0){waterPulseWeight = 0;}
              if (waterPulseWeight >= 9999){waterPulseWeight = 9999;}
                  int integerPart_waterPulseWeight = static_cast<int>(waterPulseWeight);
                  //int decimalPart_waterPulseWeight = static_cast<int>((waterPulseWeight - integerPart_waterPulseWeight) * 10);
        display.clear();
        display.showNumberDec(integerPart_waterPulseWeight);
          }
        Serial.print(" waterPulseWeight=");
        Serial.print(waterPulseWeight);
        Serial.println(" //Вес импульса расходомера ( в литрах)");
    break;

        case 20:
        if (Enter_flag){
         display.clear();
         display.setSegments(P, 1, 0);
         display.setSegments(num + 2, 1, 1);
         display.setSegments(num + 0, 1, 2);
        
        } else {
              if (buttonState_Right){pumpVolumeForLoading = pumpVolumeForLoading + 1; buttonState_Right = false;}
              if (buttonState_Left){pumpVolumeForLoading = pumpVolumeForLoading - 1; buttonState_Left = false;}
              if (pumpVolumeForLoading <= 0){pumpVolumeForLoading = 0;}
              if (pumpVolumeForLoading >= 9999){pumpVolumeForLoading = 9999;}
                  int integerPart_pumpVolumeForLoading = static_cast<int>(pumpVolumeForLoading);
                  //int decimalPart_pumpVolumeForLoading = static_cast<int>((pumpVolumeForLoading - integerPart_pumpVolumeForLoading) * 10);
        display.clear();
        display.showNumberDec(integerPart_pumpVolumeForLoading);
          }
        Serial.print(" pumpVolumeForLoading=");
        Serial.print(pumpVolumeForLoading);
        Serial.println(" //Объем реактива, закачиваемый насосом в режиме «закачка», в литрах");
    break;

        case 21:
        if (Enter_flag){
         display.clear();
         display.setSegments(P, 1, 0);
         display.setSegments(num + 2, 1, 1);
         display.setSegments(num + 1, 1, 2);
        
        } else {
              if (buttonState_Right){pumpMassForLoading = pumpMassForLoading + 1; buttonState_Right = false;}
              if (buttonState_Left){pumpMassForLoading = pumpMassForLoading - 1; buttonState_Left = false;}
              if (pumpMassForLoading <= 0){pumpMassForLoading = 0;}
              if (pumpMassForLoading >= 9999){pumpMassForLoading = 9999;}
                  int integerPart_pumpMassForLoading = static_cast<int>(pumpMassForLoading);
                  //int decimalPart_pumpMassForLoading = static_cast<int>((pumpMassForLoading - integerPart_pumpMassForLoading) * 10);
        display.clear();
        display.showNumberDec(integerPart_pumpMassForLoading);
        }
        Serial.print(" pumpMassForLoading=");
        Serial.print(pumpMassForLoading);
        Serial.println(" //масса реактива, закачиваемая насосом в режиме «закачка», в килограммах");
    break;

      case 22:
         if (Enter_flag){
          display.clear();
          display.setSegments(P, 1, 0);
          display.setSegments(U, 1, 1);
          display.setSegments(P, 1, 2);
          display.setSegments(t, 1, 3);
          
         } else {       
               if (buttonState_Right){pulse_flag = true; buttonState_Right = false;}
               if (buttonState_Left){pulse_flag = false; buttonState_Left = false;}
               
               if (pulse_flag) {
                   display.clear();
                   display.setSegments(O, 1, 0);
                   display.setSegments(N, 1, 1);
                   Serial.println(" //насос вкл.");
                 }
               if (!pulse_flag) {
                   display.clear();
                   display.setSegments(O, 1, 0);
                   display.setSegments(F, 1, 1);
                   display.setSegments(F, 1, 2);
                   Serial.println(" //насос выкл.");
                 }

              }
      break;

      case 23:
        menu = 22;
      break;
      }
    } 
    if (pulse_flag) { 
        unsigned long currentMillis = millis();
        if (currentMillis - previousMillis >= interval) {
            previousMillis = currentMillis;
            pulseState = !pulseState;
            digitalWrite(SOLENOID_PIN, pulseState);
      }
    }
  }

// функции обработки прерываний кнопок
void IRAM_ATTR Up_fun()
{
  if(!button_delay){
    buttonState_Up = true;
    button_pressed = true;
  }
}

void IRAM_ATTR Down_fun(void)
{
  if(!button_delay){
    buttonState_Down = true;
    button_pressed = true;
  }
}

void IRAM_ATTR Left_fun(void)
{
  if(!button_delay){
    buttonState_Left = true;
    button_pressed = true;
  }
}

void IRAM_ATTR Right_fun(void)
{
  if(!button_delay){
    buttonState_Right = true;
    button_pressed = true;
  }
}

void IRAM_ATTR Enter_fun(void)
{
  if(!button_delay){
    buttonState_Enter = true;
    button_pressed = true;
  }
}

void IRAM_ATTR Level_fun(void)
{
  reactive_level_low = true;
}

void IRAM_ATTR Pulse_fun(void)
{
  reagentVolume_to_go += waterPulseVolume; // увеличиваем объем реактива к закачке на "объем" импульса расходомера
}
