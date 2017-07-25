/* GSM Mains Monitor
   Version 1.0
   Written by Andy Doswell 2015
   License: The MIT License (See full license at the bottom of this file)
   
   Schematic can be found at www.andydoz.blogspot.com/

   A mains monitoring device, which will send SMS status messages on the condition of the supply.  
   
   */
   #include <FreqMeasure.h>
   #include <LiquidCrystal.h>


  double SumFreq=0; // Cumulative frequency
  int CountFreq=0; // Number of time frequency has been measured
  double SumACVolt=0; // Cumulative ACVoltage
  int Count=0; // Loop counter
  float FrequencyOut = 50; // Default frequency value
  float ACVoltage =0; // instantaneous ACVoltage
  float DCVoltage =12; // Default DC Voltage
  float ACVoltageAvg = 240; //Default AC Voltage
  boolean SMSTransmitFlag = true; // SMSTransmit flag. This is used to prevent message duplication, and also ensures a power restored message is sent on power up
  boolean BatteryTransmitFlag = false; // Flag set when battery voltage drops, to prevent multiple messages being sent
  boolean FailFlag = false; // Used to check if the mains is OK or not
  String text=""; // Used in the composition of SMS message
  int Status = 3; // Ensures the voltage and frequency loop is completed 3 times on power up, to elliminate spikes on power up, and also after SMS message has been transmitted. Prevents false triggering.
  float MainsMinV = 216.2; // This sets the lower limit for the mains voltage. Change this to suit your local voltage limit
  float MainsMaxV= 253; // Maximum voltage limit
  float BatteryMin=11.2; // Battery low limit
  float MainsMinF=49.5; // Minimum allowable mains frequency limit, change to suit local power
  float MainsMaxF=50.5; // Maximum allowable mains frequency limit.
  char Reply[200]; // Serial buffer length
  int Signal; // Signal strength as reported
  int temp1; // 4 temporary integers, used to extract the data from the incoming serial.
  int temp2;
  int temp3;
  int temp4;
  int BER; // Bit error rate
  int SignaldBm; //Signal in dBm
  char Provider [34]; // Service Privider name
  LiquidCrystal lcd(12, 11, 5, 4, 3, 2); // Define where our LCD is connected.

    
  void setup() {
    Serial.begin(9600); // Start serial comms.
    delay(2000); // alow GSM module time to come up before we attempt to configure it
    digitalWrite (A4, HIGH); // Switch the LCD backlight on
    lcd.begin(20,4);
    lcd.clear();
    lcd.setCursor (0,0);
    lcd.print (" GSM Mains Monitor ");
    lcd.setCursor(0,1);
    lcd.print ("(C) A.G.Doswell 2015");
    lcd.setCursor (0,2);
    lcd.print ("GSM connecting.");
    Serial.print ("ATE0\r"); // Set SIM 900 echo off
    delay (220);
    GetReply ();
    Serial.print("AT+CMGF=1\r"); // Set Serial to text mode
    delay (220);
    GetReply();
    Serial.print("AT+CNMI=1,2,0,0,0\r"); // set GSM module to send an incoming SMS message to the serial interface automatically.
    delay (220);
    GetReply();
    delay (10000); // wait for GSM to connect
    lcd.print ("."); // and draw some dots so the user knows something's happening
    delay (10000);
    lcd.print ("..");
    delay (10000);
    lcd.print ("..");
    delay (10000);
    GetProvider ();
    FreqMeasure.begin(); // start the FreqMeasure library
    delay (1500);    // Give FreqMeasure some time to settle
    lcd.clear();
    pinMode (A2, OUTPUT); //charge control pin is A2
    digitalWrite (A2, LOW);  //Charger on
    pinMode (A4, OUTPUT); //backlight control is pin A4
    pinMode (A3, INPUT); // Push to transmit
    digitalWrite (A3, HIGH);
    pinMode (A5, INPUT); // GSM Inhibit switch
    digitalWrite (A5,HIGH);

}

void lcd_display() { //Update the LCD display
  lcd.setCursor (0,0);
  lcd.print ("Mains ");
  if (!ACVoltageAvg) {
    lcd.print ("00");
  }
  lcd.print (ACVoltageAvg);
  lcd.print ("VAC ");
  lcd.setCursor (0,1);
  lcd.print (FrequencyOut);
  lcd.print ("Hz ");
  lcd.setCursor (0,3);
  lcd.print ("Batt ");
  lcd.print (DCVoltage);
  lcd.print ("V ");
  lcd.setCursor (16,0);
  if (!FailFlag) {
    lcd.print ("  OK");
    digitalWrite (A4, HIGH);// make sure the backlight is on.
  }
  else {
    lcd.print ("FAIL");
    digitalWrite (A4, LOW); // switch the backlight off in the event of power fail, to save a few mA, as we're now running on battery
  }
  lcd.setCursor (0,2);
  lcd.print(Provider);
  lcd.print (" ");
  lcd.print(SignaldBm);
  lcd.print("dBm ");
}
   
void GetBatteryVoltage () { // Get the current battery voltage
    digitalWrite (A2, HIGH); // switch the charger off
    delay (50);
    int sensorValueDC = analogRead (A1);
    digitalWrite (A2, LOW);// switch the charger back on
    DCVoltage = (sensorValueDC * (5.0 / 1023.0)) * 3;
}

void GetACStats () { // Create the averaged outputs for frequency and ACVoltage
    float frequency = FreqMeasure.countToFrequency(SumFreq/CountFreq);
    FrequencyOut = frequency;
    ACVoltageAvg = (SumACVolt/Count);
    if (FrequencyOut == INFINITY) {
      FrequencyOut = 0;
      ACVoltageAvg = 0;
    }   
    lcd_display();
    ResetStats ();
}

void TransmitPowerFailSMS () { // Send a power fail message if the mains is out of spec, and only if TX switch is ON.
  
    if ((!SMSTransmitFlag) && (digitalRead (A5) == HIGH)){
      lcd.setCursor (0,2);
      lcd.print ("Sending Power fail  "); 
      text = "Mains Fail "; // Set SMS message first line.
      ComposeMsg (); // Set the rest of the message
      lcd.setCursor (0,2);
      lcd.print ("                    ");
      SMSTransmitFlag = true; // Set The SMS flag, so we know a fail message has been sent.
      ResetStats (); // Reset the frequency and voltage stats. This is to prevent our frequency picking up interference from the GSM module and failing 
  }
  else {
    return;
    }
}

void TransmitPowerRestoredSMS () { // Send Power restored message if a fail message has been sent previously, and only if TX switch is ON.
  if ((SMSTransmitFlag == true) && (digitalRead (A5) == HIGH)) {
    lcd.setCursor (0,2);
    lcd.print ("Sending Power OK    ");
    text = "Power Restored.  "; // Set first line of SMS message
    ComposeMsg (); // Set the rest of the message
    lcd.setCursor (0,2);
    lcd.print ("                    ");
    SMSTransmitFlag = false; // set the flag to false, so we don't do this again until a power fail message has been transmitted.
    ResetStats ();
}
  else {
    return;
  }
}

void TransmitBatteryLow () { // Send battery low message if we haven't done it before.
  if ((BatteryTransmitFlag == true) || (digitalRead (A5) == LOW)) {
    return;
  }
    else {
      lcd.setCursor (0,2);
      lcd.print ("Sending battery low ");
      text = "Battery Low "; 
      ComposeMsg();
      lcd.setCursor (0,2);
      lcd.print ("                    ");
      BatteryTransmitFlag = true;   // set the battery transmit flag   
      ResetStats ();
    }
}

void TransmitStatus() { // Send requested message
  lcd.setCursor (0,2);
  lcd.print ("Sending on request  ");
  delay (150);
  text = "Current Status: ";
  ComposeMsg ();
  lcd.setCursor (0,2);
  lcd.print ("                    ");
  ResetStats ();
}

void GetReply() { // Reads serial replies from GSM module. Checks to see if "OK" is sent by GSM Module

  int i = 0;
  while (Serial.available()>0) {
      Reply[i] = Serial.read();
      delay (10);
      if (Reply [i]=='K' && Reply [i-1] =='O'){
        Serial.read();
        Serial.read();
        Reply [i+1] = '\0';
        Serial.print (Reply);
        return;
      }
      i ++;
  }
  Reply [i] = '\0'; // End the array nicely.
	
}  
         
void GetSignal() {  // request signal strength from GSM.
  Serial.print ("AT+CSQ\r");
  delay (150);
}

void GetSMS (){ // Checks to see if incoming sms is "Check", if it is , then send status message.
  if (Reply[51]=='C' && Reply[52]=='h' && Reply[53]=='e' && Reply[54]=='c' && Reply[55]=='k') {
    TransmitStatus();
  }
  Count = 0;
}

void GetProvider () { // This runs once, and gets the mobile network service provider ID
  if (Serial.available() !=0){
    GetReply();
  }
  Serial.print ("AT+COPS?\r");
  delay(150);
  GetReply ();
  int i=14;
  while (Reply[i] !='"') {
    Provider [i-14] = Reply[i];
    Provider [i-13] = '\0';
    i++;
  }
}    

void ResetStats () { // this routine resets our voltage and frequency statistics, and avoids issues between FreqMeasure and SoftwareSerial libraries sharing an interrupt.
    SumACVolt = 0;
    SumFreq = 0;
    CountFreq=0;
    Count = 0;
}

void ComposeMsg() { // This composes and sends the sms message
      text.concat(ACVoltageAvg);
      text = text + "VAC. ";
      text.concat (FrequencyOut);
      text = text + "Hz. Battery: ";
      text.concat (DCVoltage);
      text = text + "V";
      Serial.print("AT+CMGF=1\r");// AT command to send SMS message
      delay(150);
      Serial.println("AT + CMGS = \"+44xxxxxxxxxx\"");// recipient's mobile number, in international format
      delay(150);
      Serial.println(text);// message to send
      delay(150);
      Serial.println((char)26);                       // End AT command with a ^Z, ASCII code 26
      delay(150); 
      Serial.println(); 
      delay(1500);                                     // give module time to send SMS 
      text ="";
      Status = 3;
}

void loop() {
 
  Count++; //increment a loop counter
    
  if (digitalRead (A3) == LOW){ // Checks the "push to transmit" button, and transmitts if it's low.
    TransmitStatus();
  }
  
  if (FreqMeasure.available()) { // Checks frequency of mains, and adds to the sum, ready for averaging.
    SumFreq = SumFreq + FreqMeasure.read();
    CountFreq=CountFreq+1;
  }
  
  int sensorValue = analogRead(A0); // Reads our scales AV voltage , and adds to the sum, ready for averaging.
  ACVoltage = (sensorValue * (5.0 / 1023.0))*51;
  SumACVolt=SumACVolt+ACVoltage;
  
   if  (digitalRead (A5) == LOW) {// Checks the transmit inhibit switch. 
     lcd.setCursor (14,3);
     lcd.print ("TX off");
   }
   else{
     lcd.setCursor (14,3);
     lcd.print ("      ");
   }

 
  if (Count ==1) { // everytime the loop counter is 1, get the battry voltage and check the signal strength
    GetBatteryVoltage ();
    GetSignal();
  }

  if (Count == 30 && !Status) { // is the loop counter is 30, and status counter is 0, then calculate the averages.
    GetACStats ();
    
    if (((ACVoltageAvg <MainsMinV) || (ACVoltageAvg >MainsMaxV)) || ((FrequencyOut <MainsMinF) || (FrequencyOut >MainsMaxF))){ //if mains is outside spec then transmit power fail message
          TransmitPowerFailSMS ();
          FailFlag = true; // sets a flag to say our mains is bad.
     }
      
    if (((ACVoltage >MainsMinV) && (ACVoltage <MainsMaxV)) && ((FrequencyOut >MainsMinF) && (FrequencyOut <MainsMaxF))) { //If our mains is restored, then transmit a power restored message
        TransmitPowerRestoredSMS();
        FailFlag = false;// resets the falg to say our mains is OK
    }
      
    if (DCVoltage < BatteryMin) { //Battery Under Voltage? Then transmit Low Battery
      TransmitBatteryLow();
    }
    else {
      BatteryTransmitFlag = false;
    }  
    
  }
  
  GetReply(); // clear up anything spurious in the serial buffer.
  
  if (Reply[4]=='S' && Reply[5]=='Q'){ // this routine decodes our signal strength.
    temp1=Reply[8]-'0'; //convert relevant characters from array into integers
    temp2=Reply[9]-'0';
    temp3=Reply[10]-'0';
    temp4=Reply[11]-'0';
    
    if (temp3 == -4) { // use temp3 to determine where in the array the relevent integers are (-4 is a ",")
      Signal= temp2+ temp1*10; // calculate signal if the first digit is a multiple of 10
      BER = temp4;
    }
    
    else{
     Signal= temp1; //calculate signal if the first digit is not a multiple of 10
     BER = temp3;
    }
   
    if ( Signal == 99) { // if our signal is 99, IE no signal condition , the return a signal of -1
      Signal = -1;
    }
     
    SignaldBm = Signal*2 -113; // calculate dBm for geeks like me.  

  }
  


 if (Reply[4]=='M' && Reply[5] =='T') {// If there's an incoming SMS, then check for "Check" !
   GetSMS ();        
 }

 for (int i=0; i<200; i++) { // clean up the serial reply buffer, to stop repeating actions if nothing received.
   Reply[i]=' ';
 }
 
   if (Count == 1000 && Status !=0) { // if the loop counter is 1000 and the Status counter is not 0, then resest the frequency and voltage stats, and decrement the Status counter.
    ResetStats ();
    Status --;
  }
  
    if (Count == 1000) { // If the loop counter is 1000, then reset the stats and get the signal strength.
      GetSignal ();
      ResetStats ();
    } 
}
/*
 * Copyright (c) 2015 Andrew Doswell
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHOR(S) OR COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
