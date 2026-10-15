new customer change these things.  serarch for:
    //TODO new customer
    c:\Users\mdroo\OneDrive\Firebase\aiesdoser\public
    firebase deploy --only hosting

    Automations.
    Setup customer:  
        C:\Users\mdroo\OneDrive\Documents\platformio\AI_ES_Doser_V2>py scripts\enroll_customer.py
    Build
        C:\Users\mdroo\OneDrive\Documents\platformio\AI_ES_Doser_V2>py scripts\build_device.py

    Note:  Until all customers are on new build system have to use the 
        first time feature in build builds fireware.bin vs reefDoser3.bin

when i ota i grab wrongb reefDoser.
light apex selection doesn't work both intern changes and goes back


Kalkwasser -> CaCO2 -> Calcium Hydroxide
Calcium->CACl2->Calcium Chloride
Lye->NaOH->Alkalinity
Mg->Magnisiu 


in AI_Engine.h is where you change chemistry strength
    struct {
        //TODO this is where you change chemitry strength
        float dkhPerMlKalk = 0.04, dkhPerMlAfr = 0.1, dkhPerMlAlk = 0.15, dkhPerMlNaoh = 0.25, mgPerMlMg = 5.0, caPerMlCacl2 = 553.3;
    } chem;


for eric's tank vs mine
change potency of dosing agents.
changed emercy from 1 and 15 to 45 and 1500

Yes — most important tank-specific values are already in Preferences.

For reefDoser1 / your 300g tank, you mainly need to change:

Device ID
String deviceID = "reefDoser1";

Your uploaded main.cpp currently has:

String deviceID = "reefDoser2";

So change that before flashing reefDoser1.

Dosing mode
Set dosing mode to Mode 6:
dosingMode = 6;

Better: do it from the dashboard, because the code saves it to Preferences as dosing_mode and mirrors it to Firebase /settings/dosingMode and /state/dosingMode.

Mode 6 already maps chemicals correctly:

P1 = kalk
P2 = cacl2
P3 = naoh
P4 = mg

Tank volume
Set tank volume to 300 gallons, not 1100. The code stores this in Preferences key:
t_vol

and loads it at boot:

TANK_VOLUME_L = prefs.getFloat("t_vol", 1135.6f);
ai.setTankVolumeGallons(TANK_VOLUME_L / 3.78541f);

So the dashboard volume save should be enough. 300 gallons = about 1135.6 L.

AI baseline demand
These are stored in Preferences too:
base_kalk
base_cacl2
base_naoh
base_mg

The AI adds those baseline values in Mode 6.

For your 300g tank, do not reuse Eric’s 1100g baseline numbers. Start lower. Rough scale from 1100g to 300g is:

300 / 1100 = 0.273

TODO change in code!!!!!!!!!!!!!
In AI_Engine.h 
 Eric's limits:
    float maxKalkDay = 35000.0f;
    float maxNaohDay = 800.0f;
    float maxAlkRisePerDay = 1.5f;
Mark's limits:
    float maxKalkDay = 2500.0f;
    float maxNaohDay = 100.0f;
    float maxAlkRisePerDay = 0.5f;

So Eric’s baseline values should be multiplied by about 27% as a starting point.

Safety caps need one code change
This is the big hard-coded 1100g part:
float maxKalkDay = 35000.0f;
float maxNaohDay = 800.0f;
float maxAlkRisePerDay = 1.5f;

That is labeled “large reef tuned.” For your 300g tank, I would change it back closer to:

float maxKalkDay = 2500.0f;
float maxNaohDay = 100.0f;
float maxAlkRisePerDay = 0.5f;

or at least not leave 35000 ml/day kalk available on a 300g system.

So the actual code changes are probably only:

// main.cpp
String deviceID = "reefDoser1";

and in AI_Engine.h:

struct {
    float maxKalkDay = 2500.0f;
    float maxNaohDay = 100.0f;
    float maxAlkRisePerDay = 0.5f;
} limits;

mixing______________________________
1. Kalkwasser (Pump 1)
     teaspoons calcium hydroxide per gallon RODI
2. CaCl2 (Pump 2)
    500 g calcium chloride
    Fill with RODI to 1 gallon total volume
3. NaOH (Pump 3)
    283 grams sodium hydroxide
    Fill with RODI to 1 gallon
4. Magnesium (Pump 4)
    cups magnesium chloride
    cups magnesium sulfate (Epsom salt)
    Fill to 1 gallon

Deployment id for logger: AKfycbxJ78_lQXZ6M8o24oji_OpT03Df8LllnofZJ_1Y7UpwhRKBN2mVlKQKpMd_PVJBu2pn8Q
webapp:  https://script.google.com/macros/s/AKfycbxJ78_lQXZ6M8o24oji_OpT03Df8LllnofZJ_1Y7UpwhRKBN2mVlKQKpMd_PVJBu2pn8Q/exec


Notifications we should add:
Now that foundation is there, adding future alerts becomes easy:

dosing failure
Apex disconnected
pH swing rate
reservoir low
pump runtime anomaly
leak detector
stuck dosing pump
AI confidence warnings


wow this weird firmware burn setTankVolumeGallons
put these files in a directory i.e. /reefDoserPractic/reefDoser2:
boot_app0.bin
bootloader.bin
firmware.bin for that reefDoser2 or reefDoser3...etc.
partion.bin


Look in folder (for example) : C:\Users\mdroo\OneDrive\Firebase\aiesdoser\public\installer\reefDoser2
    boot_app0.bin
    bootloader.bin
    firmware.bin (for what ever reefDoser2 you are building).
    index.html
    mainifest.json (change it to match reefDoser2)
    partitions.bin

With these files in folder
Plug chip into board!!!!
 then open browser and to to https://aiesdoser.web.app/installer/reefDoser2/ .. or 3,etc.
 YOU HAVE TO HOLD BUTTON ON CHIP WHEN LOADING.

/////////////////////////////////////////////////////////
Setting up new customer

if firebase:

reefDoser4
    chipId:"REEFDOSER4_TEST"
    claimCode:"TEST-7392"
    claimed:false
    enabled:true

got to http://aiesdoser.web.app/setup.html
  fill out page. with the about claimCode
     name
     email
     pw pw
     deviceID
     claim code
     room

This should procure a user in firebase (athenticaliotn)
 give them an account on firebase.
 set deviceClaim
    claimed = true
    associate new user to device  reefDoser4
        devices
            reefDoser4
            ownerUID  "jsdakfjksadfjaksjf"



ok doing branching for code
so have our main stream
then we have demand-learning-feedforward.
then go merge everything go back to main and runt this
   git merge demand-learning-feedforward

   namecheap.com
   Our website aidoser.tech is paypal


   email is soho email mrood@aidoser.tech


YouTube video

have chatgpt write script

open a page  setup.html  hit window-alt-r button to record
it will record and save under videos/captured

in heygen go to media - mymedia -drag the video in.
move curster to scrip and paste script.
note:  the video recording and the script should be about same length



Your EIN is 42-5345975. The name control associated with this EIN is SYMB.


ok moving to the new pcb chip have to use: ESP32-S3 Dev Module.
Enable PSRAM in Settings: The extra 8MB of PSRAM will not be utilized by default. You must explicitly toggle the PSRAM selection to "OPI PSRAM" in your IDE compile settings so the firmware initializes it at startup.

 Critical S3 Pin Safety Rules

1. Never use GPIO 26 through 32: These are permanently bound to the internal SPI Flash package.
2. Never use GPIO 33 through 37: On 8MB Octal PSRAM variants like this one, these pins drive the high-speed PSRAM interface. Toggling them will instantly crash the module.
3. GPIO 22, 25, 26, 27 mapping: As noted in our previous discussion, GPIO 22 and 25 do not physically exist on the internal ESP32-S3 chip structure. GPIO 26 and 27 are trapped inside the internal memory bus.


{
  "ota": {
    "deviceId": "reefDoser7",
    "file": "reefDoser7.bin",
    "firstTime": false,
    "trigger": true,
    "ts": 1790650256964,
    "url": "https://aiesdoser.web.app/devices/reefDoser7/reefDoser7.bin"
  }
}