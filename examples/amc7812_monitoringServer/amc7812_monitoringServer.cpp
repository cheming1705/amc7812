/*

This is a program that reads data when a trigger is recieved from an AMC7812 ADC
chip and uploads it to the monitoring server

created 18 Feb 2015 
by Matt Ebert

You need an Ethernet Shield (ENCJ) and an AMC7812 shield

Ethernet is handled by the uIPEthernet library by (Norbert Truchsess) which uses 
the uIP tcp stack by Adam Dunkels.

REQ and PUSH ZeroMQ sockets are emulated

 */

#include <Arduino.h>
#include <UIPEthernet.h>
#include <UIPClient.h>

#include "amc7812.h"
#include "amc7812conf.h"
#include "amc7812err.h"

#include "zmqduino.h"   // zmq interface
#include "monitoringServer.h" // monitoringServer interface

#define DHCP 0

uint8_t channels = AMC7812_ADC_CNT; //16
//uint8_t dataEntrySize = 5; // 16 bits ~> 65,000 -> 5 digits
//uint8_t dataEntrySize = 4; // 12 bits ~> 4,000 -> 4 digits
uint8_t dataEntrySize = 7; // for fp values

// TODO: make macro for size of buffer
char zmq_buffer[256]={0}; //!< buffer for zmq communication, needs to fit dataPacket

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xEE};

// initialize the library instance:
EthernetClient client;
ZMQSocket ZMQPush(client, zmq_buffer, PUSH);
DataPacket packet( channels, (char *)"amcFPTest", 9, dataEntrySize, zmq_buffer + ZMQ_MSG_OFFSET );

// fill in an available IP address on your network here,
// for manual configuration:
//IPAddress ip    (192,168,1,183);
IPAddress ip    (169,254,5,10);
//IPAddress ip    (10,128,226,195);
//IPAddress server(192,168,1,213);
//IPAddress server(128,104,160,150);
IPAddress server(169,254,5,183);
int reg_port = 5556;
int mes_port = 5557;

// mega pin 13
uint8_t TrigDDR = DDRB;
uint8_t TrigPORT = PORTB;
uint8_t TrigPIN = 7;
uint8_t lastTrig = 0;
uint8_t trigpin = 13;

// define my new class
AMC7812Class AMC7812;
// linear calibrations for channels
float m[AMC7812_ADC_CNT] = {1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0 // NC
  ,1.20886,1.26525  // X
  ,1.09459,1.20886  // Y
  ,0.156906,16.2056  // Z
  ,1.0,1.0}; // NC
float b[AMC7812_ADC_CNT] = {0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0 // NC
  ,0.0136734,0.0107174  // X
  ,0.0035753,0.0136734  // Y
  ,0.0031166,0.0934562  // Z
  ,0.0,0.0};  // NC

// channel linear conversions

const

uint8_t addReadings(){
  uint8_t spcr = SPCR;                            // save spi settings, before setting up for ADC
  uint8_t spsr = SPSR;                            // save spi settings, before setting up for ADC
  SPCR = AMC7812.GetSPCR();                       // set SPI settings for ADC operations
  SPSR = AMC7812.GetSPSR();                       // set SPI settings for ADC operations
  uint32_t ts = millis();                         // time start at start, ms
  uint8_t conv_success = AMC7812.ReadADCs();      // perform conversion cycle on active ADCs
  uint16_t* readings = AMC7812.GetADCReadings();  // retrieve results of the read
  //uint16_t adc_gains = AMC7812.GetADCGains();     // get ADC gains
  //AMC7812.TriggerADCsExternal();
  SPCR = spcr;  // leave no trace
  SPSR = spsr;  // leave no trace

  // TODO: convert to seconds, 
  //uint32_t secs = ts/1000;
  //uint32_t msecs = ts%1000; 
  //add function to make char string
  
  float voltages[channels];
  for( uint8_t i=0; i<=channels; i++ ){
    voltages[i] = conv_success ? 0 : (5.0*(float)readings[i]/(4096.0))*m[i] + b[i];
  }
  
  // TODO: do fp conversion here for readings
  uint8_t len = packet.preparePacket( ts, voltages );
  Serial.write((uint8_t*)(zmq_buffer+ZMQ_MSG_OFFSET),len);
  Serial.println();
  ZMQPush.sendZMQMsg(len);
  return conv_success;
}

uint8_t setup_DAQ(){
  uint8_t spcr = SPCR;                            // save spi settings, before setting up for ADC
  uint8_t spsr = SPSR;                            // save spi settings, before setting up for ADC
  SPCR = AMC7812.GetSPCR();                       // set SPI settings for ADC operations
  SPSR = AMC7812.GetSPSR();                       // set SPI settings for ADC operations

  Serial.print(F("\ninitializing AMC7812..."));
  uint8_t ret = AMC7812.begin();
  while ( ret ){
    Serial.print(F("Init of AMC7812 failed code: 0x"));
    Serial.println(ret, HEX);
    delay(1000);
    ret = AMC7812.begin();
  }
  // enter triggered mode
  AMC7812.SetTriggeredMode();
  //AMC7812.DisableADCs();
  //for( uint8_t i=8; i<=13; i++){
  //  AMC7812.EnableADC(i);
  //}
  
  SPCR = spcr;  // leave no trace
  SPSR = spsr;  // leave no trace

  Serial.println(F("AMC7812 device initialized"));
  return ret;
}

void setup() {
  // minimal SPI bus config (cant have two devices being addressed at once)
  PORTG |= (1<<0);  // set AMC7812 CS pin high if connected
  digitalWrite(SS, HIGH); // set ENCJ CS pin high 

  pinMode(trigpin, INPUT);

  Serial.begin(115200); //Turn on Serial Port for debugging

  uint8_t ret = setup_DAQ();

  // set up ethernet chip
  byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xEE};
#if DHCP && UIP_UDP // cant use DHCP without using UDP
  Serial.println(F("DHCP..."));
  if (Ethernet.begin(mac) == 0) {
    Serial.println(F("Failed DHCP"));
    for(;;)
      ;
  }
#else
  Serial.println(F("STATIC..."));
  Ethernet.begin(mac,ip);
#endif

  Serial.println(Ethernet.localIP());
  Serial.println(Ethernet.subnetMask());
  Serial.println(Ethernet.gatewayIP());
  Serial.println(Ethernet.dnsServerIP());

  // setup request socket
  Serial.println(F("Setting up REQ socket"));
  ZMQSocket ZMQReq( client, zmq_buffer, REQ );
  int16_t len;
  do{
    uint8_t err = 0;
    len = -1;
    if( ZMQReq.connect( server, reg_port ) ){
      client.stop(); // TODO: deal with this better
      Serial.println("Cant connect to server");
      err = 1;
    }
    if(!err){
      // register datastream with server
      Serial.println(F("Registering data stream..."));
      len = packet.registerFloatStream();
      ZMQReq.sendZMQMsg(len);
      len = ZMQReq.recv();
      if( len < 0 ){
        Serial.println(F("negative len returned"));
        client.stop();
      }
    }
  } while( len < 0 );
  // check that we got the expected response from the moitoring server
  if( len != 7 ){
    Serial.println(F("Invalid Response from server, length: "));
    Serial.println(len);
    for(;;)
      ;
  }
  // disconnect from registering port
  Serial.println(F("Disconnecting REQ socket..."));
  client.stop();
  
  delay(1000); // increase stability
  
  // setup push socket
  Serial.println(F("Setting up PUSH socket..."));
  if( ZMQPush.connect( server, mes_port ) ){
    Serial.println(F("oops"));
    client.stop(); // TODO: deal with this better
    for(;;)
      ;
  }
  Serial.println(F("Starting"));
  Serial.println(F("Data"));
  Serial.println(F("Stream"));
  delay(1000);
}

void loop() {
  Ethernet.maintain();

  uint8_t newTrig = digitalRead(trigpin);
  // trig on high to low trigger
  if( lastTrig && !newTrig ){
    //Send string back to client 
    if(addReadings() == AMC7812_TIMEOUT_ERR){
      setup_DAQ();
    }
  }
  lastTrig = newTrig;

  // check for incoming packet, do stuff if we need to
  uint8_t len = client.available();
  if( len ){
    len = ZMQPush.read(); // process header and get get actual mesg length
  }
}

// normal arduino main function
int main(void){
  init();

  setup();

  for(;;){
    loop();
    if (serialEventRun) serialEventRun();
  }
  return 0;
}
