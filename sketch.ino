// Copyright [2020] Jugal Gala <jugal.gala@sakec.ac.in>
// 
// SPDX-License-Identifier: Apache-2.0
//
// Proof-of-Principle Prototype to Detect and Report Traffic Data

#define WHEEL_SIZE_INCHES 26

volatile unsigned long lastPulseEvent = 0, timePeriod = 0;
void (* reset)() = 0;

void clearSerialInputBuffer() {
  while (Serial.available() > 0)
    Serial.read();
}

void enterCommandMode() {
  delay(1000);
  Serial.write("+++");
  delay(1000);
}


void isrPulse() {
  unsigned long temp = micros();
  timePeriod = temp - lastPulseEvent;
  lastPulseEvent = temp;
}

byte sendAttentionCommand(unsigned int maxResponseTime, String AttentionCommand, String ExpectedResponseMessage1, String ExpectedResponseMessage2) {
  unsigned long attentionCommandStartTime = millis();
  String ResponseMessage;

  clearSerialInputBuffer();
  Serial.println(AttentionCommand);
  while ((millis() - attentionCommandStartTime) <= maxResponseTime) {
    while (Serial.available() > 0)
      ResponseMessage += (char) Serial.read();
    if (ResponseMessage == ExpectedResponseMessage1)
      return 1;
    else if (ResponseMessage == ExpectedResponseMessage2)
      return 2;
  }
  return 0;
}

void setupGPRS() {
  byte ipDot = 0;
  unsigned long attentionCommandStartTime = millis();
  String ExpectedResponse;

  if (sendAttentionCommand(65535, "AT+CIPSHUT", "AT+CIPSHUT\r\r\nSHUT OK\r\n", "AT+CIPSHUT\r\r\nERROR\r\n") != 1)
    reset();
  if (sendAttentionCommand(65535, "AT+CIPMODE=1", "AT+CIPMODE=1\r\r\nOK\r\n", "AT+CIPMODE=1\r\r\nERROR\r\n") != 1)
    reset();
  if (sendAttentionCommand(65535, "AT+CSTT=\"www\",\"\",\"\"", "AT+CSTT=\"www\",\"\",\"\"\r\r\nOK\r\n", "AT+CSTT=\"www\",\"\",\"\"\r\r\nERROR\r\n") != 1)
    reset();
  if (sendAttentionCommand(65535, "AT+CIICR", "AT+CIICR\r\r\nOK\r\n", "AT+CIICR\r\r\nERROR\r\n") != 1)
    reset();
  clearSerialInputBuffer();
  Serial.println("AT+CIFSR");
  while ((millis() - attentionCommandStartTime) <= 65535 && ExpectedResponse != "AT+CIFSR\r\r\nERROR\r\n")
    while (Serial.available() > 0) {
      char c = Serial.read();
      ExpectedResponse += c;
      if (c == '.')
        if (++ipDot == 3) {
          delay(1000);
          return;
        }
    }
  reset();
}

byte waitForConnectAckPacket(unsigned long functionStartTime, unsigned int maxResponseTime) {
  byte connectAckPacket[] = {0x20, 0x02, 0x00, 0x00}, index = 0;

  while ((millis() - functionStartTime) <= maxResponseTime && index < 4)
    while (Serial.available() > 0)
      if (connectAckPacket[index] == Serial.read())
        index++;
  return index;
}

void sendMQTTConnectPacket(String Host, String Port, String Username, String Password) {
  byte fixedHeader[] = {0x10, 0x00}, protocolName[] = {0x00, 0x04, 0x4D, 0x51, 0x54, 0x54}, protocolLevel = 0x04, connectFlags = B11000010, keepAlive[] = {0x00, 0x3C}, index = 0;
  unsigned int usernameLength = Username.length(), passwordLength = Password.length();
  byte lsbUsernameLength = (byte) usernameLength, msbUsernameLength = (byte) (usernameLength >> 8);
  byte lsbPasswordLength = (byte) passwordLength, msbPasswordLength = (byte) (passwordLength >> 8);
  fixedHeader[1] = 16 + usernameLength + usernameLength + passwordLength;

  if (sendAttentionCommand(65535, "AT+CIPSSL=1", "AT+CIPSSL=1\r\r\nOK\r\n", "AT+CIPSSL=1\r\r\nERROR\r\n") != 1)
    reset();
  if (sendAttentionCommand(65535, "AT+CIPSTART=\"TCP\",\"" + Host + "\",\"" + Port + "\"", "AT+CIPSTART=\"TCP\",\"" + Host + "\",\"" + Port + "\"\r\r\nOK\r\n\r\nCONNECT\r\n",
                           "notapplicable") != 1)
    reset();

  Serial.write(fixedHeader, 2);
  Serial.write(protocolName, 6);
  Serial.write(protocolLevel);
  Serial.write(connectFlags);
  Serial.write(keepAlive, 2);
  Serial.write(msbUsernameLength);
  Serial.write(lsbUsernameLength);
  index = 0;
  while (index < usernameLength)
    Serial.write((byte) Username[index++]);
  Serial.write(msbUsernameLength);
  Serial.write(lsbUsernameLength);
  index = 0;
  while (index < usernameLength)
    Serial.write((byte) Username[index++]);
  Serial.write(msbPasswordLength);
  Serial.write(lsbPasswordLength);
  index = 0;
  while (index < passwordLength)
    Serial.write((byte) Password[index++]);

  if (waitForConnectAckPacket(millis(), 3000) != 4)
    reset();
}

void sendMQTTPublishPacket(String Topic, String Message) {
  int topicLength = Topic.length(), messageLength = Message.length();
  byte fixedHeader[] = {0x30, 0x00}, index = 0, lsbTopicLength = (byte) topicLength, msbTopicLength = (byte) (topicLength >> 8);
  fixedHeader[1] = 2 + topicLength + messageLength;

  Serial.println("ATO");
  delay(1000);

  Serial.write(fixedHeader, 2);
  Serial.write(msbTopicLength);
  Serial.write(lsbTopicLength);
  index = 0;
  while (index < topicLength)
    Serial.write((byte) Topic[index++]);
  index = 0;
  while (index < messageLength)
    Serial.write((byte) Message[index++]);
}

void setup() {
  attachInterrupt(digitalPinToInterrupt(2), isrPulse, RISING);
  Serial.begin(115200);
  enterCommandMode();
  if (sendAttentionCommand(65535, "AT+CGNSPWR?", "AT+CGNSPWR?\r\r\n+CGNSPWR: 1\r\n\r\nOK\r\n", "AT+CGNSPWR?\r\r\n+CGNSPWR: 0\r\n\r\nOK\r\n") != 1)
    if (sendAttentionCommand(65535, "AT+CGNSPWR=1", "AT+CGNSPWR=1\r\r\nOK\r\n", "AT+CGNSPWR=1\r\r\nERROR\r\n") != 1)
      reset();
  setupGPRS();
  sendMQTTConnectPacket("iot.jugalgala.xyz", "8883", "MH01AA0001", "iot");
}

void loop() {
  byte index = 0;
  float instantaneousSpeed;
  unsigned long attentionCommandStartTime = millis();
  String GlobalNavigationSatelliteSystemData[8] = {"", "", "", "", "", "", "", ""};

  if (micros() < lastPulseEvent)
    return;
  enterCommandMode();
  if (sendAttentionCommand(65535, "AT+CIPSTATUS", "AT+CIPSTATUS\r\r\nOK\r\n\r\nSTATE: CONNECT OK\r\n", "AT+CIPSTATUS\r\r\nOK\r\n\r\nSTATE: TCP CLOSED\r\n") != 1)
    reset();

  clearSerialInputBuffer();
  Serial.println("AT+CGNSINF");
  while (index < 8 && (millis() - attentionCommandStartTime) < 65535) {
    while (Serial.available() > 0) {
      char c = Serial.read();
      if (c == ',')
        index++;
      else
        GlobalNavigationSatelliteSystemData[index] += c;
    }
  }
  if ((char) GlobalNavigationSatelliteSystemData[0][GlobalNavigationSatelliteSystemData[0].length() - 1] != '1')
    reset();

  if (timePeriod == 0)
    instantaneousSpeed = 0;
  else
    instantaneousSpeed = (WHEEL_SIZE_INCHES * 0.0254 * 1000000.0 * 18) / (5 * timePeriod);
  timePeriod = 0;

  sendMQTTPublishPacket("MH01AA0001",
                        "{\"UTC\":\"" + (String) GlobalNavigationSatelliteSystemData[2] + "\",\"POS\":[\"" + (String) GlobalNavigationSatelliteSystemData[3] + "\",\""
                        + (String) GlobalNavigationSatelliteSystemData[4] + "\",\"" + (String) GlobalNavigationSatelliteSystemData[5] + "\"]," + "\"INST\":\""
                        + (String) instantaneousSpeed + "\",\"SOG\":\"" + (String) GlobalNavigationSatelliteSystemData[6] + "\",\"COG\":\""
                        + (String) GlobalNavigationSatelliteSystemData[7] + "\"}");
}
