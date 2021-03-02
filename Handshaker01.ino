#include <TinyPICO.h>
#include <ArduinoJson.h>

// {"msg": "reset"}
// {"msg":"setDevice","id":"123456","type":"l","baud":"256000"}
// {"msg": "test", "string": "ABC123"}
// {"msg":"test","string":"ABC123Martin"}
// {"msg":"echo","string":"test123"}
// {"msg":"getstatus"}

#define MaxReadBytes 64
void GetMessageFromPort(String& s);
void SendMessageToPort(String& message);

/////////////////////////////////////////////////////////////////////
class MessageReceiver // interface
{
  public:
    virtual  bool HandleMessage(const StaticJsonDocument<200>& doc, const String& msgString ) = 0;
};

class MartinListener : public MessageReceiver
{
  public:
  bool HandleMessage(const StaticJsonDocument<200>& doc, const String& msgString) override
  {
     if(msgString.compareTo("echo") == 0)
      {
        Serial.print(F("echo msg"));
        const char* str = doc["echo"];
        Serial.print(str);
        return true;
      }
      return false;
  }
};

template<typename T>
struct CallbackArray
{
  static const int maxNum = 10;
  T array[maxNum];
  int index;
  CallbackArray(): index(0){}
  void Add(T& t) 
  {
    if(index >= maxNum)
    {
      Serial.print(F("too many elements in container"));
      return;
    }
    array[index++] = t;
  }
  bool HandleMessage(const StaticJsonDocument<200>& doc, const String& msgType)
  {
    for(int i=0; i<index; i++)
    {
      auto var = array[i];
      bool result = var->HandleMessage(doc, msgType );
      if(result == true)
        return true;
    }
    return false;
  }
};

/////////////////////////////////////////////////////////////////////
class CommsStatus
{
  public:
  enum Status{init, resetting, awaiting_validation, connected, invalid};
  enum DeviceType{left, right, pod};
  
    String id;
    Status status;
    int deviceType;
    CallbackArray<MessageReceiver*> callbacks;
    int baudRate;
    
  public:
    CommsStatus(): status(Status::init), deviceType(DeviceType::left), baudRate(9600){}
    void ResetStats(){}
    bool IsConnected(){return false;}
    void Add(MessageReceiver* callback){ callbacks.Add(callback);}
    bool HandleMessage(String& json)
    {
        String msgString;
       StaticJsonDocument<200> doc;
       if(Deserialize(json, doc, msgString) == false)
       {
          return false;
       }

       if(HandleStatus(msgString))
       {
          return true;
       }
       if(HandleReset(msgString))
       {
          return true;
       }
       if(status == invalid) {OutputStatus();return false;}
       if(status == Status::connected)
       {
         callbacks.HandleMessage(doc, msgString);
          return true;
       }
       if(HandleDeviceSettingMessages(msgString, doc) == true) { return true; }
       if(HandleAwaitingValidation(msgString, doc) == true) { return true; }
    
       return true;
    }
    
  private:
    bool Deserialize(const String& json, StaticJsonDocument<200>& doc, String& msgString)
    {
       DeserializationError error = deserializeJson(doc, json);
       if (error) {
          Serial.print(F("deserializeJson() failed: "));
          Serial.println(error.f_str());
          return false;
        }
       const char* messageType = doc["msg"];
       if(messageType == nullptr)
       {
        Serial.print(F("CommsStatus rejects msg"));
        return false;
       }
       msgString = messageType;
       return true;
    }
    bool HandleStatus(String& msgString)
    {
      if(msgString.compareTo("getstatus") == 0)
       {
        OutputStatus();
        return true;
       }
       return false;
    }
    void OutputStatus()
    {
      switch(status)
        {
          case init: 
            Serial.print(F("status: init"));
            break;
           case resetting: 
            Serial.print(F("status: resetting"));
            break;
           case awaiting_validation: 
            Serial.print(F("status: awaiting_validation"));
            break;
           case connected: 
            {
              Serial.println(F("status: connected"));
              Serial.print("Baud: ");
              Serial.println(baudRate, DEC);
            }
            break;
           case invalid:
            Serial.print(F("status: invalid"));
            break;
           default:
            Serial.print(F("status: unknown"));
            break;
        }
    }
    bool HandleReset(String& msgString)
    {
      if(msgString.compareTo("reset") == 0)
       {
        status = Status::resetting;
        Serial.print(F("CommsStatus resets"));
        return true;
       }
       return false;
    }
    bool HandleDeviceSettingMessages(String& msgString, StaticJsonDocument<200>& doc)
    { 
      if(msgString.compareTo("setDevice") != 0)
        return false;
      
      if(status != Status::resetting)
      {
        Serial.println(F("HandleDeviceSettingMessages bad state"));
        status = Status::invalid;
        OutputStatus();
        return false;
      }
      
      const char* _id = doc["id"];
      id = _id;
      deviceType = doc["type"];
      Serial.println(F("CommsStatus setDevice"));
      SendMessageToPort(id);
      Serial.print(deviceType, DEC);
      
      baudRate = doc["baud"];

      StaticJsonDocument<100> ack;
      ack.clear();
      ack["ack"] = "setDevice";
      ack["id"] = id;
      if(baudRate != 0)
        ack["baud"] = baudRate;
      serializeJson(ack, Serial);
      status = Status::awaiting_validation;
      if(baudRate != 0)
      {
        Serial.print(F("resetting baud rate"));
        Serial.begin(baudRate);
      }
      return true;
    }

    bool HandleAwaitingValidation(String& msgString, StaticJsonDocument<200>& doc)
    {
      if(msgString.compareTo("test") != 0)
      {
        return false;
      }
      if(status != Status::awaiting_validation)
      {
        Serial.println(F("HandleAwaitingValidation bad state"));
        status = Status::invalid;
        OutputStatus();
        return false;
      }
      
      const char* randomString = doc["string"];
      Serial.println(F("CommsStatus HandleAwaitingValidation"));

      StaticJsonDocument<100> ack;
      ack.clear();
      ack["ack"] = "test";
      ack["string"] = randomString;
      ack["id"] = id;
      serializeJson(ack, Serial);
      status = Status::connected;
      return true;
      
    }
    
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/// global status ///
CommsStatus status;
MartinListener listener;

void setup() 
{
  Serial.begin(9600);
  status.Add(&listener);
}


////////////////////////////////////////////////////////////
void loop() 
{
  String message;
  GetMessageFromPort(message);
  if(message.length() > 0)
  {
    Serial.println(message.c_str());    
    status.HandleMessage(message);
    
  }
  delay(1000);
}

////////////////////////////////////////////////////////////

void GetMessageFromPort(String& s)
{
  if(s.length() != 0)
  {
    s = "";
  }
  int numBytes = Serial.available();
  if(numBytes >0)
  {
    char buffer[MaxReadBytes+1];// max is 64 bytes on a read
   //Serial.readBytes(buffer, numBytes);
   s = Serial.readString();
  }
}

void SendMessageToPort(String& message)
{
  Serial.println(message); 
}

////////////////////////////////////////////////////////////

 
////////////////////////////////////////////////////////////
