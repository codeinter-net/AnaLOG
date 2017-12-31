// ~anaLog~
// PBA 2017-12-13
// Enregistreur de données analogiques
// Nécessite un shield LCD + clavier

#include <EEPROM.h>
#include <LiquidCrystal.h>
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

typedef struct
{
  word in;
  float out;
} IN_OUT;

typedef struct
{
  byte startHour;
  byte startMinute;
  byte numChannels;
  byte sampleRate;
} SET;

typedef struct
{
  IN_OUT inOut[10];
  SET set;
} PARAM;

PARAM param;

char anaLog[9]={0x60,'a','n','a','L','O','G',0xA4,0};

#define KEY_NONE   0
#define KEY_UP     1
#define KEY_DOWN   2
#define KEY_LEFT   3
#define KEY_RIGHT  4
#define KEY_SELECT 5
#define LCD_BACKLIGHT 10

#define MODE_MENU 0
#define MODE_PLAY 1
#define MODE_REC 2
#define MODE_SET 3
#define MODE_CAL 4
#define MODE_CAL_DATA 5
#define MODE_INIT -1

byte currentMode = MODE_MENU ;
byte menuSelect = MODE_PLAY;
byte setDigit = 0;
byte setHour = 0;
byte setMinute = 0;
byte setInter = 5;
byte setChannel = 1;
byte calIndex = 0;
byte recMode;
byte channel = 0;
word recIndex = 0;
long nextRecTime = 0;
word recStop=0xFFFF;  // Marqueur de fin d'enregistrement

byte setDigitList[]={0,1,3,4,8,12,13}; // Positions X des digits pour le réglage heure, minutes et intervalle
byte analogList[]={A1,A2,A3,A4,A5}; // Liste des canaux analogiques à lire

word inData;
char calData[10];
byte calDataIndex;
char calDataList[]={' ','0','1','2','3','4','5','6','7','8','9','.','-'};

unsigned long startTime = 0; // Diffénrence de secondes lors du démarrage
//unsigned long startCapture = 0; // valeur de millis() lors du début de la capture

void dataDump() // Export des données brutes sur le port série
{
  word i,j;
  byte data[16];
  char out[8];
  for(i=0; i<64; i++)
  {
    word addr=(i<<4);
    sprintf(out,"%03x : ",addr);
    Serial.print(out);
    for(j=0; j<16; j++)
    {
      data[j]=EEPROM.read(addr++);
    }
    for(j=0; j<16; j++)
    {
      sprintf(out,"%02x ",data[j]);
      Serial.print(out);
    }
    for(j=0; j<16; j++)
    {
      char c=data[j];
      if((c<32)||(c==127)) c='.';
      Serial.write(c);
    }
    Serial.println();
  }
}

void dataExport() // Export des données formatées sur le port série
{
  word index = sizeof(param);
  long recTime;
  EEPROM.get(index,recTime);
  index+=sizeof(recTime);
  long currentTime=recTime;
  byte seconds = currentTime % 60;
  currentTime /= 60;
  byte minutes = currentTime % 60;
  currentTime /= 60;
  byte hours = currentTime % 24;
  char fullTime[10];
  sprintf(fullTime, "%02d:%02d.%02d", hours, minutes, seconds);
  Serial.println(fullTime);

  int inData;
  while(index<EEPROM.length())
  {
    currentTime=recTime;
    byte seconds = currentTime % 60;
    currentTime /= 60;
    byte minutes = currentTime % 60;
    currentTime /= 60;
    byte hours = currentTime % 24;
    sprintf(fullTime, "%02d:%02d", hours, minutes);
    Serial.print(fullTime);
    recTime+=param.set.sampleRate*60;
    int i;
    for(i=0; i<param.set.numChannels; i++)
    {
      EEPROM.get(index,inData);
      if(inData==0xFFFF) break;
      index+=sizeof(inData);
      Serial.print(",");
      Serial.print(inData);
      IN_OUT* ioMin = &param.inOut[i<<1];
      IN_OUT* ioMax = &param.inOut[(i<<1)+1];
      float out = ioMin->out + (ioMax->out-ioMin->out)*(inData-ioMin->in)/(ioMax->in-ioMin->in);
      Serial.print(",");
      Serial.print(out);
    }
    Serial.println();
    if(inData==0xFFFF) break;
  }
}

int readButtons()  // Lecture des boutons
{
  int keyInput = analogRead(0);
  if (keyInput > 1000) return KEY_NONE;
  if (keyInput < 50)   return KEY_RIGHT;
  if (keyInput < 195)  return KEY_UP;
  if (keyInput < 380)  return KEY_DOWN;
  if (keyInput < 555)  return KEY_LEFT;
  if (keyInput < 790)  return KEY_SELECT;
  return KEY_NONE;
}

byte getDataIndex(char c) // Retourne l'index du caractère
{
  byte i;
  for(i=0;i<sizeof(calDataList);i++)
    if(calDataList[i]==c)
      return i;
  return 0;
}

void recData(char* data,byte size)  // Ecrit des données en NVRAM
{
  if(size>EEPROM.length()-sizeof(param)-recIndex) return; // Plus d'espace libre
  while(size-->0)
  {
    EEPROM.write((recIndex++)+sizeof(param),*data++);
  }
  lcd.setCursor(4, 0);
  lcd.print((long)recIndex*100/(EEPROM.length()-sizeof(param))+1);  // Indicateur de remplissage
  lcd.print("%");
}

void setup()
{
  Serial.begin(115200);
  lcd.begin(16, 2);
  lcd.setCursor(0, 0);
  lcd.print(anaLog);
  lcd.noCursor();
  lcd.noBlink();
  pinMode(LCD_BACKLIGHT, OUTPUT);
  digitalWrite(LCD_BACKLIGHT, 1);
  EEPROM.get(0,param);
}

void loop()
{
  char outText[12];
  unsigned long currentTime;
  static int lastData;
  static byte lastCalIndex;
  byte dataListIndex;

  // Affichage de l'heure
  switch(currentMode)
  {
  case MODE_MENU :
  case MODE_REC :
    currentTime = (millis() / 1000) + startTime; // secondes écoulées depuis la mise à jour de l'heure
    static unsigned long lastTime;
    if(lastTime!=currentTime)
    {
      lastTime=currentTime;
      byte seconds = currentTime % 60;
      currentTime /= 60;
      byte minutes = currentTime % 60;
      currentTime /= 60;
      byte hours = currentTime % 24;
      char fullTime[10];
      lcd.setCursor(8, 0);
      sprintf(fullTime, "%02d:%02d.%02d", hours, minutes, seconds);
      lcd.print(fullTime);
    }
  }

  // Actions effectués en cas de chargement de mode
  static byte lastMode = MODE_INIT;
  if(currentMode!=lastMode)
  {
    lastMode=currentMode;
    switch(currentMode)
    {
    case MODE_MENU :
      lcd.setCursor(0, 0);
      lcd.print(anaLog);
      lcd.setCursor(0, 1);
      lcd.blink();
      lcd.print("PLY REC SET CAL");
      break;
    case MODE_PLAY :
      lcd.noBlink();
      lcd.setCursor(0, 1);
      lcd.print("Data export    ");
      Serial.println("anaLOG");
      dataDump();
      dataExport();
      currentMode=MODE_MENU;
      break;
    case MODE_REC :
      lcd.clear();
      lcd.noBlink();
      lcd.setCursor(0, 0);
      lcd.print("PAUSE   ");
      recMode=0;
      recIndex=0;
      break;
    case MODE_SET :
/*
      currentTime = (millis() / 1000) + startTime;
      currentTime /= 60;
      setMinute = currentTime % 60;
      currentTime /= 60;
      setHour = currentTime % 24;
*/
      setHour=param.set.startHour;
      setMinute=param.set.startMinute;
      setChannel=param.set.numChannels;
      setInter=param.set.sampleRate;
      setDigit = 0;
      lcd.setCursor(0, 0);
      lcd.print("Time    Ch. Rate");
      lcd.setCursor(0, 1);
      sprintf(outText,"%02d:%02d   %01d   %02d  ",setHour,setMinute,setChannel,setInter);
      lcd.print(outText);
      lcd.setCursor(setDigitList[0], 1);
      lcd.blink();
      break;
    case MODE_CAL :
      lcd.noBlink();
      lcd.clear();
      lastData=0;
      lastCalIndex=-1;
      break;
    case MODE_CAL_DATA :
      lcd.setCursor(12, 0);
      lcd.print("    ");
      lcd.setCursor(12, 0);
      lcd.print(inData);
      lcd.setCursor(7, 1);
      lcd.print("         ");
      lcd.setCursor(7, 1);
      lcd.blink();
      calDataIndex=0;
      memset(calData,' ',sizeof(calData));
      break;
    }
  }

  // Actions effectuées systématiquement
  switch(currentMode)
  {
  case MODE_MENU :
    lcd.setCursor((menuSelect-1)<<2, 1);
    lcd.blink();
    break;
  case MODE_REC :
    // TODO : Changer régulièrement de canal affiché
    inData=analogRead(analogList[channel]);
    if(inData!=lastData)
    {
      lastData=inData;
      lcd.setCursor(0, 1);
      lcd.print("A");
      lcd.print(channel+1);
      lcd.print(":");
      lcd.print(inData);
      lcd.print(" ");
      IN_OUT* ioMin = &param.inOut[channel<<1];
      IN_OUT* ioMax = &param.inOut[(channel<<1)+1];
      float out = ioMin->out + (ioMax->out-ioMin->out)*(inData-ioMin->in)/(ioMax->in-ioMin->in);
      lcd.print(out);
    }
    if((recMode==1)&&(currentTime>nextRecTime)) // Enregistrement des données
    {
      int i;
      for(i=0; i<param.set.numChannels; i++)
      {
        inData=analogRead(analogList[channel]);
        recData((char*)&inData,sizeof(inData));
      }
      nextRecTime=currentTime+param.set.sampleRate*60;
    }
    break;
  case MODE_CAL :
    inData=analogRead(analogList[calIndex>>1]);
    if(inData!=lastData)
    {
      lastData=inData;
      lcd.setCursor(0, 0);
      lcd.print("A");
      lcd.print((calIndex>>1)+1);
      lcd.print(":");
      lcd.print(inData);
      if(inData<1000) lcd.print(" ");
      if(inData<100) lcd.print(" ");
      if(inData<10) lcd.print(" ");
      lcd.setCursor(8, 0);
      lcd.print(calIndex&1?"Max:":"Min:");
      int in=param.inOut[calIndex].in;
      if(in&0xFC00)
        lcd.print("----");
      else
        lcd.print(in);
      lcd.print("   ");
      lcd.setCursor(0, 1);
      lcd.print(calIndex&1?"OutMax:":"OutMin:");
      lcd.print(param.inOut[calIndex].out);
      lcd.print("         ");
      lcd.setCursor(7, 1);
    }
    break;
  }

  // Actions effectuées en cas d'appui sur les boutons
  byte currentKey = readButtons();
  static byte lastKey=KEY_NONE;
  if(currentKey!=lastKey)
  {
    lastKey=currentKey;
    switch(currentMode)
    {
    case MODE_MENU :
      switch (currentKey)
      {
      case KEY_LEFT:
        if(menuSelect>MODE_PLAY)
        {
          menuSelect--;
          lcd.setCursor(menuSelect<<2, 1);
        }
        break;
      case KEY_RIGHT:
        if(menuSelect<MODE_CAL)
        {
          menuSelect++;
          lcd.setCursor(menuSelect<<2, 1);
        }
        break;
      case KEY_SELECT:
        currentMode=menuSelect;
        calIndex=0;
        break;
      }
      break;
    case MODE_REC :
      switch (currentKey)
      {
      case KEY_LEFT:  // Stopper l'enregistrement
        switch(recMode)
        {
        case 1 :
          recMode=0;
          recData((char*)&recStop,sizeof(recStop));
          lcd.setCursor(0, 0);
          lcd.print("PAUSE   ");
          break;
        default:
          currentMode=MODE_MENU;
          digitalWrite(LCD_BACKLIGHT, 1); // Eclairage activé
          break;
        }
        break;
      case KEY_RIGHT: // Démarrer l'enregistrement
        lcd.setCursor(0, 0);
        lcd.print("REC     ");
        recMode=1;
        nextRecTime=currentTime;
        recData((char*)&currentTime,sizeof(currentTime));
        break;
      case KEY_UP:
        digitalWrite(LCD_BACKLIGHT, 1); // Eclairage activé
        break;
      case KEY_DOWN:
        digitalWrite(LCD_BACKLIGHT, 0); // Eclairage désactivé
        break;
      }
      break;
    case MODE_SET :
      switch (currentKey)
      {
      case KEY_LEFT:
        if(!setDigit)  // Stopper l'enregistrement
        {
          currentMode=MODE_MENU;
          break;
        }
        lcd.setCursor(setDigitList[--setDigit], 1);
        break;
      case KEY_RIGHT:
        if(setDigit<sizeof(setDigitList)-1)
          lcd.setCursor(setDigitList[++setDigit], 1);
        break;
      case KEY_UP:
        switch(setDigit)
        {
        case 0 :
          if(setHour<14) setHour+=10;
          break;
        case 1 :
          if(setHour<23) setHour++;
          break;
        case 2 :
          if(setMinute<50) setMinute+=10;
          break;
        case 3 :
          if(setMinute<59) setMinute++;
          break;
        case 4 :
          if(setChannel<5) setChannel++;
          break;
        case 5 :
          if(setInter<50) setInter+=10;
          break;
        case 6 :
          if(setInter<59) setInter++;
          break;
        }
        lcd.setCursor(0, 1);
        sprintf(outText,"%02d:%02d   %01d   %02d  ",setHour,setMinute,setChannel,setInter);
        lcd.print(outText);
        lcd.setCursor(setDigitList[setDigit], 1);
        break;
      case KEY_DOWN:
        switch(setDigit)
        {
        case 0 :
          if(setHour>9) setHour-=10;
          break;
        case 1 :
          if(setHour>0) setHour--;
          break;
        case 2 :
          if(setMinute>9) setMinute-=10;
          break;
        case 3 :
          if(setMinute>0) setMinute--;
          break;
        case 4 :
          if(setChannel>1) setChannel--;
          break;
        case 5 :
          if(setInter>10) setInter-=10;
          break;
        case 6 :
          if(setInter>1) setInter--;
          break;
        }
        lcd.setCursor(0, 1);
        sprintf(outText,"%02d:%02d   %01d   %02d  ",setHour,setMinute,setChannel,setInter);
        lcd.print(outText);
        lcd.setCursor(setDigitList[setDigit], 1);
        break;
      case KEY_SELECT:
        startTime=(long)setHour*3600+(long)setMinute*60+86400-(millis()/1000);
        param.set.startHour=setHour;
        param.set.startMinute=setMinute;
        param.set.numChannels=setChannel;
        param.set.sampleRate=setInter;
        EEPROM.put((char*)&param.set-(char*)&param,param.set);
        currentMode=MODE_MENU;
        break;
      }
      break;
    case MODE_CAL :
      lastData=0;
      switch (currentKey)
      {
      case KEY_LEFT:
        currentMode=MODE_MENU;
        break;
      case KEY_UP:
        if(calIndex<9) calIndex++;
        break;
      case KEY_DOWN:
        if(calIndex>0) calIndex--;
        break;
      case KEY_SELECT:
        currentMode=MODE_CAL_DATA;
        break;
      }
      break;
    case MODE_CAL_DATA :
      switch (currentKey)
      {
      case KEY_LEFT:
        if(!calDataIndex)
          currentMode=MODE_CAL;
        else
          lcd.setCursor(7+(--calDataIndex), 1);
        break;
      case KEY_RIGHT:
        if(calDataIndex<8)
          lcd.setCursor(7+(++calDataIndex), 1);
        break;
      case KEY_UP:
        dataListIndex=getDataIndex(calData[calDataIndex]);
        if(++dataListIndex==sizeof(calDataList))
          dataListIndex=0;
        lcd.setCursor(7+calDataIndex, 1);
        lcd.print(calData[calDataIndex]=calDataList[dataListIndex]);
        lcd.setCursor(7+calDataIndex, 1);
        break;
      case KEY_DOWN:
        dataListIndex=getDataIndex(calData[calDataIndex]);
        if(!dataListIndex)
          dataListIndex=sizeof(calDataList)-1;
        else
          dataListIndex--;
        lcd.setCursor(7+calDataIndex, 1);
        lcd.print(calData[calDataIndex]=calDataList[dataListIndex]);
        lcd.setCursor(7+calDataIndex, 1);
        break;
      case KEY_SELECT:
        calData[sizeof(calData)-1]=0;
        float floatData=atof(calData);
        param.inOut[calIndex].in=inData;
        param.inOut[calIndex].out=floatData;
        EEPROM.put((char*)&param.inOut[calIndex]-(char*)&param,param.inOut[calIndex]);
        currentMode=MODE_CAL;
        break;
      }
      break;
    }
  }
  delay(100);
}

